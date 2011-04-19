#include "MJ_parser.h"
#include "Util.h"

#include <assert.h>
#include <cstdio>
#include <cstring>
#include "autoarray"
#include <math.h>

using namespace std;

#define LINE_BUF_SIZE (1024)

// Messages
static CSTR_C ERR_MJ_UNFINISHED =
	"Trying to calculate percentage values for an unfinished meta-job.";
static CSTR_C ERR_INVALID_INPUTFILE =
	"Invalid input file specification.";
static CSTR_C ERR_SYN_EQ_EXPECTED =
	"Syntax error: '=' expected.";
static CSTR_C ERR_MJ_FINISHED =
	"Parser called on finished meta-job.";
static CSTR_C ERR_INT_REQD =
	"Invalid value. Integer required.";
static CSTR_C ERR_REQD_SPECD_MULT =
	"%Required directive specified multiple times.";
static CSTR_C ERR_SUCCAT_SPECD_MULT =
	"%SuccessAt directive specified multiple times.";
static CSTR_C ERR_SYN_KW_EXPECTED =
	"Syntax error: Keyword or directive expected.";
static CSTR_C ERR_SYN_INVALID_ARG =
	"Syntax error: Invalid argument.";

// Keywords

static const char EQ = '=';
static CSTR_C KW_QUEUE = "Queue";
static CSTR_C KW_INPUT = "Input";
static CSTR_C KW_ARGS = "Arguments";
static CSTR_C KW_ID = "%JobId";
static CSTR_C KW_COMMENT = "%Comment";
static CSTR_C KW_REQ = "%Required";
static CSTR_C KW_SUCCAT = "%SuccessAt";

// Range for %Required keywords
static CSTR_C REQ_ALL = "All";
static CSTR_C REQ_RNG [] = { REQ_ALL, 0L }; //for iterating; to check

// Range for %SuccessAt keywords
static CSTR_C SUCCAT_ALL = "All";
static CSTR_C SUCCAT_REQD = "Required";
static CSTR_C SUCCAT_RNG [] = { SUCCAT_ALL, SUCCAT_REQD, 0L }; //for iterating

// Default values for directives
static CSTR_C REQ_DEFAULT = REQ_ALL;
static CSTR_C SUCCAT_DEFAULT = SUCCAT_ALL;

enum CountType
{
	Keyword,
	Percentage,
	Piece,
};
struct Count
{
	Count() {}
	Count(CSTR kw) : type(Keyword), keyword(kw) {}
	static Count CountPercent(int pc)
	{
		Count c; c.type = Percentage; c.percent = pc; return c;
	}
	static Count CountPiece(int pc)
	{
		Count c; c.type = Piece; c.piece = pc; return c;
	}

	CountType type;
	union
	{
		CSTR keyword;
		int percent;
		int piece;
	};
};

// <Helper functions>

/**
 * Move pointer to next non-space */
#define SKIPSPACES(c) { while (isspace(*(c))) (c)++; }
/**
 * Make sure '=' sign exists, skip if does */
#define CHECK_EQ_SIGN(c) {				   \
		if (*(c) != EQ)			   \
			throw string(ERR_SYN_EQ_EXPECTED); \
		else (c)++;				   \
	}

/**
 * Determines if the given character is a keyword character. */
static bool kwchar(char c);
/**
 * Right-trim the given string */
static void rtrim(char *str);
/**
 * Cut keyword from string, return: keyword and pointer to char after keyword */
static string cutKeyword(char *start, char **end); //return: keyword
/**
 * Parse args of Input keyword; return: key */
static FileRef parseInputSpecs(int lineNum, CSTR spec, string &logicalName);
/**
 * Parse a single line of input
 * Return: How many jobs to queue */
static int parseLine(int lineNum,
		     char *line,
		     _3gbridgeParser::JobDef &jd,
		     _3gbridgeParser::MetaJobDef &mjd);
/**
 * Parse an argument for %Required or %SuccessAt */
static Count parsePcArg(int lineNum, CSTR value, CSTR_C rng[]);
/**
 * Calculate the required parameter for a fully parsed meta-job. */
static void parseReqdPc(_3gbridgeParser::MetaJobDef &mjd);
/**
 * Calculate the successAt parameter for a fully parsed meta-job. */
static void parseSuccAtPc(_3gbridgeParser::MetaJobDef &mjd);

// </Helper functions>

// Exports
namespace _3gbridgeParser
{
	CSTR_C _METAJOB_SPEC_PREFIX = "_3gb-metajob";
	size_t const _METAJOB_SPEC_PREFIX_LEN = 12;

