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
	private HashMap<String, FileRef> inputs;
	private HashMap<String, String> outputs;

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

	public void addInput(String localName, FileRef fileref) {
		inputs.put(localName, fileref);
	}

	public void addOutput(String localName, String path) {
		outputs.put(localName, path);
	}

	public HashMap<String, FileRef> getInputs() {
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
