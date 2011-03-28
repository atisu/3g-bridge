/*
 * Sample Java plugin that just marks every submitted job as finished
 *
 * How to test it:
 *
 * - Compile & install the bridge (installing it is important, otherwise it
 *   won't find the plugins)
 *
 * - Run 'javac -classpath ../plugins/3g-bridge-plugin-0.96.jar NullHandler.java'
 *   to compile this Java plugin
 *
 * - Edit 3g-bridge.conf:
 *
 *   [java-example]
 *
 *   handler = Java
 *   java-home = /usr/lib/jvm/java-6-openjdk
 *   classpath = <the path to this directory>
 *   java-class = NullHandler
 *
 * - (Re-)start the 3g-bridge
 */

package hu.sztaki.lpds.G3Bridge.null;

import hu.sztaki.lpds.G3Bridge.*;
import java.util.ArrayList;

/**
 * Example NullHandler Java plugin.
 * This is an example Java plugin implementing "null" functionality.
 */
public class NullHandler extends GridHandler {

	/**
	 * NullHandler constructor.
	 * @param instance name of the plugin instance
	 */
	public NullHandler(String instance) {
		super(instance);

		/* Tell the bridge that we want the polling interface */
		setPolling(true);

		/* Test the logging */
		Logger.logit(LogLevel.INFO, "Initialized");

		/* Test getConfig() */
		Logger.logit(LogLevel.INFO, "Config foo is " + getConfig("foo"));
	}

	/**
	 * Submit jobs.
	 * This function simply sets each job's status to RUNNING.
	 * @param jobs list of jobs to "submit"
	 */
	public void submitJobs(ArrayList<Job> jobs) {
		for (Job j: jobs) {
			/* Set the job status */
			j.setStatus(Job.RUNNING);
			/* We must make up a grid ID here */
			j.setGridId("blah");
			Logger.logit(LogLevel.INFO, "Submitted " + j.getId());
		}
	}

	/**
	 * Update status.
	 */
	public void updateStatus() {
		/* This method is not used in this example but it must be defined */
	}

	/**
	 * Poll status of a job.
	 * This function simply sets the polled job's status to FINISHED.
	 * @param j the job to "update"
	 */
	public void poll(Job j) {
		j.setStatus(Job.FINISHED);
		Logger.logit(LogLevel.INFO, "Finished " + j.getId());
	}
}
