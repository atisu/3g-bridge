/**
 *
 * @author jaime
 */
package hu.sztaki.lpds.G3Bridge.plugin.xwch;

import java.io.*;

import java.util.ArrayList;
import java.util.HashMap;

import java.util.Date;
import java.text.DateFormat;
import java.text.SimpleDateFormat;
import hu.sztaki.lpds.G3Bridge.*;

import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.net.URL;
import java.nio.channels.Channels;
import java.nio.channels.ReadableByteChannel;
import java.util.List;
import java.util.Map;
import java.util.zip.ZipEntry;
import java.util.zip.ZipInputStream;
import java.util.zip.ZipOutputStream;
import xwchclientapi.XWCHClient;
import xwchclientapi.util.JobStatsEnumType;
import xwchclientapi.util.PlateformEnumType;

public class XWCHHandler extends GridHandler
{
  protected static final String XWCH_STDOUT_FILENAME = "xwch_stdout.log";
  protected static final String XWCH_STDERR_FILENAME = "xwch_stderr.log";
  
  protected XWCHClient c;
  private static final Map<String, String> propertyNames =
               new HashMap<String, String>();
   
  // It is needed to keep the list of alive jobs.
  // This way, we keep a list of jobs for each application.
  // When the last job of an application finished, the application is deleted.
  private HashMap<String, ArrayList<String>> aliveJobs =
      new HashMap<String, ArrayList<String>>();
  
  // Suffix for the logs messages
  private String logSuffix;

  private File tmp;
  
  static
  {
    // name of the requested property keys:
    propertyNames.put ("XWCH_CLIENT_ID",          "client_id");
    propertyNames.put ("XWCH_SERVER_ADDRESS",     "server_address");
    propertyNames.put ("XWCH_TMP_DIRECTORY_PATH", "tmp_directory_path");

  }
  // mapping between 3g-bridge statuses and xwch statuses
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

  /**
   * Initializes the AbstractGridHandler superclass.
   */
  public XWCHHandler (String instance) throws RuntimeBridgeException, Exception
  {
    super (instance);
    setPolling (true);
    
    logSuffix = "XWCH plugin (" + pluginInstance + "): ";

    // Create temporary directory for input/output files.
    // There will be a directory for each job inside this directory.
    tmp = new File (getConfig (propertyNames.get ("XWCH_TMP_DIRECTORY_PATH")));
    tmp.mkdirs();
    tmp.deleteOnExit();

    Logger.logit (LogLevel.INFO, logSuffix +
                  "Temporary directory created " + tmp.getAbsolutePath());
    try
    {
      checkProperties();

      c = new XWCHClient (getConfig (propertyNames.get ("XWCH_SERVER_ADDRESS")),
                     "/", getConfig (propertyNames.get ("XWCH_CLIENT_ID")));
      if (!c.Init())
      {
        Logger.logit
          (LogLevel.ERROR,
           "Cannot initialize client with the following parameters:" +
           "\nServer address: " + propertyNames.get("XWCH_SERVER_ADDRESS") +
           "\nClient ID: "      + propertyNames.get("XWCH_CLIENT_ID") +
           " \nexit");
        throw new Exception();
      }

      Logger.logit (LogLevel.INFO, logSuffix +
                    "Is warehouse reachable ? " + c.PingWarehouse());
      
      Logger.logit (LogLevel.INFO, logSuffix + "Plugin ready for usage.");
    }
    catch (RuntimeBridgeException e)
    {
      Logger.logit (LogLevel.ERROR, logSuffix +
                    "Runtine exception occured: " + e.getMessage());
      throw e;
    }
  }

