/**
 *
 * @author Jaime Garcia + David Fischer
 */
package hu.sztaki.lpds.G3Bridge.plugin.xwch;

import hu.sztaki.lpds.G3Bridge.*;
import java.io.*;
import java.net.URL;
import java.nio.channels.Channels;
import java.nio.channels.ReadableByteChannel;
import java.text.DateFormat;
import java.text.SimpleDateFormat;
import java.util.*;
import java.util.zip.ZipEntry;
import java.util.zip.ZipInputStream;
import java.util.zip.ZipOutputStream;
import xwchclientapi.XWCHClient;
import xwchclientapi.util.JobStatsEnumType;
import xwchclientapi.util.PlateformEnumType;

public class XWCHHandler extends GridHandler
{
  private static final String FSEP = File.separator;
  private static final String XWCH_STDOUT_FILENAME = "xwch_stdout.log";
  private static final String XWCH_STDERR_FILENAME = "xwch_stderr.log";

  protected XWCHClient c;

  // Checklist used by checkConfiguration()
  private static final String[] configurationChecklist =
  {
    "client_id", "server_address", "base_path"
  };
  private String CFG_XWCH_CLIENT_ID;
  private String CFG_XWCH_SERVER_ADDRESS;
  private String CFG_XWCH_BASE_PATH;

  // It is needed to keep the list of alive jobs.
  // This way, we keep a list of jobs for each application.
  // When the last job of an application finished, the application is deleted.
  private HashMap<String, ArrayList<String>> aliveJobs =
      new HashMap<String, ArrayList<String>>();

  // Maps Job (grid) ID to XtremWeb-CH Module ID
  private HashMap<String, String> jobsModules = new HashMap<String, String>();

  // Suffix for the logs messages
  private String logSuffix;
  private File   basePath;
  
  // Mapping between 3g-bridge statuses and xwch statuses
  private static final Map<JobStatsEnumType, Integer> statusRelations =
               new HashMap<JobStatsEnumType, Integer>();

  static
  {
    statusRelations.put (JobStatsEnumType.COMPLETE,   new Integer(Job.FINISHED));
    statusRelations.put (JobStatsEnumType.KILLED,     new Integer(Job.ERROR));
    statusRelations.put (JobStatsEnumType.PROCESSING, new Integer(Job.RUNNING));
    statusRelations.put (JobStatsEnumType.READY,      new Integer(Job.RUNNING));
    statusRelations.put (JobStatsEnumType.WAITING,    new Integer(Job.RUNNING));
  }

  private void LogDebug (String line)
  { // FIXME method's parameters validation (not null, ...)
    Logger.logit (LogLevel.DEBUG, logSuffix + line);
  }

  private void LogInfo (String line)
  { // FIXME method's parameters validation (not null, ...)
    Logger.logit (LogLevel.INFO, logSuffix + line);
  }

  private void LogError (String line)
  { // FIXME method's parameters validation (not null, ...)
    Logger.logit (LogLevel.ERROR, logSuffix + line);
  }

