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
	public function do_authorization() {
		$uid = $this->userid();
		return !($uid === Null) and ($uid != '');
	}
	public function userid() {
		return ($_SERVER['SSL_CLIENT_VERIFY'] == 'SUCCESS')
			? $_SERVER['SSL_CLIENT_S_DN']
			: Null;
}
}
?>
