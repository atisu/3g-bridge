package hu.sztaki.lpds.G3Bridge;

import java.util.ArrayList;

public abstract class GridHandler {

	private boolean usePoll;

	public GridHandler() {
		usePoll = false;
	}

	public abstract void submitJobs(ArrayList<Job> jobs);

	public abstract void updateStatus();

	public abstract void poll(Job job);

	/**
	 * Sets the callback to use.
	 *
	 * @param mode		if true, the poll() method will be used;
	 * 			otherwise, the updateStatus() method
	 * 			will be called by the native code.
	 */
	public final void setPolling(boolean mode) {
		usePoll = mode;
	}
}