  /**
   * Initializes the AbstractGridHandler superclass.
   */
  public XWCHHandler (String instance) throws RuntimeBridgeException, Exception
  { // FIXME method's parameters validation (not null, ...)
    super (instance);
    setPolling (true);

    logSuffix = "XWCH plugin (" + pluginInstance + "): ";

    // Load current configuration
    CFG_XWCH_CLIENT_ID      = getConfig ("client_id");
    CFG_XWCH_SERVER_ADDRESS = getConfig ("server_address");
    CFG_XWCH_BASE_PATH      = getConfig ("base_path");

    LogInfo ("Current configuration is \n" +
             "CFG_XWCH_CLIENT_ID      : " + CFG_XWCH_CLIENT_ID      +"\n"+
             "CFG_XWCH_SERVER_ADDRESS : " + CFG_XWCH_SERVER_ADDRESS +"\n"+
             "CFG_XWCH_BASE_PATH      : " + CFG_XWCH_BASE_PATH      +"\n");

    // Create base directory for input/output files.
    // There will be a directory for each job inside this directory.
    basePath = new File (CFG_XWCH_BASE_PATH);
    basePath.mkdirs();
    basePath.deleteOnExit();
    CFG_XWCH_BASE_PATH = basePath.getAbsolutePath() + FSEP;

    LogInfo ("Base directory created " + basePath.getAbsolutePath());
    try
    {
      checkConfiguration();

      c = new XWCHClient (CFG_XWCH_SERVER_ADDRESS,
                          CFG_XWCH_BASE_PATH, CFG_XWCH_CLIENT_ID);
      if (!c.Init())
      {
        LogError ("Cannot initialize client with the following parameters:" +
                  "\nServer endpoint: " + CFG_XWCH_SERVER_ADDRESS +
                  "\nData folder: "     + CFG_XWCH_BASE_PATH      +
                  "\nClient ID: "       + CFG_XWCH_CLIENT_ID      +
                  " \nexit");
        throw new Exception();
      }

      LogInfo ("Warehouse is " + (c.PingWarehouse() ? "":"NOT ") + "recheable");
      LogInfo ("Plugin ready for usage.");
    }
    catch (RuntimeBridgeException e)
    {
      LogError ("Runtine exception occured: " + e.getMessage());
      throw e;
    }
  }

  /**
   * Submits the list of jobs to the target XWCH system.
   * @param jobs list of jobs requested for submit
   * @throws RuntimeBridgeException
   */
  public void submitJobs (ArrayList<Job> jobs) throws RuntimeBridgeException
  { // FIXME method's parameters validation (not null, ...)
    LogDebug ("submitJobs (" + jobs.toString() + ")");

    if (jobs.isEmpty()) return;

    // A new application is created in each call of this method.
    String xwchAppName = "3G-Bridge_" + getDateTime();
    String xwchAppId   = null;
    try
    {
      xwchAppId = c.AddApplication (xwchAppName);
    }
    catch (Exception ex)
    {
      LogError ("Failed to create application: " + ex.getMessage());
    }

    if (xwchAppId != null)
    {
      aliveJobs.put (xwchAppId, new ArrayList<String>());
      int cpt = 0;
      for (Job job : jobs)
      {
        try
        {
          String xwchModuleId = c.AddModule (job.getId());
          xwchclientapi.XWCHClient.fileref inputRef = null;

          // Create a directory for the job files.
          // The name of the directory will be the job id.
          File jobPath = new File (CFG_XWCH_BASE_PATH + job.getId());
          jobPath.mkdir();
          jobPath.deleteOnExit();

          String binaryZipRelPath = job.getId()+FSEP+"binary.zip";
          String inputsZipRelPath = job.getId()+FSEP+"inputs.zip";
          String binaryZipAbsPath = jobPath.getAbsolutePath()+FSEP+"binary.zip";
          String inputsZipAbsPath = jobPath.getAbsolutePath()+FSEP+"inputs.zip";


          if (prepareBinaryFile (job, binaryZipAbsPath))
          {
            LogDebug ("Executing c.AddModuleApplication (" + xwchModuleId + ", "
                      + binaryZipRelPath + ", PlateformEnumType.LINUX_x86_32)");
            c.AddModuleApplication (xwchModuleId, binaryZipRelPath,
                                    PlateformEnumType.LINUX_x86_32);
          }

          if (prepareInputFiles (job, inputsZipAbsPath))
          {
            LogDebug ("Executing c.AddData (" + inputsZipRelPath + ")");
            inputRef = c.AddData (inputsZipRelPath);
          }

          // Create a list containing output files names according to the job
          String listOutputFiles = "";
          HashMap<String, String> outputs = job.getOutputs();
          for (String outputName : outputs.keySet())
          {
            listOutputFiles += outputName + ";";
          }

          String processCmdLine = job.getName() + " " + job.getArgs();
          // listOutputFiles = "regexp:.*"; // FIXME output files: match all

          String xwchJobId = null;
          String outputFilename = xwchAppId + "_" + (cpt++);
          LogDebug ("executing c.AddJob (" + job.getId()               + ", " +
                                             xwchAppId                 + ", " +
                                             xwchModuleId              + ", " +
                                             processCmdLine            + ", " +
                                             inputRef.toJobReference() + ", " +
                                             listOutputFiles           + ", " +
                                             outputFilename + ", \"\");");

          xwchJobId = c.AddJob (job.getId(), xwchAppId, xwchModuleId,
                                processCmdLine, inputRef.toJobReference(),
                                listOutputFiles, outputFilename, "");

          // The grid ID is set to be "xwchJobId:xwchAppId:outputFilename"
          // This way there is no need to keep a map with this information,
          // and it is also kept in the db.
          job.setGridId (xwchJobId + ":" + xwchAppId + ":" + outputFilename);
          job.setStatus (Job.RUNNING);
          aliveJobs.get (xwchAppId).add (xwchJobId);
          jobsModules.put (job.getId(), xwchModuleId);
        }
        catch (Exception e)
        {
          LogError ("Failed to submit job:" + e.getMessage());
          job.setStatus (Job.ERROR);
        }
      }
    } else
    {
      LogError ("Failed to create application: " +
                "Failed to create XWCH application.");
      for (Job job : jobs) { job.setStatus (Job.ERROR); }
    }
  }

