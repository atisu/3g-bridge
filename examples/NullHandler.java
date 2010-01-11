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

import hu.sztaki.lpds.G3Bridge.*;
import java.util.ArrayList;

public class NullHandler extends GridHandler {

	public NullHandler(String instance) {
		super(instance);

		/* Tell the bridge that we want the polling interface */
		setPolling(true);

		/* Test the logging */
		Logger.logit(LogLevel.INFO, "Initialized");

		/* Test getConfig() */
		Logger.logit(LogLevel.INFO, "Config foo is " + getConfig("foo"));
	}

	public void submitJobs(ArrayList<Job> jobs) {
		for (Job j: jobs) {
			/* Set the job status */
			j.setStatus(Job.RUNNING);
			/* We must make up a grid ID here */
			j.setGridId("blah");
			Logger.logit(LogLevel.INFO, "Submitted " + j.getId());
		}
	}

	public void updateStatus() {
		/* This method is not used in this example but it must be defined */
	}

	public void poll(Job j) {
		j.setStatus(Job.FINISHED);
		Logger.logit(LogLevel.INFO, "Finished " + j.getId());
	}
}
