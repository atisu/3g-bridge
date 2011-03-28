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

import java.util.ArrayList;

/**
 * GridHandler Java interface.
 * This abstract class can be extended by Java grid plugins to implement plugin
 * functionalities.
 */
public abstract class GridHandler {

	/// Flag indicating if the plugin needs polling (true) or will perform
	/// status updates on its own (false).
	private boolean usePoll;

	/// Name of the plugin instance.
	protected String pluginInstance;

	/**
	 * Constructor.
	 * The GridHandler constructor sets the instance's name and disables
	 * polling.
	 * @param pluginInstance the instance's name
	 */
	public GridHandler(String pluginInstance) {
		usePoll = false;
		this.pluginInstance = pluginInstance;
	}

	/**
	 * Submits a list of jobs to the underlying grid architecture.
	 *
	 * The method should update the status of the jobs and also set their
	 * grid ID if the submission was successful.
	 *
	 * @param jobs the list of jobs to submit.
	 */
	public abstract void submitJobs(ArrayList<Job> jobs);

	/**
	 * Updates the status of all submitted jobs.
	 *
	 * It is called by the bridge when setPolling(false) is in effect.
	 */
	public abstract void updateStatus();

	/**
	 * Checks and updates the status of a single job.
	 *
	 * It is called by the bridge when setPolling(true) is in effect.
	 */
	public abstract void poll(Job job);

	/**
	 * Returns a configuration string with the given key.
	 *
	 * The key is looked up in the plugin's configuration section in 3g-bridge.conf.
	 * If there is no such configuration key, null is returned.
	 *
	 * @param key the key to look up.
	 * @return The Value of the key or null if the key was not found.
	 */
	public final String getConfig(String key) {
		return native_getConfig(pluginInstance, key);
	}

	/// Native configuration query function.
	private static native String native_getConfig(String instance, String key);

	/**
	 * Sets the update mode.
	 *
	 * There are two ways the bridge can call the plugin to update the
	 * status of submitted jobs. In the first mode, the bridge calls the
	 * updateStatus() method without arguments. This is useful if the
	 * underlying grid middleware provides status notifications for the
	 * executed jobs, as the plugin can use these notifications and does
	 * not have to poll jobs that are still running. The second mode calls
	 * the poll() method for every submitted job. Use this method if the
	 * grid middleware can not provide state change notifications.
	 *
	 * @param mode		if true, the poll() method will be used;
	 * 			otherwise, the updateStatus() method
	 * 			will be called by the bridge.
	 */
	public final void setPolling(boolean mode) {
		usePoll = mode;
	}
}