  /**
   * According to the local status of the parameter job it asks the actual
   * status of the job from XWCH or requests the termination of the job.
   * @param job
   * @throws RuntimeBridgeException
   */
  public void poll (Job job) throws RuntimeBridgeException
  { // FIXME method's parameters validation (not null, ...)
    LogDebug ("poll (" + job.getId() + ")");

    LogInfo ("Status of job " + job.getId() + " is " + job.getStatus());
    int status = job.getStatus();
    if      (status == Job.RUNNING) updateJob (job);
    else if (status == Job.CANCEL)  cancelJob (job);

    // Remark : We must use actual job status !
    if (job.getStatus() > Job.RUNNING) cleanJob (job);
  }

  /**
   * Asks the actual status of the parameter job from XWCH and updates the local
   * status of the job according to the answer.
   * @param job
   * @throws RuntimeBridgeException
   */
  private void updateJob (Job job) throws RuntimeBridgeException
  { // FIXME method's parameters validation (not null, ...)
    LogDebug ("updateJob (" + job.getId() + ")");
    LogInfo  ("Updating job status for job " + job.getId() + " (GridId is " +
              job.getGridId() + ")");

    String           xwchJobId      = null;
    String           xwchAppId      = null;
    String           outputFilename = null;
    JobStatsEnumType xwchJobStatus  = null;
    try
    {
      xwchJobId      = job.getGridId().split(":")[0];
      xwchAppId      = job.getGridId().split(":")[1];
      outputFilename = job.getGridId().split(":")[2];
      xwchJobStatus  = c.GetJobStatus (xwchJobId);
    }
    catch (Exception e)
    {
      LogError ("Unable to update job " + job.getId() + ": " + getException(e));
    }

    LogInfo ("Status of job " + job.getId() + " is " + job.getStatus() + "/" +
             xwchJobStatus);

    if (xwchJobStatus == null)
    {
      job.setStatus (Job.ERROR);
    }
    else if (xwchJobStatus == JobStatsEnumType.COMPLETE)
    {
      try
      {
        c.GetJobResult (xwchJobId, outputFilename);
        File outputFile = new File (CFG_XWCH_BASE_PATH + outputFilename);
        File jobPath    = new File (CFG_XWCH_BASE_PATH + job.getId());

        LogInfo ("Extracting " + outputFile.getAbsolutePath() +
                 " into "      + jobPath.getAbsolutePath());

        decompressZipFile (outputFile, jobPath, true);

        String                  outputPath = null;
        HashMap<String, String> outputs    = job.getOutputs();

        for (String outputName : outputs.keySet())
        {
          outputPath = outputs.get (outputName);
          copyFile (jobPath.getAbsolutePath() + FSEP + outputName, outputPath);

          LogInfo ("Copying output file '"+ outputName + "' from " +
                   jobPath.getAbsolutePath() + " to " + outputPath);
        }

        if (haveOutputFilesArrived (job))
        {
          LogInfo ("Job " + job.getId() + " successful !");
          job.setStatus (Job.FINISHED);
        } else
        {
          LogInfo ("Job " + job.getId() + " unsucessful !");
          job.setStatus (Job.ERROR);
        }

        // Allow apache daemon to read output files by changing file's ownership
        // FIXME chown of the output path of the job instead of the whole work path !
        RecursiveChown (new File (CFG_XWCH_BASE_PATH), "www-data:www-data");

        aliveJobs.get (xwchAppId).remove (xwchJobId);
        // if this is the last job alive for this application -> end application
        if (aliveJobs.get (xwchAppId).isEmpty())
        {
          LogInfo ("Application " + xwchAppId + " ended");
          c.EndApplication (xwchAppId + ";true");
          aliveJobs.remove (xwchAppId);
        }
      }
      catch (Exception e)
      {
        LogError ("Error getting results for job " + job.getId() + ": " +
                  getException (e));
        job.setStatus (Job.ERROR);
      }
    }
    else
    {
      job.setStatus (statusRelations.get (xwchJobStatus));
    }
  }

