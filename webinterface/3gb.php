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
	public function __construct($request, $matches) {
		CacheControl::setCacheable(FALSE);
		parent::__construct($request, $matches);
	}
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

class JobHandler extends RESTHandler
{
	public function __construct($request, $matches) {
		CacheControl::setCacheable(FALSE);
		parent::__construct($request, $matches);
	}
	
	protected function handleGet() {
		$lsep = $this->request->list_separator;
		
		$ids = join(', ', array_map('DB::stringify',
					    explode($lsep, $this->matches['id'])));
		$full = !array_key_exists('attr', $this->matches);
		$field = $full
			? '*'
			: join(', ', explode($lsep, $this->matches['attr']));
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
