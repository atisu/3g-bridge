<?php

require_once('error.inc.php');

class DB {
	private $db_link = Null;
	private static $inst = Null;

	public function instance($config_file = Null) {
		if (DB::$inst == Null) {
			if ($config_file == Null)
				throw new Exception('DB handler is not initialized');
			DB::$inst = new DB($config_file);
		}
		return DB::$inst;
	}

	private function __construct($config_file) {
		$cfg = parse_ini_file($config_file, true, INI_SCANNER_RAW);
		
		$this->db=$cfg['database'];
		if (!($this->db_link = mysql_connect($this->db['host'],
						     $this->db['user'],
						     $this->db['password'])))
			$this->derr();
		
		if (!mysql_select_db($this->db['name']))
			$this->derr();
	}
	public function __destruct() {
		$this->free();
	}
	private function free() {
		try {
			if ($this->db_link)
				mysql_close($this->db_link);
		}
		catch (Exception $ex) {}
	}
	public static function derr() {
		$exc = new DBError("Database error: " . mysql_error());
		if (DB::$inst)
			DB::$inst=Null;
		throw $exc;
	}
}
?>
