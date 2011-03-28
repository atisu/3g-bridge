/*
 * Copyright (C) 2009-2010 MTA SZTAKI LPDS
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * In addition, as a special exception, MTA SZTAKI gives you permission to link the
 * code of its release of the 3G Bridge with the OpenSSL project's "OpenSSL"
 * library (or with modified versions of it that use the same license as the
 * "OpenSSL" library), and distribute the linked executables. You must obey the
 * GNU General Public License in all respects for all of the code used other than
 * "OpenSSL". If you modify this file, you may extend this exception to your
 * version of the file, but you are not obligated to do so. If you do not wish to
 * do so, delete this exception statement from your version.
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "Java_handler.h"
#include "DBHandler.h"
#include "Job.h"
#include "Util.h"
#include "Conf.h"

#include <stdlib.h>
#include <string.h>
#include <dlfcn.h>
#include <ftw.h>

using namespace std;

/**********************************************************************
 * Definitions
 */

/* Native Java classes */
#define CLASS_ArrayList \
	"java/util/ArrayList"
#define CTOR_ArrayList \
	"(I)V"
#define SIG_ArrayList_add \
	"(Ljava/lang/Object;)Z"
#define SIG_Class_getName \
	"()Ljava/lang/String;"
#define CLASS_ClassNotFoundException \
	"java/lang/ClassNotFoundException"
#define CLASS_IllegalArgumentException \
	"java/lang/IllegalArgumentException"
#define CLASS_NullPointerException \
	"java/lang/NullPointerException"
#define CLASS_System \
	"java/lang/System"
#define SIG_System_getProperty \
	"(Ljava/lang/String;)Ljava/lang/String;"

/* Classes defined in package hu.sztaki.lpds.G3Bridge */
#define CLASS_GridHandler \
	"hu/sztaki/lpds/G3Bridge/GridHandler"
#define CTOR_GridHandler \
	"(Ljava/lang/String;)V"
#define SIG_GridHandler_submitJobs \
	"(Ljava/util/ArrayList;)V"
#define SIG_GridHandler_updateStatus \
	"()V"
#define SIG_GridHandler_poll \
	"(Lhu/sztaki/lpds/G3Bridge/Job;)V"
#define CLASS_Job \
	"hu/sztaki/lpds/G3Bridge/Job"
#define CLASS_FileRef \
	"hu/sztaki/lpds/G3Bridge/FileRef"
#define CTOR_Job \
	"(Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;I)V"
#define CTOR_FileRef \
	"(Ljava/lang/String;Ljava/lang/String;I)V"
#define SIG_Job_addInput \
	"(Ljava/lang/String;Lhu/sztaki/lpds/G3Bridge/FileRef;)V"
#define SIG_Job_addOutput \
	"(Ljava/lang/String;Ljava/lang/String;)V"
#define CLASS_Logger \
	"hu/sztaki/lpds/G3Bridge/Logger"
#define CLASS_RuntimeBridgeException \
	"hu/sztaki/lpds/G3Bridge/RuntimeBridgeException"
#define CTOR_RuntimeBridgeException \
	"(Ljava/lang/String;Ljava/lang/Throwable;)V"


/**********************************************************************
 * Macros
 */

