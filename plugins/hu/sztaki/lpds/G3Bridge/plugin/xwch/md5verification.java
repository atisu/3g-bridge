
package hu.sztaki.lpds.G3Bridge.plugin.xwch;

import java.io.FileInputStream;
import java.io.InputStream;
import java.security.MessageDigest;

/**
 * md5verification.
 * @author Boesch
 */
public class md5verification
{
  /**
   * Constructor.
   * Creates a new instance of md5verification */
  public md5verification()
  {
  }

  /**
   * Creates a checksum from a complete reference file.
   * @param filename
   * @return md5 if operation is done. null Otherwise.
   */
  public String createChecksum(String filename)
  {
    try
    {
      InputStream fis = new FileInputStream(filename);

      int           count;
      byte[]        buffer   = new byte[1024];
      MessageDigest complete = MessageDigest.getInstance ("MD5");
      do
      {
        count = fis.read (buffer);
        if (count > 0) complete.update (buffer, 0, count);
      } while (count != -1);
      fis.close();

      byte[] hash = complete.digest();

      StringBuffer hashString = new StringBuffer();
      for (int i = 0; i < hash.length; ++i)
      {
        String hex = Integer.toHexString (hash[i]);
        if (hex.length() == 1)
        {
          hashString.append ('0');
          hashString.append (hex.charAt (hex.length() - 1));
        } else
        {
          hashString.append (hex.substring (hex.length() - 2));
        }
      }
      return hashString.toString();
    } catch (Exception e)
    {
      System.out.println
        ("Error on md5 checksum : " + filename + " E : " +  e.getMessage());
      return null;
    }
  }
}