  /**
   * Submits the list of jobs to the target XWCH system.
   * @param jobs list of jobs requested for submit
   * @throws RuntimeBridgeException
   */
  public void submitJobs (ArrayList<Job> jobs) throws RuntimeBridgeException
  {
    if (jobs.isEmpty()) return;

    // A new application is created in each call of this method.
    String appName = "3G-Bridge_" + getDateTime();
    String appId   = null;
    try
    {
      appId = c.AddApplication (appName);
    }
    catch (Exception ex)
    {
      Logger.logit (LogLevel.ERROR, logSuffix +
                    "Failed to create application: " + ex.getMessage());
    }

    if (appId != null)
    {
      aliveJobs.put (appId, new ArrayList<String>());
      for (Job job : jobs)
      {
        try
        {
          String fileGeneratorModuleId = c.AddModule (job.getId());
          xwchclientapi.XWCHClient.fileref inputRef = null;

          // Create a directory for the job files.
          // The name of the directory will be the job id.
          File jobDir = new File (tmp.getAbsolutePath() + "/" + job.getId());
          jobDir.mkdir();
          jobDir.deleteOnExit();

          String zipPath       = jobDir.getAbsolutePath() + "/" + job.getId();
          String inputZip      = zipPath + "_inputs.zip";
          String binaryFileZip = zipPath + "_binary.zip";

          if (prepareBinaryFile (job, binaryFileZip))
          {
            c.AddModuleApplication (fileGeneratorModuleId, binaryFileZip,
                                    PlateformEnumType.LINUX_x86_32);
          }

          if (prepareInputFiles (job, inputZip))
          {
            inputRef = c.AddData (inputZip);
          }

          // Alternative line, depending on how the jobs are sent.
          // job.getName() contains the binary name.
          // If job.getArgs() looks like "binary_name arg1 arg2 arg3", use the commented one.
          // If job.getArgs() looks like "arg1 arg2 arg3" use the enabled one.

          // String processCmdLine = job.getArgs();
          String processCmdLine = job.getName() + " " + job.getArgs();

          String listOutputFiles = "";
          HashMap<String, String> outputs = job.getOutputs();
          
          for (String outputName : outputs.keySet())
          {
            listOutputFiles += outputName + ";";
          }

          String jobId = null;
          try
          {
            jobId = c.AddJob (job.getId(), appId, fileGeneratorModuleId,
                              processCmdLine, inputRef.toJobReference(),
                              listOutputFiles, job.getId(), "");
          }
          catch (Exception e)
          {
            Logger.logit (LogLevel.ERROR, logSuffix +
                          "Failed to submit job:" + e.getMessage());
          }

          try
          {
            // The grid ID is set to be "jobId:appId"
            // This way there is no need to keep a map with this information,
            // and it is also kept in the db.
            job.setGridId (jobId + ":" + appId);
          }
          catch (Exception e)
          {
            Logger.logit (LogLevel.ERROR, logSuffix + e.getMessage());
          }
          job.setStatus (Job.RUNNING);
          aliveJobs.get (appId).add (jobId);
        }
        catch (Exception e)
        {
          Logger.logit (LogLevel.ERROR, logSuffix +
                        "Failed to submit job:" + e.getMessage());
          job.setStatus (Job.ERROR);
        }
      }
    } else
    {
      Logger.logit (LogLevel.ERROR, logSuffix +
                    "Failed to create application: " +
                    "Failed to create XWCH application.");
      for (Job job : jobs) { job.setStatus (Job.ERROR); }
    }
  }

  /**
   * According to the local status of the parameter job it asks the actual status
   * of the job from XWCH or requests the termination of the job.
   * @param job
   * @throws RuntimeBridgeException
   */
  public void poll (Job job) throws RuntimeBridgeException
  {
    int status = job.getStatus();
    if (status == Job.RUNNING) updateJob (job);
    // In case the bridge has cancelled the job, xwch also cancels the job.
    else if (status == Job.CANCEL) cancelJob (job);
  }

  /**
   * Prepares a zip file with the input files of a given job
   * @param job
   * @param outFileName
   * @return Boolean if the files have been downloaded and the zip created succesfully
   */
  private boolean prepareInputFiles (Job job, String outFileName)
  {
    Logger.logit (LogLevel.INFO, logSuffix + "Preparing input files of job " + job.getId());
    List<String> list_of_input_paths = new ArrayList<String>();

    HashMap<String, FileRef> inputs = job.getInputs();

    for (String inputName : inputs.keySet())
    {
      String inputPath =
        tmp.getAbsolutePath() + "/" + job.getId() + "/" + inputName;

      if (!inputName.equals (job.getName()))
      {
        FileRef inputRef = inputs.get (inputName);
        wget (inputRef.getURL(), inputPath);

        if (!checkDownloadedFile (inputPath, inputRef))
        {
          Logger.logit (LogLevel.ERROR, logSuffix +
                        "Error downloading '" + inputName +
                        "' in job "           + job.getId());
          return false;
        }

        list_of_input_paths.add (inputPath);
      }
    }

    return createZipFile (list_of_input_paths, outFileName);
  }

