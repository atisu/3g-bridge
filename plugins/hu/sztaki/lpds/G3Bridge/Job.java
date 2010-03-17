package hu.sztaki.lpds.G3Bridge;

import java.util.HashMap;

public class Job {

	public static final int PREPARE = 0;
	public static final int INIT = 1;
	public static final int RUNNING = 2;
	public static final int FINISHED = 3;
	public static final int ERROR = 4;
	public static final int TEMPFAILED = 5;
	public static final int CANCEL = 6;

	private String id;
	private String name;
	private String grid;
	private String args;
	private String gridId;
	private int status;
	private HashMap<String, String> inputs;
	private HashMap<String, String> outputs;

	public Job(String id, String name, String grid, String args, String gridid, int status) {
		this.id = id;
		this.name = name;
		this.grid = grid;
		this.args = args;
		this.gridId = gridid;
		this.status = status;

		this.inputs = new HashMap<String, String>();
		this.outputs = new HashMap<String, String>();
	}

	public String getId() {
		return id;
	}

	public String getName() {
		return name;
	}

	public String getGrid() {
		return gridId;
	}

	public String getGridId() {
		return gridId; 
	}

	public String getArgs() {
		return args;
	}

	public int getStatus() {
		return status;
	}

	public void addInput(String localName, String path) {
		inputs.put(localName, path);
	}

	public void addOutput(String localName, String path) {
		outputs.put(localName, path);
	}

	public HashMap<String, String> getInputs() {
		return inputs;
	}

	public HashMap<String, String> getOutputs() {
		return outputs;
	}

	public final void setGridId(String gridId) {
		native_setGridId(id, gridId);
		this.gridId = gridId;
	}

	public final void setStatus(int status) {
		native_setStatus(id, status);
		this.status = status;
	}

	public final void deleteJob() {
		native_deleteJob(id);
	}

	static private native void native_setGridId(String id, String gridId);

	static private native void native_setStatus(String id, int status);

	static private native void native_deleteJob(String id);

}
