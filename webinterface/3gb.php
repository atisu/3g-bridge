<?php

$CONFIG_FILE='/home/avisegradi/Inst/etc/3g-bridge.conf';

ini_set('display_errors','On');
error_reporting(E_ALL);

require_once('error.inc.php');
require_once('common.inc.php');
require_once('rest_base.inc.php');
require_once('db.inc.php');
require_once('auth.inc.php');

function err_logger($errcode, $msg) {
}

class JobsHandler extends RESTHandler
{
	protected function handleGet() {
		CacheControl::setCacheable(FALSE);
		$q = 'SELECT * FROM cg_job';
		$r = mysql_query($q);
		while ($line = mysql_fetch_array($r, MYSQL_ASSOC)) {
			$this->output_dataitem($line, $line['id']);
		}
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
		$id = $this->matches['id'];
		$full = !array_key_exists('attr', $this->matches);
		$field = $full ? '*' : $this->matches['attr'];
		$q = "SELECT {$field} FROM cg_job WHERE id='{$id}'";
		$r = mysql_query($q);
		
		if (!($line = mysql_fetch_array($r, MYSQL_ASSOC)))
			throw NotFound("Job: {$id}");
			
		$this->render_dataitem($full ? $line : array($id) + $line);
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
