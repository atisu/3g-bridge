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

package hu.sztaki.lpds.G3Bridge.plugin.bes;

import hu.sztaki.lpds.G3Bridge.Job;
import hu.sztaki.lpds.G3Bridge.Logger;
import hu.sztaki.lpds.G3Bridge.LogLevel;

import java.io.File;
import java.io.IOException;
import java.io.Writer;
import java.io.FileWriter;
import java.io.BufferedWriter;


/**
 * This class is responsible to generate the wrapper scripts of jobs for a BES plugin instance.
 * @author Peter Modos (modos@sztaki.hu) MTA-SZTAKI LPDS
 */
public class WrapperBuilder {

	// base name of the generated wrapper script files
	private String wrapperBaseFileName;

	// location of the directory contains the generated wrapper script files in the local file system
	private String wrapperBaseDir;


	public WrapperBuilder(String wrapperBaseFileName, String wrapperBaseDir) {
		this.wrapperBaseFileName = wrapperBaseFileName;
		this.wrapperBaseDir = wrapperBaseDir;
	}


	/**
	 * Generates the contents and makes the wrapper script file for the parameter job.
	 * The file will be stored in the local file system, according to the object's settings
	 * (location= [wrapperBaseDir]/[wrapperBaseFileName][job ID]).
	 * @param job
	 * @param LOG logger object for storing messages
	 * @param pluginName name of the BES plugin instance (for logging)
	 * @throws Exception contains error messages in connection with file handling
	 */
	public void generateWrapperFile(Job job, String pluginName) throws Exception {
		String fullFilePath = wrapperBaseDir + "/" + wrapperBaseFileName + job.getId();
		Writer writer = null;

		try {
			File file = new File(fullFilePath);
			writer = new BufferedWriter(new FileWriter(file));

			String line = "#!/bin/bash\n";
			line += "chmod 755 " + job.getName() +"\n";
			line += "LD_LIBRARY_PATH=.:$LD_LIBRARY_PATH ./"+ job.getName() +" "+ job.getArgs();

			writer.write(line);
		} catch(Exception e) {
			throw new Exception(" Could not create wrapper file: " + e.getMessage());
		} finally {
			try {
				if (writer != null) {
					writer.close();
				}
			} catch(IOException e) {
				throw new Exception(" Could not create wrapper file: " + e.getMessage());
			}
		}
		Logger.logit(LogLevel.INFO, "BES plugin (" + pluginName + "): wrapper script file is ready for job id = "+job.getId()+", [file location] = " + fullFilePath );
	}


	/**
	 * Deletes the wrapper file belonging to a job.
	 * @param job the Job whose wrapper should be removed
	 */
	public void deleteWrapper(Job job) {
		String fullFilePath = wrapperBaseDir + "/" + wrapperBaseFileName + job.getId();
		try {
			File file = new File(fullFilePath);
			file.delete();
		} catch (Exception e) {}
	}

}