  /**
   * Prepares a zip file with the binary file of a given job
   * @param job
   * @return String with the name of zip file
   */
  private boolean prepareBinaryFile (Job job, String outFileName)
  {
    Logger.logit (LogLevel.INFO, logSuffix +
                  "Preparing binary file of job " + job.getId());
    
    List<String>             list_of_input_paths = new ArrayList<String>();
    HashMap<String, FileRef> inputs              = job.getInputs();
    FileRef                  binary              = inputs.get (job.getName());
    
    String binaryPath =
      tmp.getAbsolutePath() + "/" + job.getId() + "/" + job.getName();

    wget (binary.getURL(), binaryPath);
    if (!checkDownloadedFile(binaryPath, binary))
    {
      Logger.logit (LogLevel.ERROR, logSuffix +
                    "Error downloading '" + job.getName() +
                    "' in job "           + job.getId());
      return false;
    }

    list_of_input_paths.add (binaryPath);

    return createZipFile (list_of_input_paths, outFileName);
  }

  /**
   * Checks if all of the requested output files have arrived
   * @param job
   */
  private boolean haveOutputFilesArrived (Job job)
  {
    Logger.logit (LogLevel.INFO, logSuffix +
                  "Checking output files of job " + job.getId());
    
    HashMap<String, String> outputs = job.getOutputs();
    for (String outputName : outputs.keySet())
    {
      String outputPath = outputs.get (outputName);
      File   file       = new File (outputPath);
      if (!file.exists()) return false;
    }
    return true;
  }

  /**
   * Asks XWCH to abort the parameter job.
   * @param job
   */
  private void cancelJob (Job job)
  {
    Logger.logit (LogLevel.INFO, logSuffix + "About to cancel job " +
                  job.getId());
    
    String appId = job.getGridId().split(":")[1];
    try
    {
      c.KillJob (job.getGridId(), appId);
      job.deleteJob();
    }
    catch (Exception e)
    {
      Logger.logit (LogLevel.ERROR, logSuffix + "Unable to cancel job " +
                    job.getId() + ": " + e.getMessage());
    }
  }

  /**
   * Asks the actual status of the parameter job from XWCH and updates the local
   * status of the job according to the answer.
   * @param job
   * @throws RuntimeBridgeException
   */
  private void updateJob (Job job) throws RuntimeBridgeException
  {
    JobStatsEnumType jobStatusXWCH = null;
    try
    {
      jobStatusXWCH = c.GetJobStatus (job.getGridId().split(":")[0]);
    }
    catch (Exception ex)
    {
      Logger.logit (LogLevel.ERROR, logSuffix + ex.getMessage());
    }
    Logger.logit (LogLevel.INFO, logSuffix +
                  "Status of job " + job.getId() + " is: " + jobStatusXWCH);

    if (jobStatusXWCH == null)
    {
      job.setStatus (Job.ERROR);
    }
    else if (jobStatusXWCH == JobStatsEnumType.COMPLETE)
    {
      try
      {
        //job id was used as the name for the output zip file
        String appId = job.getGridId().split(":")[1];
        c.GetJobResult (appId, job.getId());
        File file   = new File (job.getId());
        File jobDir = new File (tmp.getAbsolutePath() + "/" + job.getId());

        Logger.logit (LogLevel.INFO,
                      "Extracting " + file.getAbsolutePath() +
                      " into "      + jobDir.getAbsolutePath());
        
        decompressZipFile (file, jobDir, true);

        String                  outputPath = null;
        HashMap<String, String> outputs    = job.getOutputs();

        for (String outputName : outputs.keySet())
        {
          outputPath = outputs.get(outputName);
          copyFile (jobDir.getAbsolutePath() + '/' + outputName, outputPath);
        }
 
        if (haveOutputFilesArrived (job))
        {  
          job.setStatus (Job.FINISHED);
        } else
        {
          job.setStatus (Job.ERROR);
        }

        aliveJobs.get (appId).remove (job.getId());
        // if this is the last job alive for this application -> end the application
        if (aliveJobs.get(appId).isEmpty())
        {
          c.EndApplication (appId);
          aliveJobs.remove (appId);
        }
      }
      catch (Exception ex)
      {
        Logger.logit (LogLevel.ERROR,
                      logSuffix + "Error getting results. " + ex.getMessage());
        job.setStatus (Job.ERROR);
      }
    }
    else
    {
      job.setStatus (statusRelations.get (jobStatusXWCH));
    }
  }

