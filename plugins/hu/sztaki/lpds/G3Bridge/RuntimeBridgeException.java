package hu.sztaki.lpds.G3Bridge;

/**
 * An <code>RuntimeBridgeException</code> is thrown when the JNI glue code detects
 * an error within itself.
 *
 * @author Gábor Gombás
 * @version $Id$
 */

public class RuntimeBridgeException extends RuntimeException {

	/**
	 * Creates a new <code>RuntimeBridgeException</code> with the
	 * given message.
	 *
	 * @param msg		the message of the exception.
	 */
	public RuntimeBridgeException(String msg) {
		super(msg);
	}

	/**
	 * Creates a new <code>RuntimeBridgeException</code> with the
	 * given message and cause.
	 *
	 * @param msg		the message of the exception.
	 * @param cause		the cause of the exception.
	 */
	public RuntimeBridgeException(String msg, Throwable cause) {
		super(msg, cause);
	}

	/**
	 * Creates a new <code>RuntimeBridgeException</code> with the
	 * given cause.
	 *
	 * @param cause		the cause of the exception.
	 */
	public RuntimeBridgeException(Throwable cause) {
		super(cause);
	}
}
