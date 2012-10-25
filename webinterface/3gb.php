<?php

$CONFIG_FILE='/home/avisegradi/Inst/etc/3g-bridge.conf';

ini_set('display_errors','On');
error_reporting(E_ALL);

require_once('error.php');
require_once('common.php');
require_once('restbase.php');
require_once('db.php');
require_once('auth.php');

function err_logger($errcode, $msg) {
}

class JobsHandler extends RESTHandler
{
	protected function handleGet() {
		$q = 'SELECT * FROM cg_job';
		$r = mysql_query($q);
		C::p($this, '$this');
	}
	protected function handlePost() {
	}
	protected function allowed() {
		return "GET, POST";
	}
	protected function renderform()
	{
	}
	public static function pathRegex() {
		return '|^/jobs/?$|';
	}
}

class JobHandler extends RESTHandler
{
	protected function handleGet() {
		CacheControl::setCacheable(FALSE);
		C::p($this, '$this');
	}
	protected function handlePost() {
	}
	protected function allowed() {
		return "GET";
	}
	public static function pathRegex() {
		return '|^/jobs/(?<id>[^/]+)(/(?<attr>[^/]*)/?)?$|';
	}
}

RESTHandler::addHandler('JobsHandler');
RESTHandler::addHandler('JobHandler');

set_error_handler('err_logger');
set_exception_handler('final_handler');

try {
	$db = DB::instance($CONFIG_FILE);
	
	$r = RESTRequest::instance();
	
	$hndlr = RESTHandler::create($r);

	$hndlr->handle();
}
catch (HTTPException $ex) {
	$ex->render($r);
	exit(1);
}

?>