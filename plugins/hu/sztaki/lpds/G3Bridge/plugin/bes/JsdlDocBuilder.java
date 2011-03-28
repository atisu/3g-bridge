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


import hu.sztaki.lpds.G3Bridge.*;

import org.ggf.schemas.jsdl.x2005.x11.jsdl.JobDefinitionDocument;
import org.ggf.schemas.jsdl.x2005.x11.jsdl.JobDescriptionType;
import org.ggf.schemas.jsdl.x2005.x11.jsdl.ApplicationType;
import org.ggf.schemas.jsdl.x2005.x11.jsdl.DataStagingType;
import org.ggf.schemas.jsdl.x2005.x11.jsdl.SourceTargetType;
import org.ggf.schemas.jsdl.x2005.x11.jsdl.CreationFlagEnumeration;
import org.ggf.schemas.jsdl.x2006.x07.jsdlHpcpa.HPCProfileApplicationDocument;
import org.ggf.schemas.jsdl.x2006.x07.jsdlHpcpa.HPCProfileApplicationType;

import org.apache.xmlbeans.XmlObject;

import java.util.HashMap;


/**
 * This class is responsible to generate correct JSDL descriptions and wrapper scripts of jobs for a BES plugin instance.
 * @author Peter Modos (modos@sztaki.hu) MTA-SZTAKI LPDS
 */
public class JsdlDocBuilder {

	// external address of the plugin's FTP server
	private String ftpAddress;

	// path of the base (root) directory of the FTP server in the local filesystem
	private String ftpBaseDir;

	// user name in the FTP server accounts for the BES plugin instance
	private String ftpUserName;

	// user password in the FTP server accounts for the BES plugin instance
	private String ftpUserPsw;

	// has the FTP server anonymous access
	private boolean isAnonymous;

	// is necessary generating wrapper script for the BES plugin instance
	private boolean isWrapperNeeded;

	// base name of the generated wrapper script files
	private String wrapperBaseFileName;

	// location of the directory contains the generated wrapper script files in the local file system
	private String wrapperBaseDir;

	/**
	 * Initializes the JsdlDocBuilder object for further usage according to the parameters.
	 * @param ftpAddress external address of the plugin's FTP server
	 * @param ftpBaseDir path of the base (root) directory of the FTP server in the local filesystem
	 * @param ftpUserName user name in the FTP server accounts for the BES plugin instance
	 * @param ftpUserPsw user password in the FTP server accounts for the BES plugin instance
	 * @param isWrapperNeeded is necessary generating wrapper script for the BES plugin instance
	 * @param wrapperBaseFileName base name of the generated wrapper script files
	 * @param wrapperBaseDir location of the directory contains the generated wrapper script files in the local file system
	 */
	public JsdlDocBuilder(String ftpAddress, String ftpBaseDir, boolean ftpAnonymous, String ftpUserName, String ftpUserPsw, boolean isWrapperNeeded, String wrapperBaseFileName, String wrapperBaseDir) {
		this.ftpAddress = ftpAddress;
		this.ftpBaseDir = ftpBaseDir;
		this.isAnonymous = ftpAnonymous;
		this.ftpUserName = ftpUserName;
		this.ftpUserPsw = ftpUserPsw;
		this.isWrapperNeeded = isWrapperNeeded;
		this.wrapperBaseFileName = wrapperBaseFileName;
		this.wrapperBaseDir = wrapperBaseDir;
	}



	/**
	 * Generates and returns a JobDefinitionDocument object what contains the
	 * JSDL description of the parameter job.
	 * @param job
	 * @return JobDefinitionDocument object
	 * @throws Exception contians file information which is not accessable under ftp root dir
	 */
	public JobDefinitionDocument generateJSDLDoc(Job job) throws Exception {
		JobDefinitionDocument jobDefDoc = JobDefinitionDocument.Factory.newInstance();;
		JobDescriptionType jobDescription = jobDefDoc.addNewJobDefinition().addNewJobDescription();

		if (isWrapperNeeded) {
			FileRef fr = new FileRef(wrapperBaseDir + "/" + wrapperBaseFileName + job.getId(), "", 0);
			job.addInput(wrapperBaseFileName + job.getId(), fr);
		}

		buildApplication(jobDescription, job);
		buildDataStaging(jobDescription, job);

		return jobDefDoc;
	}


