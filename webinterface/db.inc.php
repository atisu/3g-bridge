<?php

require_once('error.inc.php');

/*
  Try-finally for mysql result
 */
class ResWrapper {
	public $res;
	public function __construct($result) {
		$this->res = $result;
	}
	public function __destruct() {
		try { mysql_free_result($this->res); }
		catch (Exception $ex) {}
	}
}

/*
  3GBridge related related methods with safe close.
  Needs 3g-bridge.conf to work.
 */
class DB {
	private $db_link = Null;
	private static $inst = Null;

	// Convert string to mysql string literal
	public static function stringify($str, $empty_as_null = TRUE) {
		if ($str===Null or ($empty_as_null and $str==''))
			return 'NULL';
		else
			return '\'' . mysql_real_escape_string($str) . '\'';
	}

	public static function instance($config_file = Null) {
		if (DB::$inst == Null) {
			if ($config_file == Null) { 
				throw new ServerError(
					'DB handler is not initialized');
			}
			DB::$inst = new DB($config_file);
		}
		return DB::$inst;
	}

	private function __construct($cfg) {
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

	public static function begin() {
		DB::q("BEGIN");
	}
	public static function commit() {
		DB::q("COMMIT");
	}
	public static function rollback() {
		DB::q("ROLLBACK");
	}
	public static function q($query) {
		if (!($r = mysql_query($query)))
			DB::derr();
		return $r;
	}

	public static function derr() {
		$exc = new DBError("Database error: " . mysql_error());
		if (DB::$inst)
			DB::$inst=Null;
		throw $exc;
	}
}
?>
