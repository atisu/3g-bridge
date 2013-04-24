<?php

require_once('error.inc.php');

define('TRACE', 0);
define('DEBUG', 10);
define('INFO', 20);
define('AUDIT', 30);
define('ERROR', 40);
define('CRITICAL', 50);

class Log {
        private static $inst = Null;
        private $file;
        private $level;

        public static function instance($file=Null, $level=Null) {
                if ($file) {
                        if (!$level) $level = 'DEBUG';
                        Log::$inst = new Log($file, $level);
                }
                else {
                        if (Log::$inst == Null)
                                throw new ServerError(
                                        "Logging has not been initialized.");
                }
                return Log::$inst;
        }
        private function __construct($file, $level) {
                $this->file = @fopen($file, 'a');
                if (!$this->file) {
                        $err = error_get_last();
                        throw new ServerError(
                                "Error opening log file: " . $err['message']);
                }
                $this->level = constant($level);
                $this->_log('DEBUG', "Opened log file. Loglevel: '$level'");
        }
        public function __destruct() {
                $this->_log('DEBUG', "Closed log file.");
                fclose($this->file);
        }

        public function _log($level, $what) {
                if (constant($level) < $this->level)
                        return;

                $time = new DateTime();
                $ts = $time->format(DATE_RFC3339);
                $entry = "[$ts]\t$level\t${_SERVER['REMOTE_ADDR']}:${_SERVER['REMOTE_PORT']}\t$what\n";

                if (!@flock($this->file, LOCK_EX)) {
                        $err = error_get_last();
                        throw new ServerError(
                                "Error locking log file: " . $err['message']);
                }

                if (!@fwrite($this->file, $entry))
                {
                        flock($this->file, LOCK_UN);
                        $err = error_get_last();
                        throw new ServerError(
                                "Error opening log file: " . $err['message']);
                }
                flock($this->file, LOCK_UN);
        }

        public static function log($level, $what) {
                Log::instance()->_log($level, $what);
        }
}

?>