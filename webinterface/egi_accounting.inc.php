<?php

require_once('rest_base.inc.php');
require_once('db.inc.php');
require_once('auth.inc.php');
require_once('basic_handlers.inc.php');

/*
  Handles querying individual jobs for accounting information.
 */
class AccinfoHandler extends cg_job_Handler
{
	protected function handleGet() {
		$ids = $this->get_selected_ids();
		$field = $this->get_selected_attrs();

		$r = new ResWrapper(
			DB::q("SELECT {$field} FROM accounting_info "
			      . "WHERE id in ({$ids})"
			      . $this->auth_sql_filter(' AND ')));
		
		while ($line = mysql_fetch_array($r->res, MYSQL_ASSOC))
			$this->output_dataitem($line);
	}
	protected function allowed() {
		return "GET";
	}
	public static function pathRegex() {
		return '|^/jobs/(?<id>[^/]+)/accinfo(/(?<attr>[^/]*)/?)?$|';
	}
}

?>
