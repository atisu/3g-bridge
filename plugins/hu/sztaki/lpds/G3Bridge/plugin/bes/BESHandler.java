package hu.sztaki.lpds.G3Bridge.plugin.bes;

import java.io.File;

import java.util.ArrayList;
import java.util.HashMap;
import java.util.Map;

import hu.sztaki.lpds.G3Bridge.*;

import de.fzj.unicore.bes.client.FactoryClient;
import de.fzj.unicore.bes.faults.UnknownActivityIdentifierFault;
import de.fzj.unicore.bes.security.UsernameOutHandler;
import de.fzj.unicore.uas.security.IUASSecurityProperties;
import de.fzj.unicore.uas.security.UASSecurityProperties;
import de.fzj.unicore.wsrflite.security.ISecurityProperties;
import org.ggf.schemas.bes.x2006.x08.besFactory.ActivityDocumentType;
import org.ggf.schemas.bes.x2006.x08.besFactory.CreateActivityDocument;
import org.ggf.schemas.bes.x2006.x08.besFactory.CreateActivityResponseDocument;
import org.ggf.schemas.bes.x2006.x08.besFactory.ActivityStateEnumeration;
import org.w3.x2005.x08.addressing.EndpointReferenceType;
import org.apache.xmlbeans.XmlException;

public class BESHandler extends GridHandler {

	protected static final String BES_STDOUT_FILENAME = "bes_stdout.log";
	protected static final String BES_STDERR_FILENAME = "bes_stderr.log";

	// client object for the communication with the remote WS
	private FactoryClient client = null;

	// object for making wrapper scripts (when necessary)
	private WrapperBuilder wrapperBuider = null;

	// object for generating the JSDL description of jobs
	private JsdlDocBuilder jsdlDocBuider = null;

	// contains whether wrapper script generating is necessary for this BES plugin instance or not
	private boolean wrapperIsNeeded;

	private static final Map<String, String> propertyNames = new HashMap<String, String>();
	static {
		// name of the requested property keys:
		propertyNames.put("FTP_ADDR", "ftpserver.address");
		propertyNames.put("FTP_BASEDIR", "ftpserver.basedir");
		propertyNames.put("FTP_IS_ANONYMOUS", "ftpserver.anonymous");
		propertyNames.put("FTP_USERNAME", "ftpserver.username");
		propertyNames.put("FTP_PASSWORD", "ftpserver.password");

		propertyNames.put("WS_ADDR", "beswebservice.address");
		propertyNames.put("WS_SSL_USERKEY_FILE", "beswebservice.ssl.userkey.file");
		propertyNames.put("WS_SSL_USERKEY_PSW", "beswebservice.ssl.userkey.password");
		propertyNames.put("WS_SSL_TRUSTSTORE_FILE", "beswebservice.ssl.truststore.file");
		propertyNames.put("WS_SSL_TRUSTSTORE_PSW", "beswebservice.ssl.truststore.password");

		propertyNames.put("WS_WRAPPER_ISNEEDED", "beswebservice.wrapperscript.isneeded");
		propertyNames.put("WS_WRAPPER_BASEFILENAME", "beswebservice.wrapperscript.filename");
		propertyNames.put("WS_WRAPPER_BASEDIR", "beswebservice.wrapperscript.basedir");
	}

	// map object describes the connection between BES and 3GBridge statuses
	private static final Map<ActivityStateEnumeration.Enum, Integer> statusRelations = new HashMap<ActivityStateEnumeration.Enum, Integer>();
	static {
		statusRelations.put(ActivityStateEnumeration.PENDING, new Integer(Job.RUNNING));
		statusRelations.put(ActivityStateEnumeration.RUNNING, new Integer(Job.RUNNING));
		statusRelations.put(ActivityStateEnumeration.FINISHED, new Integer(Job.FINISHED));
		statusRelations.put(ActivityStateEnumeration.CANCELLED, new Integer(Job.ERROR));
		statusRelations.put(ActivityStateEnumeration.FAILED, new Integer(Job.ERROR));
	}