	void parseMetaJob(void *instance,
			  istream &input,
			  MetaJobDef &mjd,
			  JobDef &jobDef,
			  queueJobHandler handler,
			  size_t maxJobs)
	{
		if(mjd.finished)
			throw ParserException(-1, ERR_MJ_FINISHED);

		size_t lineNum = 0;
		size_t jobCount = 0;

		{ //enclose buf
			AutoArray<char, LINE_BUF_SIZE> buf;

			while (input.good() && (!maxJobs || jobCount < maxJobs))
			{
				input.getline(buf.array(), LINE_BUF_SIZE - 1);
				lineNum += 1;

				//skip to starLine, then
				if (lineNum > mjd.startLine)
				{
					int cnt = parseLine(lineNum, buf.array(),
							    jobDef, mjd);
					if (cnt > 0)
					{
						handler(instance, jobDef, cnt);
						jobCount += cnt;
						//id cannot be inherited;
						//clearing it
						jobDef.comment = string();
					}
				}
			}

			// Skip blank lines

			// Useful if maxJobs and end of specs are reached
			// together, and there are still blank lines at the
			// eof. We can avoid an extra iteration of no use.
			while (input.good())
			{
				input.getline(buf.array(), LINE_BUF_SIZE - 1);
				char *i = buf.array();
				SKIPSPACES(i);
				if (*i) //non-empty line
					break;
				lineNum++; //skip empty line
			}

		} // buf is freed here

		// Save state for next iteration
		mjd.startLine = lineNum;
		mjd.count += jobCount;

		// Meta-job is finished
		if (!input.good())
		{
			mjd.finished = true;
			// Calculate actual numbers now
			parseReqdPc(mjd);
			parseSuccAtPc(mjd);
		}
	} //parseMetaJob

	JobDef::JobDef(string const &metajobid,
		       string const &grid,
		       string const &algName,
		       string const &args,
		       outputMap const &outputs,
		       inputMap const &inpMap)
		: metajobid(metajobid), grid(grid), algName(algName),
		  outputs(outputs), args(args), inputs(inpMap)
	{
	}

	MetaJobDef::MetaJobDef()
		: count(0), startLine(0),
		  strRequired(REQ_DEFAULT),
		  strSuccessAt(SUCCAT_DEFAULT),
		  required(0), successAt(0),
		  finished(false)
	{
	}

	void saveMJ(ostream &output, const MetaJobDef &mjd)
	{
		output << KW_REQ <<' '<< mjd.required << endl
		       << KW_SUCCAT<<' '<< mjd.successAt << endl
		       << "# Total generated: " << mjd.count << endl
		       << endl;
	}

	void saveSJ(ostream &output, const JobDef &jd)
	{
		output << KW_COMMENT <<' '<< jd.comment << endl
		       << KW_ID <<' '<< jd.dbId << endl
		       << KW_ARGS <<' '<<EQ<<' '<< jd.args << endl;

		for (inputMap::const_iterator i = jd.inputs.begin();
		     i != jd.inputs.end(); i++)
		{
			if (!strncmp(i->first.c_str(),
				     _METAJOB_SPEC_PREFIX,
				     _METAJOB_SPEC_PREFIX_LEN)) //startswith
				continue;

			const FileRef &fr = i->second;
			output << KW_INPUT<<' '<<EQ<<' '
			       << i->first<<EQ<< fr.getURL();
			if (fr.getMD5())
			{
				output << EQ << fr.getMD5();
				if (fr.getSize() >= 0)
					output << EQ << fr.getSize();
			}
			output << endl;
		}

		output << KW_QUEUE << endl << endl;
	}
}

using _3gbridgeParser::ParserException;

static int parseLine(int lineNum,
		     char *line,
		     _3gbridgeParser::JobDef &jd,
		     _3gbridgeParser::MetaJobDef &mjd)
{
	char *bok = line;

	SKIPSPACES(bok); //Trim left -- keyword
	if (!*bok || *bok == '#') return 0; //Empty line/comment

	char *value;
	string keyword = cutKeyword(bok, &value);
	rtrim(value);

	if (keyword == KW_QUEUE)
	{
		int numJobs = 1;
		if (*value) {
			if (!sscanf(value, "%d", &numJobs))
				throw ParserException(lineNum, ERR_INT_REQD);
		}
		return numJobs;
	}
	else if (keyword == KW_INPUT)
	{
		CHECK_EQ_SIGN(value); SKIPSPACES(value);
		string ifname;
		const FileRef ifinstance = parseInputSpecs(lineNum, value, ifname);
		jd.inputs[ifname] = ifinstance;
	}
	else if (keyword == KW_ARGS)
	{
		CHECK_EQ_SIGN(value); SKIPSPACES(value);
		jd.args = string(value);
	}
	else if (keyword == KW_ID)
	{
		// This information is supplied by the meta-job plugin for the
		// user. It's not used as an input parameter.
	}
	else if (keyword == KW_COMMENT)
	{
		jd.comment = string(value);
	}
	else if (keyword == KW_REQ)
	{
		if (!mjd.strRequired.empty())
			throw ParserException(lineNum, ERR_REQD_SPECD_MULT);
		parsePcArg(lineNum, value, REQ_RNG);
		mjd.strRequired = value;
	}
	else if (keyword == KW_SUCCAT)
	{
		if (!mjd.strSuccessAt.empty())
		 	throw ParserException(lineNum, ERR_SUCCAT_SPECD_MULT);
		parsePcArg(lineNum, value, SUCCAT_RNG);
		mjd.strSuccessAt = value;
	}
	else throw ParserException(lineNum, ERR_SYN_KW_EXPECTED);

	return 0; // keyword isn't 'Queue' -- don't generate any jobs now
}

