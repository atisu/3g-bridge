package hu.sztaki.lpds.G3Bridge;

public final class Logger {
	/**
	 * Sends a log message.
	 *
	 * @param level		the severity of the message.
	 * @param message	the message to send.
	 */
	public static synchronized void logit(LogLevel level, String message) {
		native_logit(level.getCode(), message);
	}

	private static native void native_logit(int level, String message);
}
