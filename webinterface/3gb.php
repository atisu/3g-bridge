<?php

ini_set('display_errors','On');
error_reporting(E_ALL);

include('common.php');

class JobsHandler extends RESTHandler
{
	protected function handleGet() {
		p($this, '$this');
	}
	protected function handlePost() {
	}
	protected function allowed() {
		return "GET, POST";
	}
}

class JobHandler extends RESTHandler
{	
	protected function handleGet() {
		p($this, '$this');
	}
	protected function handlePost() {
	}
	protected function allowed() {
		return "GET";
	}
}

$r=new RESTRequest;

RESTHandler::addHandler('/jobs', 'JobsHandler');
RESTHandler::addHandler('/jobs/.*', 'JobHandler');

$hndlr=RESTHandler::create($r);
if ($hndlr === FALSE)
	httpcode(404);

if (isset($r->parameters['format']) && $r->parameters['format'] == 'plain')
	header("Content-Type: text/plain");

try {
	$hndlr->handle();
}
catch (NotSupported $ns) {
	header("Allow: " . $ns->allowed);
	httpcode(405);
	exit(0);
}

?>