	/**
	 * Fills the jobDescription object with the necessary HPC Profile Application description
	 * information according to the data stored in the job object.
	 * @param jobDescription
	 * @param job
	 */
	private void buildApplication(JobDescriptionType jobDescription, Job job) {
		HPCProfileApplicationDocument hpcDoc = HPCProfileApplicationDocument.Factory.newInstance();
		HPCProfileApplicationType hpcType = hpcDoc.addNewHPCProfileApplication();

		if (isWrapperNeeded) {
			hpcType.addNewExecutable().setStringValue("/bin/bash");
			hpcType.addNewArgument().setStringValue(wrapperBaseFileName + job.getId());
		} else {
			hpcType.addNewExecutable().setStringValue(job.getName());
			hpcType.addNewArgument().setStringValue(job.getArgs());
		}

		hpcType.addNewOutput().setStringValue(BESHandler.BES_STDOUT_FILENAME);
		hpcType.addNewError().setStringValue(BESHandler.BES_STDERR_FILENAME);

		ApplicationType appType = jobDescription.addNewApplication();
		appType.set(hpcDoc);
	}


	/**
	 * Fills the jobDescription object with the necessary data staging description
	 * information according to the input/output files data stored in the job object.
	 * @param jobDescription
	 * @param job
	 * @throws Exception contians file information which is not accessable under ftp root dir
	 */
	private void buildDataStaging(JobDescriptionType jobDescription, Job job) throws Exception {
		HashMap<String, FileRef> inputFileList = job.getInputs();
		for (String inputFile : inputFileList.keySet()) {
			DataStagingType dataType = jobDescription.addNewDataStaging();
			dataType.setFileName(inputFile);
			dataType.setCreationFlag(CreationFlagEnumeration.OVERWRITE);
			dataType.setDeleteOnTermination(false);
			SourceTargetType stt = SourceTargetType.Factory.newInstance();
			stt.setURI(getFileFTPAddress(inputFileList.get(inputFile).getURL()));
			dataType.setSource(stt);
			if (!isAnonymous) {
				try {
					XmlObject o;
					String s = dataType.toString();
					s = s.replaceAll("</xml-fragment>", "<Credential  xmlns=\"http://schemas.ogf.org/hpcp/2007/11/ac\"><UsernameToken xmlns=\"http://docs.oasis-open.org/wss/2004/01/oasis-200401-wss-wssecurity-secext-1.0.xsd\"><Username>" + ftpUserName + "</Username><Password>" + ftpUserPsw + "</Password></UsernameToken></Credential></xml-fragment>");
					o = XmlObject.Factory.parse(s);
					dataType.set(o);
				} catch(Exception e) {
					// officially can't be exception here...
					e.printStackTrace();
				}
			}
		}

		HashMap<String, String> outputFileList = job.getOutputs();
		for (String outputFile : outputFileList.keySet()) {
			DataStagingType dataType = jobDescription.addNewDataStaging();
			dataType.setFileName(outputFile);
			dataType.setCreationFlag(CreationFlagEnumeration.OVERWRITE);
			dataType.setDeleteOnTermination(false);
			SourceTargetType stt = SourceTargetType.Factory.newInstance();
			stt.setURI(getFileFTPAddress(outputFileList.get(outputFile)));
			dataType.setTarget(stt);
			if (!isAnonymous) {
				try {
					XmlObject o;
					String s = dataType.toString();
					s = s.replaceAll("</xml-fragment>", "<Credential  xmlns=\"http://schemas.ogf.org/hpcp/2007/11/ac\"><UsernameToken xmlns=\"http://docs.oasis-open.org/wss/2004/01/oasis-200401-wss-wssecurity-secext-1.0.xsd\"><Username>" + ftpUserName + "</Username><Password>" + ftpUserPsw + "</Password></UsernameToken></Credential></xml-fragment>");
					o = XmlObject.Factory.parse(s);
					dataType.set(o);
				} catch (Exception e) {
					// officially can't be exception here...
					e.printStackTrace();
				}
			}
		}
	}


	/**
	 * Returns what will be the file's external FTP address, according to the
	 * ftpBaseDir and ftpAddress member variable values and the parameters.
	 * @param path path of the file in the local filesystem
	 * @return JobDefinitionDocument object
	 * @throws Exception contains file information which is not accessible under ftp root directory
	 */
	private String getFileFTPAddress(String path) throws Exception {
		if (!path.startsWith(ftpBaseDir)) {
			throw new Exception("The following input/output file is not under the FTP root dir: " + path);
		}
		path = path.replaceFirst(ftpBaseDir, ftpAddress);
		if (isAnonymous) {
			path = path.replaceFirst("ftp://", "ftp://anonymous@");
		}
		return path;
	}

}
