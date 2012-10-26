<?php

require_once('common.inc.php');
require_once('error.inc.php');

class AuthInfo {
	private static $inst = Null;
	public static function instance() {
		if (!AuthInfo::$inst)
			AuthInfo::$inst = new AuthInfo;
		return AuthInfo::$inst;
	}
	private function __construct() {
	}
	public function userid() {
		if ($_SERVER['SSL_CLIENT_VERIFY'] == 'SUCCESS')
			return $_SERVER['SSL_CLIENT_S_DN'];
		else
			return Null;
	}
}
?>
