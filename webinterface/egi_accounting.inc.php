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
	protected function where($ids)
	{
		if ($ids == "'*'")
			return $this->auth_sql_filter(' WHERE ');
		else
			return "WHERE id in ({$ids})" . $this->auth_sql_filter(' AND ');
	}

	protected function handleGet() {
		$ids = $this->get_selected_ids();
		$field = $this->get_selected_attrs();
                $w = $this->where($ids);

		$r = new ResWrapper(
			DB::q("SELECT {$field} "
                              . " FROM accounting_info_metajob "
			      . $w
                              . " UNION "
                              . " SELECT {$field} "
                              . " FROM accounting_info_nonmetajob "
			      . $w));

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