#define GET_CTOR(env, name, cls, meth) \
	get_constructor(env, CLASS_ ## name, CTOR_ ## name, cls, meth)
#define GET_METH(env, cls, clsname, methname) \
	get_method_id(env, cls, G_STRINGIFY(methname), \
		SIG_ ## clsname ## _ ## methname)


/**********************************************************************
 * Prototypes
 */

static jclass find_class(JNIEnv *env, const char *classname);
static int get_constructor(JNIEnv *env, const char *clsname, const char *args,
	jclass *cls, jmethodID *meth);
static jmethodID get_method_id(JNIEnv *env, jclass cls, const char *methodname,
	const char *signature);
static jfieldID get_field_id(JNIEnv *env, jclass cls, const char *fieldname,
	const char *signature);

typedef jint (*create_vm_func)(JavaVM **pvm, void **penv, void *args);


/**********************************************************************
 * Global variables
 */

static char *libjvm_path;

/* This is a small layering violation, but... */
static GKeyFile *global_config;


/**********************************************************************
 * Utility functions
 */

static string string_from_java(JNIEnv *env, jstring str)
{
	const char *p = env->GetStringUTFChars(str, NULL);
	string ret = p;
	env->ReleaseStringUTFChars(str, p);
	return ret;
}

static bool check_exception(JNIEnv *env)
{
	jthrowable exc = env->ExceptionOccurred();
	if (!exc)
		return false;

	jclass cls = env->GetObjectClass(exc);
	jmethodID meth = env->GetMethodID(cls, "toString", "()Ljava/lang/String;");
	jstring msgobj = (jstring)env->CallObjectMethod(exc, meth);
	string msg = string_from_java(env, msgobj);
	logit(LOG_ERR, "Java exception occured: %s", msg.c_str());
	env->DeleteLocalRef(msgobj);
	env->DeleteLocalRef(cls);
	env->DeleteLocalRef(exc);
	return true;
}

static jclass find_class(JNIEnv *env, const char *classname)
{
	jclass cls;

	cls = env->FindClass(classname);
	if (!cls)
	{
		check_exception(env);
		char *p = g_strdup(classname);
		for (unsigned i = 0; p[i]; i++)
			if (p[i] == '/')
				p[i] = '.';
		logit(LOG_ERR, "Class %s was not found", classname);
		g_free(p);
	}
	return cls;
}

static int get_constructor(JNIEnv *env, const char *classname, const char *signature,
	jclass *cls, jmethodID *meth)
{
	*cls = find_class(env, classname);
	if (!*cls)
		return -1;

	*meth = env->GetMethodID(*cls, "<init>", signature);
	if (!*meth)
	{
		check_exception(env);
		char *p = g_strdup(classname);
		for (unsigned i = 0; p[i]; i++)
			if (p[i] == '/')
				p[i] = '.';

		logit(LOG_ERR, "Class %s has no constructor with "
			"signature %s", p, signature);
		g_free(p);

		return -1;
	}
	return 0;
}

static jmethodID get_method_id(JNIEnv *env, jclass cls, const char *methodname,
	const char *signature)
{
	jmethodID meth = env->GetMethodID(cls, methodname, signature);
	if (meth)
		return meth;

	if (check_exception(env))
		return NULL;

	/* Determine the class name */
	jclass clscls = env->GetObjectClass(cls);
	meth = env->GetMethodID(clscls, "getName", SIG_Class_getName);
	jstring namestr = (jstring)env->CallObjectMethod(cls, meth);
	env->DeleteLocalRef(clscls);
	if (!namestr)
	{
		check_exception(env);
		return NULL;
	}

	string name = string_from_java(env, namestr);
	env->DeleteLocalRef(namestr);

	logit(LOG_ERR, "JNI: Failed to locate method %s (signature: '%s') of "
		"class %s", methodname, signature, name.c_str());
	return NULL;
}

static jfieldID get_field_id(JNIEnv *env, jclass cls, const char *fieldname,
	const char *signature)
{
	jfieldID field = env->GetFieldID(cls, fieldname, signature);
	if (field)
		return field;

	if (check_exception(env))
		return NULL;

	/* Determine the class name */
	jclass clscls = env->GetObjectClass(cls);
	jmethodID meth = env->GetMethodID(clscls, "getName", SIG_Class_getName);
	jstring namestr = (jstring)env->CallObjectMethod(cls, meth);
	env->DeleteLocalRef(clscls);
	if (!namestr)
	{
		check_exception(env);
		return NULL;
	}

	string name = string_from_java(env, namestr);
	env->DeleteLocalRef(namestr);

	logit(LOG_ERR, "JNI: Failed to locate field %s (signature: '%s') of "
		"class %s", fieldname, signature, name.c_str());
	return NULL;
}

static int look_for_libjvm(const char *fpath, const struct stat *sb, int typeflag, struct FTW *ftwbuf)
{
	if (typeflag != FTW_F)
		return 0;
	if (strcmp(fpath + ftwbuf->base, "libjvm.so"))
		return 0;
	libjvm_path = g_strdup(fpath);
	return 1;
}

static void *try_load_jvm(const char *homedir)
{
	g_free(libjvm_path);
	libjvm_path = NULL;

	nftw(homedir, look_for_libjvm, 15, 0);
	if (!libjvm_path)
		return NULL;

	void *res = dlopen(libjvm_path, RTLD_NOW);
	if (res)
		logit(LOG_INFO, "Using JVM %s", libjvm_path);
	return res;
}

static int JNICALL vfprintf_hook(FILE *f, const char *fmt, va_list ap)
{
	char *msg = g_strdup_vprintf(fmt, ap);
	int ret = strlen(msg);
	logit(LOG_NOTICE, "Java: %s", msg);
	g_free(msg);
	return ret;
}

/**********************************************************************
 * Java native methods
 */

static void JNICALL Job_setGridId(JNIEnv *env, jclass cls, jstring idstr, jstring grididstr)
{
	string id = string_from_java(env, idstr);
	string gridid = string_from_java(env, grididstr);

	DBHandler *dbh = DBHandler::get();
	auto_ptr<Job> job = dbh->getJob(id.c_str());
	DBHandler::put(dbh);

	job->setGridId(gridid.c_str());
}

static void JNICALL Job_setStatus(JNIEnv *env, jclass cls, jstring idstr, jint status)
{
	string id = string_from_java(env, idstr);

	DBHandler *dbh = DBHandler::get();
	auto_ptr<Job> job = dbh->getJob(id.c_str());
	DBHandler::put(dbh);

	job->setStatus((Job::JobStatus)status);
}

static void JNICALL Job_deleteJob(JNIEnv *env, jclass cls, jstring idstr)
{
	string id = string_from_java(env, idstr);

	DBHandler *dbh = DBHandler::get();
	dbh->deleteJob(id.c_str());
	DBHandler::put(dbh);
}

static JNINativeMethod JobMethods[] =
{
	{(char *)"native_setGridId",	(char *)"(Ljava/lang/String;Ljava/lang/String;)V",	(void *)Job_setGridId},
	{(char *)"native_setStatus",	(char *)"(Ljava/lang/String;I)V",			(void *)Job_setStatus},
	{(char *)"native_deleteJob",	(char *)"(Ljava/lang/String;)V",			(void *)Job_deleteJob},
};

static jstring JNICALL GridHandler_getConfig(JNIEnv *env, jclass cls, jstring inststr, jstring keystr)
{
	string instance = string_from_java(env, inststr);
	string key = string_from_java(env, keystr);
	
	gchar *value = g_key_file_get_string(global_config, instance.c_str(), key.c_str(), NULL);
	g_strstrip(value);
	jstring valstr;
	if (value)
		valstr = env->NewStringUTF(value);
	else
		valstr = NULL;
	g_free(value);
	return valstr;
}

static JNINativeMethod GridHandlerMethods[] =
{
	{(char *)"native_getConfig",	(char *)"(Ljava/lang/String;Ljava/lang/String;)Ljava/lang/String;",	(void *)GridHandler_getConfig},
};

static void JNICALL Logger_logit(JNIEnv *env, jclass cls, jint level, jstring msgstr)
{
	if (!msgstr || level < 0)
		return;

	string msg = string_from_java(env, msgstr);
	logit(level, msg.c_str());
}

static JNINativeMethod LoggerMethods[] =
{
	{(char *)"native_logit",	(char *)"(ILjava/lang/String;)V",					(void *)Logger_logit},
};

/**********************************************************************
 * Class: JavaHandler
 */

static string get_classpath(GKeyFile *config, const char *instance)
{
	string path;
	char *p;

	path = (char *)"-Djava.class.path=" JAVA_CLASSPATH;
	p = g_key_file_get_string(config, instance, "classpath", NULL);
	if (p)
	{
		g_strstrip(p);
		path.push_back(':');
		path += p;
		g_free(p);
	}
	return path;
}

static void force_load_and_register(JNIEnv *env, const char *className,
	JNINativeMethod *native_meths, unsigned count)
{
	/* http://bugs.sun.com/bugdatabase/view_bug.do?bug_id=6493522 */
	jclass cls = find_class(env, className);
	if (!cls)
		throw new BackendException("Plugin initialization failed");
	jclass clsobj = env->GetObjectClass(cls);
	jmethodID meth = env->GetMethodID(clsobj, "getConstructors", "()[Ljava/lang/reflect/Constructor;");
	/* We intentionally leak here */
	(void)env->CallObjectMethod(cls, meth);
	env->DeleteLocalRef(clsobj);

	if (!native_meths)
		return;
	if (env->RegisterNatives(cls, native_meths, count) < 0)
	{
		check_exception(env);
		throw new BackendException("Failed to register Job methods");
	}
}

JavaHandler::JavaHandler(GKeyFile *config, const char *instance) throw (BackendException *): GridHandler(config, instance)
{
	jmethodID meth;
	jclass cls;

	name = instance;
	groupByNames = false;

	jvm = NULL;
	objref = NULL;

	/* Locate & load the JVM library */
	void *lib_handle = NULL;
	char *opt = g_key_file_get_string(config, instance, "java-home", NULL);
	if (opt)
	{
		g_strstrip(opt);
		lib_handle = try_load_jvm(opt);
		g_free(opt);
	}
	if (!lib_handle)
	{
		opt = getenv("JAVA_HOME");
		if (opt)
			lib_handle = try_load_jvm(opt);
	}
	if (!lib_handle)
		throw new BackendException("Failed to locate libjvm.so");

	create_vm_func create_vm = (create_vm_func)dlsym(lib_handle, "JNI_CreateJavaVM");
	if (!create_vm)
		throw new BackendException("Symbol lookup failed: %s", dlerror());

	/* Remember the configuration */
	global_config = config;

	/* Set up the JVM options */
	string classpath = get_classpath(config, instance);
	JavaVMOption options[] =
	{
		{ (char *)classpath.c_str(),		NULL },
		{ (char *)"vfprintf",			(void *)vfprintf_hook },
#if 0
		{ (char *)"-Xcheck:jni",		NULL },
#endif
	};

	/* Create the JVM */
	JavaVMInitArgs vm_args;
	vm_args.version = JNI_VERSION_1_4;
	vm_args.nOptions = G_N_ELEMENTS(options);
	vm_args.options = options;
	vm_args.ignoreUnrecognized = JNI_TRUE;

	jint res = create_vm(&jvm, (void **)&env, &vm_args);
	if (res < 0)
		throw new BackendException("Failed to create the Java VM");
	logit(LOG_INFO, "Java VM created for %s", instance);

	force_load_and_register(env, CLASS_GridHandler, GridHandlerMethods, G_N_ELEMENTS(GridHandlerMethods));
	force_load_and_register(env, CLASS_Job, JobMethods, G_N_ELEMENTS(JobMethods));
	force_load_and_register(env, CLASS_Logger, LoggerMethods, G_N_ELEMENTS(LoggerMethods));

	/* Get the name of the handler class */
	char *class_name = g_key_file_get_string(config, instance, "java-class", NULL);
	if (!class_name)
		throw new BackendException("Missing handler-class for %s", instance);
	g_strstrip(class_name);
	/* Convert foo.bar to foo/bar */
	for (int i = 0; class_name[i]; i++)
		if (class_name[i] == '.')
			class_name[i] = '/';
	if (get_constructor(env, class_name, CTOR_GridHandler, &cls, &meth))
		throw new BackendException("Plugin initialization failed");
	g_free(class_name);

	jstring argstr = env->NewStringUTF(instance);
	if (!argstr)
	{
		env->DeleteLocalRef(cls);
		throw new BackendException("Failed to create String");
	}

	jobject obj = env->NewObject(cls, meth, argstr);
	if (!obj)
	{
		env->DeleteLocalRef(cls);
		env->DeleteLocalRef(argstr);
		throw new BackendException("Object creation failed");
	}

	env->DeleteLocalRef(cls);
	env->DeleteLocalRef(argstr);

	objref = env->NewGlobalRef(obj);
	env->DeleteLocalRef(obj);
}

JavaHandler::~JavaHandler()
{
	if (objref)
		env->DeleteGlobalRef(objref);

	if (jvm)
	{
		logit(LOG_INFO, "Shutting down Java VM");
		jvm->DestroyJavaVM();
	}
}

static jobject fileref_to_java(JNIEnv *env, const FileRef &fr)
{
	jstring url, md5;
	jint size;
	jmethodID meth;
	jclass cls;
	jobject retval;

	url = env->NewStringUTF(fr.getURL().c_str());
	md5 = env->NewStringUTF(fr.getMD5().c_str());
	size = fr.getSize();
	if (!url || !md5)
	{
		check_exception(env);
		if (url)
			env->DeleteLocalRef(url);
		if (md5)
			env->DeleteLocalRef(md5);
		return 0;
	}

	if (GET_CTOR(env, FileRef, &cls, &meth))
	{
		env->DeleteLocalRef(url);
		env->DeleteLocalRef(md5);
	}

	retval = env->NewObject(cls, meth, url, md5, size);
	env->DeleteLocalRef(url);
	env->DeleteLocalRef(md5);
	if (!retval)
		check_exception(env);

	env->DeleteLocalRef(cls);
	return retval;

}

static jobject job_to_java(JNIEnv *env, Job *job)
{
	jstring idstr, namestr, gridstr, argsstr, grididstr;
	jmethodID meth;
	jobject retval;
	jclass cls;

	idstr = env->NewStringUTF(job->getId().c_str());
	namestr = env->NewStringUTF(job->getName().c_str());
	gridstr = env->NewStringUTF(job->getGrid().c_str());
	argsstr = env->NewStringUTF(job->getArgs().c_str());
	grididstr = env->NewStringUTF(job->getGridId().c_str());
	if (!idstr || !namestr || !gridstr || !argsstr || !grididstr)
	{
		check_exception(env);
		if (idstr)
			env->DeleteLocalRef(idstr);
		if (namestr)
			env->DeleteLocalRef(namestr);
		if (gridstr)
			env->DeleteLocalRef(gridstr);
		if (argsstr)
			env->DeleteLocalRef(argsstr);
		if (grididstr)
			env->DeleteLocalRef(grididstr);
		return 0;
	}

	if (GET_CTOR(env, Job, &cls, &meth))
	{
		env->DeleteLocalRef(idstr);
		env->DeleteLocalRef(namestr);
		env->DeleteLocalRef(gridstr);
		env->DeleteLocalRef(argsstr);
		env->DeleteLocalRef(grididstr);
		return 0;
	}

	retval = env->NewObject(cls, meth, idstr, namestr, gridstr, argsstr, grididstr, job->getStatus());
	env->DeleteLocalRef(idstr);
	env->DeleteLocalRef(namestr);
	env->DeleteLocalRef(gridstr);
	env->DeleteLocalRef(argsstr);
	env->DeleteLocalRef(grididstr);
	if (!retval)
	{
		check_exception(env);
		env->DeleteLocalRef(cls);
		return retval;
	}

	/* Add the input files */
	meth = GET_METH(env, cls, Job, addInput);
	if (!meth)
	{
		env->DeleteLocalRef(cls);
		env->DeleteLocalRef(retval);
		return NULL;
	}

	auto_ptr< vector<string> > inputs = job->getInputs();
	for (vector<string>::iterator it = inputs->begin(); it != inputs->end(); it++)
	{
		jstring lfnstr;
		jobject fileref;

		lfnstr = env->NewStringUTF((*it).c_str());
		fileref = fileref_to_java(env, job->getInputRef(*it));
		if (!lfnstr || !fileref)
		{
			check_exception(env);
			if (lfnstr)
				env->DeleteLocalRef(lfnstr);
			if (fileref)
				env->DeleteLocalRef(fileref);
			env->DeleteLocalRef(cls);
			env->DeleteLocalRef(retval);
			return NULL;
		}

		env->CallVoidMethod(retval, meth, lfnstr, fileref);
		env->DeleteLocalRef(lfnstr);
		env->DeleteLocalRef(fileref);
		if (check_exception(env))
		{
			env->DeleteLocalRef(cls);
			env->DeleteLocalRef(retval);
			return NULL;
		}
	}

	/* Add the output files */
	meth = GET_METH(env, cls, Job, addOutput);
	if (!meth)
	{
		env->DeleteLocalRef(cls);
		env->DeleteLocalRef(retval);
		return NULL;
	}

	auto_ptr< vector<string> > outputs = job->getOutputs();
	for (vector<string>::iterator it = outputs->begin(); it != outputs->end(); it++)
	{
		jstring lfnstr, pathstr;

		lfnstr = env->NewStringUTF((*it).c_str());
		pathstr = env->NewStringUTF(job->getOutputPath(*it).c_str());
		if (!lfnstr || !pathstr)
		{
			check_exception(env);
			if (lfnstr)
				env->DeleteLocalRef(lfnstr);
			if (pathstr)
				env->DeleteLocalRef(pathstr);
			env->DeleteLocalRef(cls);
			env->DeleteLocalRef(retval);
			return NULL;
		}

		env->CallVoidMethod(retval, meth, lfnstr, pathstr);
		env->DeleteLocalRef(lfnstr);
		env->DeleteLocalRef(pathstr);
		if (check_exception(env))
		{
			env->DeleteLocalRef(cls);
			env->DeleteLocalRef(retval);
			return NULL;
		}
	}

	env->DeleteLocalRef(cls);
	return retval;
}

void JavaHandler::submitJobs(JobVector &jobs) throw (BackendException *)
{
	jobject aryobj, jobobj;
	jmethodID meth;
	jclass cls;

	if (GET_CTOR(env, ArrayList, &cls, &meth))
		throw new BackendException("Constructor lookup failed");
	aryobj = env->NewObject(cls, meth, (jint)jobs.size());
	if (!aryobj)
	{
		check_exception(env);
		throw new BackendException("Array creation failed");
	}

	meth = GET_METH(env, cls, ArrayList, add);
	if (!meth)
	{
		env->DeleteLocalRef(aryobj);
		env->DeleteLocalRef(cls);
		throw new BackendException("Method lookup failed");
	}

	for (JobVector::iterator it = jobs.begin(); it != jobs.end(); it++)
	{
		jobobj = job_to_java(env, *it);
		if (!jobobj)
		{
			check_exception(env);
			env->DeleteLocalRef(aryobj);
			env->DeleteLocalRef(cls);
			throw new BackendException("Job conversion failed");
		}
		env->CallBooleanMethod(aryobj, meth, jobobj);
		env->DeleteLocalRef(jobobj);
		if (check_exception(env))
		{
			env->DeleteLocalRef(aryobj);
			env->DeleteLocalRef(cls);
			throw new BackendException("Java exception occured");
		}
	}

	env->DeleteLocalRef(cls);
	cls = env->GetObjectClass(objref);
	meth = GET_METH(env, cls, GridHandler, submitJobs);
	if (!meth)
	{
		env->DeleteLocalRef(aryobj);
		env->DeleteLocalRef(cls);
		throw new BackendException("Method lookup failed");
	}

	env->CallVoidMethod(objref, meth, aryobj);
	env->DeleteLocalRef(aryobj);
	env->DeleteLocalRef(cls);
	if (check_exception(env))
		throw new BackendException("Java exception occured");
}

void JavaHandler::updateStatus(void) throw (BackendException *)
{
	jclass cls = env->GetObjectClass(objref);
	jfieldID field = get_field_id(env, cls, "usePoll", "Z");
	if (!field)
	{
		env->DeleteLocalRef(cls);
		throw new BackendException("Field lookup failed");
	}
	jboolean use_poll = env->GetBooleanField(objref, field);

	if (use_poll)
	{
		env->DeleteLocalRef(cls);
		DBHandler *dbh = DBHandler::get();
		dbh->pollJobs(this, Job::RUNNING, Job::CANCEL);
		DBHandler::put(dbh);
		return;
	}

	jmethodID meth = GET_METH(env, cls, GridHandler, updateStatus);
	if (!meth)
	{
		env->DeleteLocalRef(cls);
		throw new BackendException("Method lookup failed");
	}
	env->CallVoidMethod(objref, meth);
	env->DeleteLocalRef(cls);
	if (check_exception(env))
		throw new BackendException("Java exception occured");
}

void JavaHandler::poll(Job *job) throw (BackendException *)
{
	jobject jobobj = job_to_java(env, job);
	if (!jobobj)
		throw new BackendException("Job object conversion failed");

	jclass cls = env->GetObjectClass(objref);
	jmethodID meth = GET_METH(env, cls, GridHandler, poll);
	if (!meth)
	{
		env->DeleteLocalRef(cls);
		env->DeleteLocalRef(jobobj);
		throw new BackendException("Method lookup failed");
	}
	logit(LOG_DEBUG, "Calling Java poll");
	env->CallVoidMethod(objref, meth, jobobj);
	env->DeleteLocalRef(cls);
	env->DeleteLocalRef(jobobj);
	if (check_exception(env))
		throw new BackendException("Java exception occured");
}

/**********************************************************************
 * Factory function
 */

HANDLER_FACTORY(config, instance)
{
	return new JavaHandler(config, instance);
}