	/**
	 * Initializes the AbstractGridHandler superclass
	 * and the BESManager object according to the properties parameter.
	 * @param properties contains the requested settings of the submitter plugin (read before from a properties file)
	 * @param dbHandler the object for database access
	 */
	public BESHandler(String instance) throws RuntimeBridgeException {
		super(instance);
		setPolling(true);
		try {
			checkProperties();

			// FTP related settings
			String ftpUser = "";
			String ftpPsw = "";
			String ftpAddress = getConfig(propertyNames.get("FTP_ADDR"));
			String path = getConfig(propertyNames.get("FTP_BASEDIR"));
			boolean isAnonymous = Boolean.parseBoolean(getConfig(propertyNames.get("FTP_IS_ANONYMOUS")));
			if (!isAnonymous) {
				ftpUser = getConfig(propertyNames.get("FTP_USERNAME"));
				ftpPsw = getConfig(propertyNames.get("FTP_PASSWORD"));
			}

			// wrapper script related settings
			String wrapperBaseFileName = "";
			String wrapperBaseDir = "";
			wrapperIsNeeded = Boolean.parseBoolean(getConfig(propertyNames.get("WS_WRAPPER_ISNEEDED")));
			if (wrapperIsNeeded) {
				wrapperBaseFileName = getConfig(propertyNames.get("WS_WRAPPER_BASEFILENAME"));
				wrapperBaseDir = getConfig(propertyNames.get("WS_WRAPPER_BASEDIR"));
				wrapperBuider = new WrapperBuilder(wrapperBaseFileName, wrapperBaseDir);
			}

			// create jsdlBuilder
			jsdlDocBuider = new JsdlDocBuilder(ftpAddress, path, isAnonymous, ftpUser, ftpPsw, wrapperIsNeeded, wrapperBaseFileName, wrapperBaseDir);
			Logger.logit(LogLevel.INFO, "BES plugin (" + pluginInstance + "): plugin ready for usage. [FTP address] = " + ftpAddress + ", [FTP root dir in local filesystem] = " + path +", [wrapper script needed] = "+ wrapperIsNeeded);
		} catch (RuntimeBridgeException e) {
			Logger.logit(LogLevel.ERROR, "BES plugin (" + pluginInstance + "): runtine exception occured: " + e.getMessage());
			throw(e);
		}
	}


	/**
	 * Returns the BES client and create it during first calling.
	 * @return BES client object
	 * @throws Exception contains the error message, why could not create BES client object
	 */
	private FactoryClient getClient() throws Exception {
		if(client == null) {
			client = createFactoryClient();
		}
		return client;
	}


	/**
	 * Report a job's status as failed. Also removes the job's wrapper file.
	 * @param job the Job to set to ERROR
	 */
	private void reportError(Job job) {
		wrapperBuider.deleteWrapper(job);
		job.setStatus(Job.ERROR);
	}


	/**
	 * Submits the list of jobs to the target BES system.
	 * @param jobs list of jobs requested for submit
	 * @throws RuntimeBridgeException
	 */
	public void submitJobs(ArrayList<Job> jobs) throws RuntimeBridgeException {
		if (jobs.isEmpty()) {
			return;
		}

		for (Job job : jobs) {
			try {
				CreateActivityDocument req = CreateActivityDocument.Factory.newInstance();
				ActivityDocumentType activityDocType = req.addNewCreateActivity().addNewActivityDocument();
				CreateActivityResponseDocument resp = null;

				activityDocType.setJobDefinition(jsdlDocBuider.generateJSDLDoc(job).getJobDefinition());

				if (wrapperIsNeeded)
					wrapperBuider.generateWrapperFile(job, pluginInstance);

				// System.out.print(req); // print out the request jsdl message
				resp = getClient().createActivity(req);

				if (resp != null) {
					job.setGridId(resp.getCreateActivityResponse().getActivityIdentifier().xmlText());
				} else {
					throw new Exception("The answer message from the webservice is empty!");
				}
				job.setStatus(Job.RUNNING);
			} catch (Exception e) {
				Logger.logit(LogLevel.ERROR, "BES plugin (" + pluginInstance + " ): failed to submit job: " + e.getMessage());
				reportError(job);
			}
		}
	}


