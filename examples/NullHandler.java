import hu.sztaki.lpds.G3Bridge.*;
import java.util.ArrayList;

/*
 * Example for the GridHandler Java API
 */

public class NullHandler extends GridHandler {

	public NullHandler(String instance) {
		super();
		setPolling(true);
	}

	public void submitJobs(ArrayList<Job> jobs) {
		for (Job j: jobs) {
			j.setStatus(Job.RUNNING);
		}
	}

	public void updateStatus() {
		/* This method is not used but it must be defined */
	}

	public void poll(Job j) {
		j.setStatus(Job.FINISHED);
	}
}
