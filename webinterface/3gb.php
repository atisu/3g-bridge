<?php

define(CONFIG_FILE, '/home/avisegradi/Inst/etc/3g-bridge.conf');
define(BRIDGE_PATH, '/home/avisegradi/Inst/sbin/3g-bridge');

ini_set('display_errors','On');
error_reporting(E_ALL);

require_once('error.inc.php');
require_once('common.inc.php');
require_once('rest_base.inc.php');
require_once('db.inc.php');
require_once('auth.inc.php');

function err_logger($errcode, $msg) {
}

class VersionHandler extends RESTHandler {
	protected function handleGet() {
		$this->output_dataitem(array('version'=>exec(BRIDGE_PATH . ' -V')));
	}
	protected function allowed() {
		return "GET";
	}
	public static function pathRegex() {
		return '|^/version/?$|';
	}
}

class JobsHandler extends RESTHandler
{
	protected function handleGet() {
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

class FinishedJobsHandler extends JobsHandler
{
	protected function handleGet() {
		$field = $this->get_selected_attrs();
		$q = "SELECT {$field} FROM cg_job WHERE status='FINISHED'";
		$r = mysql_query($q);
		while ($line = mysql_fetch_array($r, MYSQL_ASSOC)) {
			$this->output_dataitem($line, $line['id']);
		}
	}
	public static function pathRegex() {
		return '|^/jobs/finished(/(?<attr>[^/]*)/?)?$|';
	}
}

class JobHandler extends RESTHandler
{
	protected function handleGet() {
		$ids = join(', ',
			    array_map('DB::stringify',
				      explode($this->request->list_separator,
					      $this->matches['id'])));
		$field = $this->get_selected_attrs();
		
		$q = "SELECT {$field} FROM cg_job WHERE id in ({$ids})";
		$r = new ResWrapper(mysql_query($q));
		
		$found = FALSE;
		while ($line = mysql_fetch_array($r->res, MYSQL_ASSOC)) {
			$found=TRUE;
			$this->output_dataitem($line);
		}

		if (!$found) 
			throw new NotFound("Job: {$ids}");
		
	}	
	protected function allowed() {
		return "GET";
	}
	public static function pathRegex() {
		return '|^/jobs/(?<id>[^/]+)(/(?<attr>[^/]*)/?)?$|';
	}
}

class GridsHandler extends RESTHandler
{
	protected function handleGet() {
		$q = 'SELECT * FROM cg_algqueue';
		$r = mysql_query($q);
		while ($line = mysql_fetch_array($r, MYSQL_ASSOC))
			$this->output_dataitem($line);
	}
	protected function handlePost() {
		throw NotImplemented();
	}
	protected function allowed() {
		return "GET, POST";
	}
	protected function renderform()
	{
	}
	public static function pathRegex() {
		return '|^/grids/?$|';
	}
}

class GridHandler extends RESTHandler
{
	protected function handleGet() {
		$ids = join(', ',
			    array_map('DB::stringify',
				      explode($this->request->list_separator,
					      $this->matches['id'])));
		$field = $this->get_selected_attrs();
		
		$q = "SELECT {$field} FROM cg_algqueue WHERE grid in ({$ids})";
		$r = new ResWrapper(mysql_query($q));
		
		$found = FALSE;
		while ($line = mysql_fetch_array($r->res, MYSQL_ASSOC)) {
			$found=TRUE;
			$this->output_dataitem($line);
		}

		if (!$found) 
			throw new NotFound("Grid: {$ids}");
		
	}	
	protected function allowed() {
		return "GET";
	}
	public static function pathRegex() {
		return '|^/grids/(?<id>[^/]+)(/(?<attr>[^/]*)/?)?$|';
	}
}

RESTHandler::addHandler('JobsHandler');
RESTHandler::addHandler('JobHandler');
RESTHandler::addHandler('FinishedJobsHandler');
RESTHandler::addHandler('VersionHandler');
RESTHandler::addHandler('GridsHandler');
RESTHandler::addHandler('GridHandler');

set_error_handler('err_logger');
set_exception_handler('final_handler');

try {
	$db = DB::instance(CONFIG_FILE);
	
	$r = RESTRequest::instance();
	
	$hndlr = RESTHandler::create($r);

	$hndlr->handle();
}
catch (HTTPException $ex) {
	$ex->render($r);
	exit(1);
}

?>
