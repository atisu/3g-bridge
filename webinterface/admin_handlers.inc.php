<?php

require_once('rest_base.inc.php');
require_once('db.inc.php');
require_once('auth.inc.php');
require_once('basic_handlers.inc.php');

class Admin_GridsHandler extends GridsHandler {
	protected function allowed() {
		return "GET";
	}
	protected function handlePost() {
		throw NotImplemented();
	}
}

class Admin_GridHandler extends GridHandler {
	protected function allowed() {
		return "GET, POST, DELETE";
	}
	protected function handleDelete() {
		throw NotImplemented();
	}
	protected function handlePost() {
		throw NotImplemented();
	}
}

class Admin_JobsHandler extends JobsHandler
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

class Admin_FinishedJobsHandler extends JobHandler
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

?>