	/**
	 * Updates the status of all jobs connected to this BES plugin.
	 * @throws RuntimeBridgeException
	 */
	public void updateStatus() throws RuntimeBridgeException {
	}


	/**
	 * According to the local status of the parameter job it asks the actual status
	 * of the job from the remote WS or requests the termination of the job.
	 * @param job
	 * @throws RuntimeBridgeException
	 */
	public void poll(Job job) throws RuntimeBridgeException {
		if (job.getStatus() == Job.RUNNING) {
			updateJob(job);
		} else if (job.getStatus() == Job.CANCEL) {
			cancelJob(job, true);
		}
	}


	/**
	 * Asks the actual status of the parameter job from the remote WS and updates the local
	 * status of the job according to the answer. When it can not get the answer during
	 * MAXTRIES number of tries, it changes the status of the job to ERROR.
	 * @param job
	 * @throws RuntimeBridgeException
	 */
	private void updateJob(Job job) throws RuntimeBridgeException {
		ActivityStateEnumeration.Enum besStatus = null;
		try {
			EndpointReferenceType epr = EndpointReferenceType.Factory.parse(job.getGridId());
			besStatus = getClient().getActivityStatus(epr);
			Logger.logit(LogLevel.DEBUG, "BES plugin (" + pluginInstance + "): status of job " + job.getId() + " is: " + besStatus);
			// if the job is officially finished, but the output files haven't arrived, it is an error
			if (besStatus == ActivityStateEnumeration.FINISHED && !haveOutputFilesArrived(job)) {
				//LOG.warn("Finished but maybe files are not here: " + haveOutputFilesArrived(job));
				besStatus = ActivityStateEnumeration.FAILED;
			}
			if (besStatus != null) {
				job.setStatus(statusRelations.get(besStatus));
				if (statusRelations.get(besStatus) == Job.FINISHED || statusRelations.get(besStatus) == Job.ERROR)
					wrapperBuider.deleteWrapper(job);
			} else {
				Logger.logit(LogLevel.ERROR, "BES plugin (" + pluginInstance + "): status of job " + job.getId() + " is null for some reason, setting status to ERROR!");
				reportError(job);
			}
		} catch (UnknownActivityIdentifierFault e) {
			Logger.logit(LogLevel.ERROR, "BES plugin (" + pluginInstance + "): job " + job.getId() + " doesn't exist on the BES server, setting status to ERROR!");
			reportError(job);
		} catch (XmlException e) {
			Logger.logit(LogLevel.ERROR, "BES plugin (" + pluginInstance + "): couldn't parse grid ID of job " + job.getId() + ", setting status to ERROR!");
			reportError(job);
		} catch (Exception e) {
			if (e.getMessage() != null)
				Logger.logit(LogLevel.ERROR, "BES plugin (" + pluginInstance + "): couldn't update status of job " + job.getId() + ": " + e.getMessage());
			else
				Logger.logit(LogLevel.ERROR, "BES plugin (" + pluginInstance + "): couldn't update status of job " + job.getId() + " due to an unkown exception.");
			reportError(job);
		}
	}


	/**
	 * Asks the remote WS to abort the parameter job. If parameter clean is true,
	 * also delete the job description from the database.
	 * @param job
	 * @param clean
	 */
	private void cancelJob(Job job, boolean clean) {
		EndpointReferenceType epr = null;
		Logger.logit(LogLevel.INFO, "BES plugin (" + pluginInstance + "): about to cancel job " + job.getId());
		try {
			epr = EndpointReferenceType.Factory.parse(job.getGridId());
			getClient().terminateActivity(epr);
			job.deleteJob();
			wrapperBuider.deleteWrapper(job);
		} catch (UnknownActivityIdentifierFault e) {
			Logger.logit(LogLevel.ERROR, "BES plugin (" + pluginInstance + "): unable to cancel job " + job.getId() + ", as no job with the given grid ID does not exist in the BES server.");
		} catch (XmlException e) {
			Logger.logit(LogLevel.ERROR, "BES plugin (" + pluginInstance + "): unable to cancel job " + job.getId() + ", as could not parse grid ID: " + e.getMessage());
		} catch (Exception e) {
			Logger.logit(LogLevel.ERROR, "BES plugin (" + pluginInstance + "): unable to cancel job " + job.getId() + ": " + e.getMessage());
		}
	}


