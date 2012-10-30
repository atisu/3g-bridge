<?php

/*

  curl https://canopus.lpds.sztaki.hu/t/jobs -F json='[{"name":"dsp", "grid":"test", "env":{"a":"b", "c":"d"}, "input":{"pools.txt":{"url":"urlabcd","md5":"md5abcd"}, "_3gb-metajob-1":{"url":"url2abcd222", "md5": "md52abcd222"}}, "output":["result.txt", "res2.txt"]}]' && echo
  
 */


define('CONFIG_FILE', '/home/avisegradi/Inst/etc/3g-bridge.conf');
define('BRIDGE_VER_CMD',
       '/bin/bash -c /home/avisegradi/Inst/sbin/3g-bridge\ -V');
define('BASE_URL', 'https://canopus.lpds.sztaki.hu/t');
define('METAJOB_GRID', 'Metajob');

ini_set('display_errors','On');
error_reporting(E_ALL);

require_once('error.inc.php');
require_once('common.inc.php');
require_once('rest_base.inc.php');
require_once('db.inc.php');
require_once('auth.inc.php');
require_once('basic_handlers.inc.php');

function err_logger($errcode, $msg) {
}

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