  /**
   * Asks XWCH to abort the parameter job.
   * @param job
   */
  private void cancelJob (Job job)
  {
    LogDebug ("cancelJob (" + job.getId() + ")");
    LogInfo  ("About to cancel job " + job.getId());

    String xwchAppId = null;
    try
    {
      xwchAppId = job.getGridId().split(":")[1];
      c.KillJob (job.getGridId(), xwchAppId);
      job.deleteJob();

      LogDebug ("Job " + job.getId() + " deleted");
    }
    catch (Exception e)
    {
      LogError ("Unable to cancel job " + job.getId() + ": " + e.getMessage());
    }
  }

  /**
   * Remove job's working directory and remove job's module from XWCH
   * @param job
   */
  private void cleanJob (Job job)
  { // FIXME method's parameters validation (not null, ...)
    LogDebug ("cleanJob (" + job.getId() + ")");
    LogDebug ("Removing job directory for job " + job.getId());
    RecursiveRemove (new File (CFG_XWCH_BASE_PATH + job.getId()));
    String xwchModuleId = jobsModules.get (job.getId());
    c.RemoveModule     (job.getId());
    jobsModules.remove (job.getId());
  }

  /**
   * Prepares a zip file with the binary file of a given job
   * @param job
   * @return String with the name of zip file
   */
  private boolean prepareBinaryFile (Job job, String dstFilename)
  { // FIXME method's parameters validation (not null, ...)
    LogDebug ("prepareBinaryFile (" + job.getId() + ", " + dstFilename + ")");
    LogInfo  ("Preparing binary file of job " + job.getId());

    List<String>             inputsPathsList = new ArrayList<String>();
    HashMap<String, FileRef> inputs          = job.getInputs();
    FileRef                  binary          = inputs.get (job.getName());

    String binaryPath = job.getName();
    // FIXME if the name of the binary != job.getName() -> BUG
    LogInfo ("Downloading binary file '" + job.getName() + "'");

    wget (binary.getURL(), binaryPath);
    if (!checkDownloadedFile (binaryPath, binary))
    {
      LogError ("Error downloading '" + job.getName() + "' in job " +
                job.getId());
      return false;
    }

    inputsPathsList.add (binaryPath);

    return createZipFile (inputsPathsList, dstFilename);
  }
  
