<?php

require_once('config.inc.php');

ini_set('display_errors','On');
error_reporting(E_ALL);

require_once('log.inc.php');
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

header('Access-Control-Allow-Origin: *');
header('Access-Control-Allow-Methods: POST, GET, OPTIONS, DELETE');
header('Access-Control-Max-Age: 1000');
if(array_key_exists('HTTP_ACCESS_CONTROL_REQUEST_HEADERS', $_SERVER)) {
    header('Access-Control-Allow-Headers: '
           . $_SERVER['HTTP_ACCESS_CONTROL_REQUEST_HEADERS']);
} else {
    header('Access-Control-Allow-Headers: *');
}

if("OPTIONS" == $_SERVER['REQUEST_METHOD']) {
    exit(0);
}

try {
        Log::instance(LOG_FILE, LOG_LEVEL);
        Log::log('AUDIT', "Serving request [${_SERVER['REQUEST_URI']}]");

        $cfg = parse_ini_file(CONFIG_FILE, true, INI_SCANNER_RAW);
        $db = DB::instance($cfg);
        $r = RESTRequest::instance($cfg);
        $hndlr = RESTHandler::create($r);
        $hndlr->handle();
}
catch (ClientError $ex) {
        Log::log('ERROR', "Client error: ($ex->code) $ex->what");
        $ex->render($r);
        exit(0);
}
catch (HTTPException $ex) {
        try { Log::log('CRITICAL', "Exeception occured: ($ex->code) $ex->what"); }
        catch (Exception $ex) {}
        $ex->render($r);
        exit(1);
}

?>