  private boolean createZipFile (List<String> filenames, String outFilename)
  {
    // Create a buffer for reading the files
    byte[] buf = new byte[1024];
    try
    {
      // Create the ZIP file
      ZipOutputStream out =
        new ZipOutputStream (new FileOutputStream (outFilename));
      
      // Compress the files
      for (int i = 0; i < filenames.size(); i++)
      {
        FileInputStream in = new FileInputStream (filenames.get (i));
        
        // Add ZIP entry to output stream.
        File f = new File (filenames.get (i));
        out.putNextEntry (new ZipEntry (f.getName()));
        
        // Transfer bytes from the file to the ZIP file
        int len;
        while ((len = in.read (buf)) > 0) { out.write (buf, 0, len); }
        
        // Complete the entry
        out.closeEntry();
        in. close     ();
      } // Complete the ZIP file
      out.close();
      return true;
    }
    catch (IOException e)
    {
      Logger.logit (LogLevel.ERROR, logSuffix + e.getMessage());
      return false;
    }
  }

  public void decompressZipFile (final File file, final File folder,
                                 final boolean deleteZipAfter)
     throws IOException
  {
    final ZipInputStream zis =
      new ZipInputStream (new BufferedInputStream
     (new FileInputStream (file.getCanonicalFile())));
    
    ZipEntry ze;
    try
    {
      // Parcourt tous les fichiers
      while (null != (ze = zis.getNextEntry()))
      {
        final File f = new File (folder.getCanonicalPath(), ze.getName());
        if (f.exists()) { f.delete(); }
          
        // Creation of directories
        if (ze.isDirectory())
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
            while (-1 != (bytesRead = zis.read (buf)))
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
    } finally { zis.close(); }
    
    if (deleteZipAfter) file.delete();
  }

  private void wget (String urlString, String name)
  {
    try
    {
      URL url = new URL (urlString);
      ReadableByteChannel rbc = Channels.newChannel (url.openStream());
      FileOutputStream    fos = new FileOutputStream (name);
      fos.getChannel().transferFrom (rbc, 0, 1 << 24);
    }
    catch (IOException ex)
    {
      Logger.logit (LogLevel.ERROR, logSuffix + ex.getMessage());
    }
  }

  private void checkProperties()
  {
    String errorMsg = "";
    for (String propertyName : propertyNames.values())
    {
      String value = getConfig (propertyName);
      if (value == null || value.trim().equals(""))
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
  {
    DateFormat dateFormat = new SimpleDateFormat ("yyyy-MM-dd_HH:mm:ss");
    Date       date       = new Date();
    return dateFormat.format (date);
  }

  private boolean checkDownloadedFile (String inputFile, FileRef inputRef)
  {
    inputRef.getMD5();
    md5verification md5      = new md5verification();
    String          checksum = md5.createChecksum (inputFile);

    return (checksum == null ? inputRef.getMD5() == null :
                               checksum.equals (inputRef.getMD5()));
    //return true;
  }

  private void copyFile (String sourceFile, String destinationFile)
  {
    try
    {
      File         f1  = new File (sourceFile);
      File         f2  = new File (destinationFile);
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
      Logger.logit (LogLevel.ERROR, logSuffix + ex.getMessage());
    }
    catch (IOException e)
    {
      Logger.logit (LogLevel.ERROR, logSuffix + e.getMessage());
    }
  }

  @Override
  public void updateStatus()
  {
    throw new UnsupportedOperationException ("Not supported yet.");
  }
}
