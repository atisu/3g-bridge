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

}