  /**
   * Prepares a zip file with the input files of a given job
   * @param job
   * @param outFileName
   * @return Boolean if files have been downloaded and zip succesfully created
   */
  private boolean prepareInputFiles (Job job, String dstFilename)
  { // FIXME method's parameters validation (not null, ...)
    LogDebug ("prepareInputFiles (" + job.getId() + ", " + dstFilename + ")");
    LogInfo  ("Preparing input files of job " + job.getId());

    List<String> inputsPathsList = new ArrayList<String>();

    HashMap<String, FileRef> inputs = job.getInputs();

    for (String inputName : inputs.keySet())
    {
      String inputPath = inputName;
      if (!inputName.equals (job.getName()))
      {
        FileRef inputRef = inputs.get (inputName);
        wget (inputRef.getURL(), inputPath);

        LogInfo ("Downloading input file '" + inputName + "'");

        if (!checkDownloadedFile (inputPath, inputRef))
        {
          LogError ("Error downloading '" + inputName + "' in job " +
                    job.getId());
          return false;
        }

        inputsPathsList.add (inputPath);
      }
    }

    return createZipFile (inputsPathsList, dstFilename);
  }

  /**
   * Checks if all of the requested output files have arrived
   * @param job
   */
  private boolean haveOutputFilesArrived (Job job)
  { // FIXME method's parameters validation (not null, ...)
    LogDebug ("haveOutputFilesArrived (" + job.getId() + ")");
    LogInfo  ("Checking output files of job " + job.getId());

    HashMap<String, String> outputs = job.getOutputs();
    for (String outputName : outputs.keySet())
    {
      String outputPath = outputs.get (outputName);
      File   file       = new File (outputPath);

      LogDebug ("Output file '" + outputName + "' does exist ? " +
                (file.exists() ? "yes" : "no"));

      if (!file.exists()) return false;
    }
    return true;
  }

  private boolean createZipFile (List<String> srcFilenames, String dstFilename)
  { // FIXME method's parameters validation (not null, ...)
    LogDebug ("createZipFile (" + srcFilenames.toString() + ", " +
              dstFilename + ")");

    // Create a buffer for reading the files
    byte[] buf = new byte[1024];
    try
    {
      // Create the ZIP file
      ZipOutputStream zipStream =
        new ZipOutputStream (new FileOutputStream (dstFilename));

      // Compress the files
      for (int i = 0; i < srcFilenames.size(); i++)
      {
        FileInputStream srcStream = new FileInputStream (srcFilenames.get (i));

        // Add ZIP entry to output stream.
        File srcFile = new File (srcFilenames.get (i));
        zipStream.putNextEntry (new ZipEntry (srcFile.getName()));

        // Transfer bytes from the file to the ZIP file
        int len;
        while ((len = srcStream.read (buf)) > 0)
        {
          zipStream.write (buf, 0, len);
        }

        // Complete the entry
        zipStream.closeEntry();
        srcStream.close     ();
      } // Complete the ZIP file
      zipStream.close();
      return true;
    }
    catch (IOException e)
    {
      LogError ("Unable to create zip file: " + e.getMessage());
      return false;
    }
  }

  public void decompressZipFile (final File zipFile, final File dstPath,
                                 final boolean deleteZipAfter)
     throws IOException
  { // FIXME method's parameters validation (not null, ...)
    LogDebug ("decompressZipFile (" + zipFile.getAbsolutePath() + ", " +
                                      dstPath.getPath()         + ", " +
                                     (deleteZipAfter ? "yes" : "no")  + ")");

    final ZipInputStream zipStream = new ZipInputStream (new BufferedInputStream
                                   (new FileInputStream (zipFile)));

    ZipEntry zipEntry;
    try
    {
      // Parcourt tous les fichiers
      while ((zipEntry = zipStream.getNextEntry()) != null)
      {
        final File f = new File (dstPath.getCanonicalPath(),zipEntry.getName());
        if (f.exists()) { f.delete(); }

        // Creation of directories
        if (zipEntry.isDirectory())
        {
          f.mkdirs();
          continue;
        }
        f.getParentFile().mkdirs();
        final OutputStream fos =
                new BufferedOutputStream (new FileOutputStream (f));

        // Writing files
        try
        {
          try
          {
            final byte[] buf = new byte[8192];
            int bytesRead;
            while (-1 != (bytesRead = zipStream.read (buf)))
            {
              fos.write (buf, 0, bytesRead);
            }
          }
          finally { fos.close(); }
        }
        catch (final IOException ioe)
        {
          f.delete();
          throw ioe;
        }
      }
    } finally { zipStream.close(); }

    if (deleteZipAfter) zipFile.delete();
  }