	/**
	 * Checks if all of the requested output files have arrived
	 * @param job
	 */
	private boolean haveOutputFilesArrived(Job job) {
		Logger.logit(LogLevel.DEBUG, "BES plugin (" + pluginInstance + "): checking output files of job " + job.getId());
		HashMap<String, String> outputs = job.getOutputs();
		for (String outputName : outputs.keySet()) {
			String outputPath = outputs.get(outputName);
			File file = new File(outputPath);
			if (!file.exists()) {
				return false;
			}
		}
		return true;
	}


	/**
	 * Creates a new BES client object according the settings of the BESManager object,
	 * set the parameters of the WS connection.
	 * @return new FactoryClient (BES client) object
	 * @throws Exception contains the error message, why could not create BES client object
	 */
	private FactoryClient createFactoryClient() throws Exception{
		FactoryClient client = null;

		EndpointReferenceType eprt = EndpointReferenceType.Factory.newInstance();
		String wsAddress = getConfig(propertyNames.get("WS_ADDR"));
		eprt.addNewAddress().setStringValue(wsAddress);

		try {
			IUASSecurityProperties p = makeSecurityPropertiesWithProxy();
			client = new FactoryClient(wsAddress, eprt, p);
		} catch(Throwable e) {
			e.printStackTrace();
			throw new Exception("Could not create client for WS ("+wsAddress+"): " + e.getMessage());
		}

		Logger.logit(LogLevel.INFO, "BES plugin (" + pluginInstance + "): client ready for WS (" + wsAddress + ").");
		return client;
	}


	/**
	 * Creates and returns an IUASSecurityProperties object what contains the
	 * security description of the WS connection.
	 * @return IUASSecurityProperties object
	 */
	private IUASSecurityProperties makeSecurityPropertiesWithProxy() {
		UASSecurityProperties p = new UASSecurityProperties();

		String userKeyFile = getConfig(propertyNames.get("WS_SSL_USERKEY_FILE"));
		String userKeyPsw = getConfig(propertyNames.get("WS_SSL_USERKEY_PSW"));
		String trustStoreFile = getConfig(propertyNames.get("WS_SSL_TRUSTSTORE_FILE"));
		String trustStorePsw = getConfig(propertyNames.get("WS_SSL_TRUSTSTORE_PSW"));

		p.setProperty(ISecurityProperties.WSRF_SSL, "true");
		p.setProperty(ISecurityProperties.WSRF_SSL_CLIENTAUTH, "true");

		p.setProperty(ISecurityProperties.WSRF_SSL_KEYSTORE, userKeyFile);
		p.setProperty(ISecurityProperties.WSRF_SSL_KEYTYPE, "pkcs12");
		p.setProperty(ISecurityProperties.WSRF_SSL_KEYPASS, userKeyPsw);

		p.setProperty(ISecurityProperties.WSRF_SSL_TRUSTSTORE, trustStoreFile);
		p.setProperty(ISecurityProperties.WSRF_SSL_TRUSTPASS, trustStorePsw);
		p.setProperty(ISecurityProperties.WSRF_SSL_TRUSTTYPE, "JKS");

		//p.setProperty(IUASSecurityProperties.UAS_OUTHANDLER_NAME, TDOutHandler.class.getName() + " " + DSigOutHandler.class.getName());
		p.setProperty(IUASSecurityProperties.UAS_OUTHANDLER_NAME, UsernameOutHandler.class.getName());

		p.setSignMessage(false);
		p.setAddTrustDelegation(true);
		return p;
	}


	private void checkProperties() {
		String errorMsg = "";
		for (String propertyName : propertyNames.values()) {
			String value = getConfig(propertyName);
			if (value == null || value.trim().equals("")) {
				errorMsg += " " + propertyName;
			}
		}
		if (errorMsg.length() > 0) {
			errorMsg = "Could not initialize BES plugin! The following properties are missing or empty:" + errorMsg;
			throw new RuntimeBridgeException(errorMsg);
		}
	}

}
