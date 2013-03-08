<?php

// BASE_URL must point to the REST interface itself.
// This setting is used for redirection.
define('BASE_URL', 'https://<project_url>/download/wss');
// The path of the 3G Bridge condfiguration.
// You should probably leave this as is, and create a hard link to the
// file. This way, it should work no matter how Apache is configured.
define('CONFIG_FILE', '3g-bridge.conf');
// Command used to query the version of the Bridge used.
// Make sure it executes the binary being used.
define('BRIDGE_VER_CMD',
       '/bin/bash -c /usr/sbin/3g-bridge\ -V');
// Destination grid for incoming meta-jobs.
define('METAJOB_GRID', 'Metajob');

ini_set('display_errors','On');
error_reporting(E_ALL);

require_once('error.inc.php');
require_once('common.inc.php');
require_once('rest_base.inc.php');
require_once('db.inc.php');
require_once('auth.inc.php');
require_once('basic_handlers.inc.php');
require_once('egi_accounting.inc.php');

function err_logger($errcode, $msg) {
}

RESTHandler::addHandler('AccinfoHandler');
RESTHandler::addHandler('JobsHandler');
RESTHandler::addHandler('JobHandler');
RESTHandler::addHandler('FinishedJobsHandler');
RESTHandler::addHandler('VersionHandler');
RESTHandler::addHandler('GridsHandler');
RESTHandler::addHandler('GridHandler');
RESTHandler::addHandler('QueuesHandler');

set_error_handler('err_logger');
set_exception_handler('final_handler');

try {
	$cfg = parse_ini_file(CONFIG_FILE, true, INI_SCANNER_RAW);
	$db = DB::instance($cfg);
	$r = RESTRequest::instance($cfg);
	$hndlr = RESTHandler::create($r);
	$hndlr->handle();
}
catch (HTTPException $ex) {
	$ex->render($r);
	exit(1);
}

?>
