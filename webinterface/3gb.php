<?php

ini_set('display_errors','On');
error_reporting(E_ALL);

require_once('error.php');
require_once('common.php');
require_once('restbase.php');

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
		p($this, '$this');
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

try {
	$r=new RESTRequest;
	$hndlr=RESTHandler::create($r);
	$hndlr->handle();
}
catch (HTTPException $ex) {
	$ex->render($r);
	exit(1);
}

?>