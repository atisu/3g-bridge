<?php

require_once('rest_base.inc.php');
require_once('db.inc.php');
require_once('auth.inc.php');

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
	protected function handleDelete() {
		throw new NotImplemented();
	}
	protected function allowed() {
		return "GET, DELETE";
	}
	public static function pathRegex() {
		return '|^/jobs/(?<id>[^/]+)(/(?<attr>[^/]*)/?)?$|';
	}
}

class GridsHandler extends RESTHandler
{
	protected function handleGet() {
		$q = 'SELECT DISTINCT grid FROM cg_algqueue';
		$r = mysql_query($q);
		while ($line = mysql_fetch_array($r, MYSQL_ASSOC))
			$this->output_dataitem($line);
	}
	protected function allowed() {
		return "GET";
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
	}
	protected function allowed() {
		return "GET";
	}
	public static function pathRegex() {
		return '|^/grids/(?<id>[^/]+)(/(?<attr>[^/]*)/?)?$|';
	}
}

?>