static void rtrim(char *str)
{
	char *trimmer = str;

	while (*trimmer) trimmer++;          //find end
	trimmer--;                           //standing on \0, step back
	while (isspace(*trimmer)) trimmer--; //skip spaces backwards
	trimmer++;                     //standing on last non-space; step forward
	*trimmer = 0;                  //cut off spaces
}

static bool kwchar(char c)
{
	return isalnum(c) || c=='.' || c=='-' || c == '_' || c =='%';
}

static string cutKeyword(char *start, char **end)
{
	//Find end of keyword
	char* eok = start;
	while (kwchar(*eok)) eok++;

	//Cut keyword
	string keyword(start, eok-start);

	SKIPSPACES(eok); //Trim left -- remains: '=[ \t]*<value>'
			 //                  or:        '<value>'

	*end = eok;
	return keyword;
}

static FileRef parseInputSpecs(int lineNum, CSTR spec, string &logicalName)
{
	CSTR i = spec;
	while (*i && *i != EQ) i++; //find first '='
	if (!*i) throw ParserException(lineNum, ERR_INVALID_INPUTFILE);

	CSTR_C endkey = i;

	i++; //Skip '='
	SKIPSPACES(i);

	istringstream is(i);
	string token;
	string url, md5;
	off_t isize = -1;

	if (!getline(is, token, EQ))
		throw new ParserException(lineNum, ERR_SYN_INVALID_ARG);
	url.swap(token);
	if (getline(is, token, EQ))
	{
		md5.swap(token);
		if (getline(is, token, EQ))
		{
			if (1 != sscanf(token.c_str(), "%ld", &isize))
				throw new ParserException(lineNum,
							  ERR_SYN_INVALID_ARG);
		}
	}
	string key = string(spec, endkey-spec);
	logicalName.swap(key);
	return FileRef(url,
		       md5.empty() ? 0L : md5.c_str(),
		       isize);
}

static Count parsePcArg(int lineNum, CSTR value, CSTR_C rng[])
{
	// check if value is a keyword in range
	for (CSTR_C *i = rng; *i; i++)
		if (!strcmp(value, *i))
			return Count(*i);

	// not a keyword, parse as numeric
	int val; char c; int pRead;
	//try as percentage
	if (2 == (pRead = sscanf(value, "%d%c", &val, &c)))
	{
		if (c != '%' || val < 0 || val > 100)
			throw ParserException(lineNum, ERR_SYN_INVALID_ARG);
		return Count::CountPercent(val);
	}
	//not a percentage, it's a number
	else if (1 == pRead)
	{
		return Count::CountPiece(val);
	}
	else
	{
		throw ParserException(lineNum, ERR_SYN_INVALID_ARG);
	}
}

static void parseReqdPc(_3gbridgeParser::MetaJobDef &mjd)
{
	if (!mjd.finished)
		throw ParserException(-1, ERR_MJ_UNFINISHED);

	Count value(REQ_DEFAULT);
	if (!mjd.strRequired.empty())
		value = parsePcArg(-1, mjd.strRequired.c_str(), REQ_RNG);

	//Translate percentage values to actual numbers considering mjd.count
	switch (value.type)
	{
	case Percentage:
		mjd.required = int(ceil(mjd.count*value.percent / 100.0));
		break;
	case Piece:
		mjd.required = value.piece;
		break;
	case Keyword:
		if (value.keyword == REQ_ALL) // == 100%
			mjd.required = mjd.count;
		else
			assert(0); // Theoretically impossible
		break;
	}
}

static void parseSuccAtPc(_3gbridgeParser::MetaJobDef &mjd)
{
	if (!mjd.finished)
		throw ParserException(-1, ERR_MJ_UNFINISHED);

	Count value(SUCCAT_DEFAULT);
	if (!mjd.strSuccessAt.empty())
		value = parsePcArg(-1, mjd.strSuccessAt.c_str(), SUCCAT_RNG);

	//Translate percentage values to actual numbers considering mjd.count
	switch (value.type)
	{
	case Percentage:
		mjd.successAt = int(ceil(mjd.count * value.percent / 100.0));
		break;
	case Piece:
		mjd.successAt = value.piece;
		break;
	case Keyword:
		if (value.keyword == SUCCAT_REQD) // == %Required
			mjd.successAt = mjd.required;
		else if (value.keyword == SUCCAT_ALL) // == 100%
			mjd.successAt = mjd.count;
		else
			assert(0); // Theoretically impossible
		break;
	}
}