  private void RecursiveRemove (File path)
  { // FIXME method's parameters validation (not null, ...)
    if (path.isDirectory())
    {
      String[] files = path.list();
      for (int i = 0; i < files.length; i++)
      {
        RecursiveRemove (new File (path + File.separator + files[i]));
      }
    }
    path.delete();
  }

  private void RecursiveChown (File path, String owner)
  {
    try
    {
      String command =
        "chown " + owner + " " + path.getAbsolutePath() + " -R";
      LogDebug ("RecursiveChown : " + command);
      Runtime r = Runtime.getRuntime();
      Process p = r.exec (command);
      //p.waitfor();
    }
    catch (IOException ex)
    {
      LogError ("RecursiveChown (" + path + "): " + ex.getMessage());
    }
  }

  private void wget (String srcUrlString, String dstFilename)
  { // FIXME method's parameters validation (not null, ...)
    try
    {
      URL url = new URL (srcUrlString);
      ReadableByteChannel rbc = Channels.newChannel (url.openStream());
      FileOutputStream    fos = new FileOutputStream (dstFilename);
      fos.getChannel().transferFrom (rbc, 0, 1 << 24);
    }
    catch (IOException ex)
    {
      LogError ("wget ("+srcUrlString+", "+dstFilename+"): " + ex.getMessage());
    }
  }

  private void checkConfiguration()
  { // FIXME method's parameters validation (not null, ...)
    String errorMsg = "";
    for (String propertyName : configurationChecklist)
    {
      String value = getConfig (propertyName);
      if (value == null || value.trim().equals (""))
      {
        errorMsg += " " + propertyName;
      }
    }
    if (errorMsg.length() > 0)
    {
      errorMsg = "Could not initialize XWCH plugin! The following properties " +
                 "are missing or empty:" + errorMsg;
      throw new RuntimeBridgeException (errorMsg);
    }
  }

  private String getDateTime()
  { // FIXME method's parameters validation (not null, ...)
    DateFormat dateFormat = new SimpleDateFormat ("yyyy-MM-dd_HH:mm:ss");
    Date       date       = new Date();
    return dateFormat.format (date);
  }

  private boolean checkDownloadedFile (String inputFile, FileRef inputRef)
  { // FIXME method's parameters validation (not null, ...)
    inputRef.getMD5();
    md5verification md5      = new md5verification();
    String          checksum = md5.createChecksum (inputFile);
    return true; // FIXME this is for debugging purpose, please remove on prod.
    // return (checksum == null ? inputRef.getMD5() == null :
    //                            checksum.equals (inputRef.getMD5()));
  }

  private void copyFile (String srcFile, String dstFile)
  { // FIXME method's parameters validation (not null, ...)
    try
    {
      File         f1  = new File (srcFile);
      File         f2  = new File (dstFile);
      InputStream  in  = new FileInputStream  (f1);
      OutputStream out = new FileOutputStream (f2);

      byte[] buf = new byte[1024];
      int len;
      while ((len = in.read(buf)) > 0) { out.write (buf, 0, len); }
      in. close();
      out.close();
    }
    catch (FileNotFoundException ex)
    {
      LogError ("copyFile("+srcFile+", "+dstFile+"): " + ex.getMessage());
    }
    catch (IOException e)
    {
      LogError ("copyFile("+srcFile+", "+dstFile+"): " + e.getMessage());
    }
  }

  @Override
  public void updateStatus()
  {
    throw new UnsupportedOperationException ("Not supported yet.");
  }

  /**
   * Returns the stack trace of an exception as a string
   * @param e
   */
  private static String getException (Exception e)
  {
    // FIXME method's parameters validation (not null, ...)
    StringWriter w = new StringWriter();
    e.printStackTrace (new PrintWriter (w));
    return w.toString();
  }
}
