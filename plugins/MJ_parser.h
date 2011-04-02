/* -*- mode: c++; coding: utf-8-unix -*- */

#ifndef __MJ_PARSER_H_
#define __MJ_PARSER_H_

#include <string>
#include <map>
#include <iostream>
#include <exception>
#include <sstream>
#include <vector>

#include "FileRef.h"

using namespace std;

/**
 * Meta-job parser interface
 */
namespace _3gbridgeParser
{
	typedef map<string, FileRef> inputMap;
	typedef map<string, string> outputMap;

	/**
	 * Callback data for job creation
	 */
	typedef struct JobDef
	{
		JobDef() {}
		JobDef(string const &metajobid,
		       string const &grid,
		       string const &algName,
		       string const &args,
		       outputMap const &outputs,
		       inputMap const &inpMap);

		// Predefined by caller of parseMetaJob()
		string metajobid;
		string dbId; //valid only after the job has been
			     //inserted to DB
		string grid;
		string algName;
		outputMap outputs;

		// Set by parseMetaJob()
		string comment;
		string args;

		/**
		 * Contains (basename, filespec.) pairs. The set of basenames is
		 * predefined by caller, and will not be changed by
		 * parseMetaJob()
		 */
		inputMap inputs;
	} JobDef;

	/**
	 * Meta-job-global information
	 */
	typedef struct MetaJobDef
	{
		MetaJobDef()
			: count(0), startLine(0), finished(false) {}
		MetaJobDef(size_t count, size_t startLine,
			   string strRequired, string strSuccessAt)
			: count(count),
			  startLine(startLine),
			  strRequired(strRequired),
			  strSuccessAt(strSuccessAt)
		{
		}

		// Predefined by caller and changed by parseMetaJob()
		size_t count;
		size_t startLine;

		/**
		 * Percentage specifications stored by parseMetaJob()
		 *
		 *    State must be saved between calls, but can be parsed only
		 *    after total count is known.
		 */
		string strRequired;
		string strSuccessAt;

		// Set by parseMetaJob() when parsing is fully done
		size_t required;
		size_t successAt;
		bool finished;
	} MetaJobDef;

	/**
	 * Delegate type for job creation. Called by parseMetaJob on each Queue
	 * command in the meta-job file.
	 *
	 * @param jobDef Information to create the job
	 * @param count  How many copies have to be created
	 */
	typedef void (*queueJobHandler)(const JobDef &jobDef, size_t count);

	/**
	 * Parses the meta-job specification from a stream. For each job, calls
	 * the callback function handler, which should insert the jobs in the
	 * database.
	 *
	 * @param input          Stream, containing meta-job specs.
	 * @param templateJobDef Template for job definitions
	 * @param handler        Callback function to create jobs
	 * @param maxJobs        Maximum number of jobs to be generated at once
	 *
	 * @return Meta-job-global information
	 */
	void parseMetaJob(istream &input,
			  MetaJobDef &mjd,
			  JobDef &jobDef,
			  queueJobHandler handler,
			  size_t maxJobs = 0);

	namespace HelperFunctions
	{
		/**
		 * Save a meta-job-global information to stream.
		 */
		void saveMJ(ostream &output, const MetaJobDef &mjd);
		/**
		 * Save a sub-job to stream.
		 */
		void saveSJ(ostream &output, const JobDef &jd);
	}

	/**
	 * Save a meta-job to a stream. The format fill be such that
	 * it can be parsed back.
	 */
	template <class iterator_t>
	void saveMetaJob(ostream &output,
			 const MetaJobDef &mjd,
			 iterator_t first,
			 iterator_t last)
	{
		HelperFunctions::saveMJ(output, mjd);
		for (iterator_t i = first; i != last; i++)
			HelperFunctions::saveSJ(output, *i);
	}
			 

	/**
	 * Parser exception. Contains line number information.
	 */
	class ParserException : public exception
	{
		int _line;
		string _msg;
	public:
		~ParserException() throw() {}
		ParserException(int line, string msg)
			: _line(line)
		{
			ostringstream os;
			os << "Error at line " << _line << ": " << msg;
			_msg = os.str();
		}

		int line() const { return _line; }
		virtual const char* what() const throw() { return _msg.c_str(); }
	};
}

#endif //__MJ_PARSER_H_
