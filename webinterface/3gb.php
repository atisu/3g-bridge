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

try {
        Log::instance(LOG_FILE, LOG_LEVEL);
        Log::log('AUDIT', "Serving request [${_SERVER['REQUEST_URI']}]");

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
