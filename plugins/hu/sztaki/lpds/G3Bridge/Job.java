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

package hu.sztaki.lpds.G3Bridge;

import java.util.HashMap;

/**
 * Job Java class.
 * The Job class stores every important information about Java Job objects.
 */
public class Job {

	/// Value of the PREPARE status
	public static final int PREPARE = 0;

	/// Value of the INIT status
	public static final int INIT = 1;

	/// Value of the RUNNING status
	public static final int RUNNING = 2;

	/// Value of the FINISHED status
	public static final int FINISHED = 3;

	/// Value of the ERROR status
	public static final int ERROR = 4;

	/// Value of the TEMPFAILED status
	public static final int TEMPFAILED = 5;

	/// Value of the CANCEL status
	public static final int CANCEL = 6;

	/// The job's internal identifier
	private String id;

	/// The algorithm/executable name of the job
	private String name;

	/// The grid plugin the job is assigned to
	private String grid;

	/// Command-line arguments to the job
	private String args;

	/// The job's grid identifier
	private String gridId;

	/// Status of the job
	private int status;

	/// Input files references by the job
	private HashMap<String, FileRef> inputs;

	/// Expected output files of the job
	private HashMap<String, String> outputs;

	/**
	 * Constructor.
	 * This constructor creates a job object.
	 * @param id the job's 3G Bridge identifier
	 * @param name name of the executable
	 * @param grid destination plugin's name
	 * @param args command-line arguments
	 * @param gridid the job's identifier in the destination grid
	 * @param status status of the job
	 */
	public Job(String id, String name, String grid, String args, String gridid, int status) {
		this.id = id;
		this.name = name;
		this.grid = grid;
		this.args = args;
		this.gridId = gridid;
		this.status = status;

		this.inputs = new HashMap<String, FileRef>();
		this.outputs = new HashMap<String, String>();
	}

	/**
	 * Get job's 3G Bridge identifier.
	 * @return job's 3G Bridge identifier
	 */
	public String getId() {
		return id;
	}

	/**
	 * Get executable's name.
	 * @return executable's name
	 */
	public String getName() {
		return name;
	}

	/**
	 * Get job's grid.
	 * @return job's grid
	 */
	public String getGrid() {
		return gridId;
	}

	/**
	 * Get job's grid identifier.
	 * @return job's grid identifier
	 */
	public String getGridId() {
		return gridId; 
	}

	/**
	 * Get job's command-line arguments.
	 * @return job's command-line arguments
	 */
	public String getArgs() {
		return args;
	}

	/**
	 * Get job's status.
	 * @return job's status
	 */
	public int getStatus() {
		return status;
	}

	/**
	 * Add an input reference to the job.
	 * @param localName the input file's local name
	 * @param fileref reference to the input file
	 */
	public void addInput(String localName, FileRef fileref) {
		inputs.put(localName, fileref);
	}

	/**
	 * Add an output file to the job.
	 * @param localName the output file's local name
	 * @param path expected path of the output file
	 */
	public void addOutput(String localName, String path) {
		outputs.put(localName, path);
	}

	/**
	 * Get inputs of the job.
	 * @return HashMap of local name -> file reference
	 */
	public HashMap<String, FileRef> getInputs() {
		return inputs;
	}

	/**
	 * Get outputs of the job.
	 * @return HashMap of local name -> path
	 */
	public HashMap<String, String> getOutputs() {
		return outputs;
	}

	/**
	 * Set the job's grid identifier.
	 * @param gridId the grid identifier to set
	 */
	public final void setGridId(String gridId) {
		native_setGridId(id, gridId);
		this.gridId = gridId;
	}

	/**
	 * Set the job's status.
	 * @param status the status to set
	 */
	public final void setStatus(int status) {
		native_setStatus(id, status);
		this.status = status;
	}

	/**
	 * Delete the job.
	 */
	public final void deleteJob() {
		native_deleteJob(id);
	}

	/// Native function to set grid identifier.
	static private native void native_setGridId(String id, String gridId);

	/// Native function to set status.
	static private native void native_setStatus(String id, int status);

	/// Native function to delete the job.
	static private native void native_deleteJob(String id);

}
