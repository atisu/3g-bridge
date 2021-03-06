<?php

require_once('rest_base.inc.php');
require_once('db.inc.php');
require_once('auth.inc.php');

class VersionHandler extends RESTHandler {
        protected function handleGet() {
                Log::log('DEBUG', 'Querying 3G Bridge version');
                $ret = exec(BRIDGE_VER_CMD, $output, $exitcode);
                Log::log('AUDIT', "3G Bridge version query (exit code: $exitcode): '$ret'");
                $this->output_dataitem(
                        array('version' => $ret));
        }
        protected function allowed() {
                return "GET";
        }
        public static function pathRegex() {
                return '|^/version/?$|';
        }
}

/*
  Methods used both by Jobs and Jobs/* handlers
 */
class cg_job_Handler extends RESTHandler {
        protected $auth_audit = '(no authentication)';

        protected function auth_sql_filter($prefix='', $postfix='') {
                $ai = AuthInfo::instance();

                if (!$ai->do_authorization())
                        return '';

                $uid=DB::stringify($ai->userid());
                $this->auth_audit = '(authenticated user: $uid)';

                return "{$prefix} (userid <=> {$uid}) {$postfix}";
        }

        protected function auth_check_ids($ids) {
                $ai = AuthInfo::instance();
                if ($ai->do_authorization())
                {
                        $this->auth_audit = '(authenticated user: $uid)';

                        $uid = DB::stringify($ai->userid());
                        $r = new ResWrapper(
                                DB::q("SELECT COUNT(*) FROM cg_job "
                                      . "WHERE id IN ({$ids})"
                                      . $this->auth_sql_filter(' AND NOT ')));
                        if (!($line = mysql_fetch_array($r->res))) {
                                DB::derr();
                        }

                        if ($line[0]) //exists
                                throw new AuthorizationError;
                }
        }

        protected function limit_clause() {
                $l = $this->request->select_limit;
                if ($l == Null)
                        return '';
                else
                        return " limit {$l}";
        }

        protected function cancel_jobs($ids) {
                $ids_arr = explode(", ", $ids);

                if (count($ids_arr) == 0) return;

                $this->auth_check_ids($ids);

                $s = count($ids_arr) == 1 ? '' : 's';
                Log::log('AUDIT', "Canceling job$s $ids $this->auth_audit");

                DB::q("UPDATE cg_job SET status='CANCEL' WHERE id in ({$ids})");
        }

        protected function allowedFields() {
                return array("id", "grid", "alg", "status", "gridid", "args", "griddata", "tag", "creation_time", "metajobid", "userid", "error_info",
			     "mjrunning",
			     "mjerror");
        }
}

/*
  Handle job listing and submission
 */
class JobsHandler extends cg_job_Handler
{
        private $ismetajob;

        protected function timefilter() {
                if (isset($_GET['last'])) {
                        return " ORDER BY creation_time DESC LIMIT {$_GET['last']}";
                }
                else
                        return '';
        }

        protected function handleGet() {
                Log::log('AUDIT', "Querying all jobs $this->auth_audit");
                $authfilter = $this->auth_sql_filter(' WHERE ');
                $timefilter = $this->timefilter();
                $q = 'SELECT * FROM cg_job'
                        . $authfilter
                        . $timefilter
                        . $this->limit_clause();
                $r = mysql_query($q);
                while ($line = mysql_fetch_array($r, MYSQL_ASSOC)) {
                        $this->output_dataitem($line, $line['id']);
                }
        }
        protected function handleDelete() {
                Log::log('DEBUG', 'Canceling jobs');

                $ids = join(", ", array_map(stringify, $this->request->parameters));

                $this->cancel_jobs($ids);
        }
        protected function handlePost() {
                Log::log('DEBUG', 'Submitting job(s)');

                // Cache information
                $r = $this->request;
                $p=$r->parameters;
                $this->input_directory = $r->cfg['wssubmitter']['input-dir'];
                if (!$this->input_directory)
                        throw new ConfigError('wssubmitter/input-dir');
                $this->output_directory = $r->cfg['wssubmitter']['output-dir'];
                if (!$this->output_directory)
                        throw new ConfigError('wssubmitter/output-dir');
                $ai=AuthInfo::instance();
                $this->userid = $ai->userid();

                // Submit jobs; ids are collected
                $this->ismetajob = false;
                $ids = array();

                // If the regular POST request contained a special job list
                // (_GET['json'] for example), only use that.
                if (isset($p[JOBDEFS])) {
                        foreach ($p[JOBDEFS] as $job)
                                C::cond_push($ids, $this->submit_job($job));
                }
                else
                        // There was no special job list, the POST request
                        // itself must be a job definition
                        C::cond_push($ids, $this->submit_job($p));

                $cnt = count($ids);
                $idss = join(', ', $ids);
                $jbs = $this->ismetajob ? 'metajob' : 'job';
                $s = $cnt > 1 ? 's' : '';
                Log::log('AUDIT', "Submitted $cnt $jbs$s: $idss $this->auth_audit");

                // Simply print newly generated job ids or redirect to a page
                // fully describing them, as requested.
                if ($r->redirect_after_submit) {
                        $target = BASE_URL
                                . "/jobs/" . join('+', $ids)
                                . "?" . $_SERVER['QUERY_STRING'];
                        Log::log('INFO', "Redirecting client to '$target'");
                        header("Location: " . $target);
                        return TRUE;
                }
                else
                        foreach ($ids as $i)
                                $this->output_dataitem(array('id'=>$i));
        }
        protected function allowed() {
                return "GET, POST, DELETE";
        }
        protected function renderform()
        {
                //TODO: submission GUI
        }
        public static function pathRegex() {
                return '|^/jobs/?$|';
        }

        private function checked_get(&$p, $key, $defval=Null) {
                if (isset($p[$key]))
                        return $p[$key];
                elseif ($defval===Null)
                        throw new BadRequest("Missing job attribute: '{$key}'");
                else
                        return $defval;
        }

        private function input_files($p) {
                $id=$p['id'];
                $infiles=array();

                // Prepare input files uploaded in POST request
                // TODO: solve file upload issues:
                //       - logical name is incorrect (priority!)
                //       - if an error happens, move_upload_file provides
                //         no information about what happened
                foreach ($_FILES as $logicalName => $info) {
                        $tmppath = $info['tmp_name'];
                        $outdir = "{$this->input_directory}/".C::hash_dir($id);
                        $outpath = "{$outdir}/{$logicalName}";
                        if ($info['error'] == UPLOAD_ERR_OK) {
                                mkdir($outdir, 0755, true);
                                if (!move_uploaded_file($tmppath, $outpath))
                                        throw new ServerError('input error');

                                $infiles[$logicalName] =
                                        array('url' => $outpath,
                                              'md5' => md5_file($outpath),
                                              'size' => filesize($outpath));
                        }
                        else throw ServerError('File upload error ');
                }

                // Prepare remote files
                // TODO: parse URL (not sure if really necessary)
                foreach ($p['input'] as $logicalName => $info__) {
                        $info = (array)$info__;
                        // Report unknown attributes
                        foreach ($info as $key=>$value) {
                                if (!in_array($key,
                                              array('md5', 'size', 'url')))
                                {
                                        throw new BadRequest(
                                                "Invalid attribute for input "
                                                . "file '{$logicalName}': "
                                                . "'{$key}'");
                                }
                        }
                        // URL is the only mandatory attribute
                        if (!isset($info['url']))
                                throw new BadRequest(
                                        "Missing attribute 'url' "
                                        . "for input file '{$logicalName}'");

                        $infiles[$logicalName] = $info;
                }

                return $infiles;
        }
        private function output_files($p) {
                $id=$p['id'];
                $outdir = "{$this->output_directory}/" . C::hash_dir($id);
                if (!@mkdir($outdir, 0777, true)) {
                        $err = error_get_last();
                        throw new ServerError("Could not create output directory '$outdir'. " . $err['message']);
                }
                if (!@chmod($outdir, 01777)) {
                        $err = error_get_last();
                        throw new ServerError("Error setting permission on directory '$outdir'. " . $err['message']);
                }
                $outfiles = array();
                foreach ($p['output'] as $logicalName) {
                        $outfiles[$logicalName] =
                                "{$outdir}/{$logicalName}";
                }
                return $outfiles;
        }
        private function envvars($p) {
                $vars = array();
                if (isset($p['env']))
                        $vars = $p['env'];
                return $vars;
        }

        /*
          Returns array containing submission query strings:

          0: complete CG_JOB insert statement
          1: format string for CG_INPUTS insert, template args:
               (localname, url, md5, size:int)
          2: format string for CG_OUTPUTS insert, template args:
               (localname, path)
          3: format string for CG_ENV insert, template args:
               (name, value)
         */
        private function build_queries($id, $name, $grid,
                                       $gridid, $args, $tag) {
                $safe_id = DB::stringify($id);
                return array(
                        sprintf("INSERT INTO cg_job "
                                . "(id, alg, grid, gridid, args, "
                                . "tag, status, userid) "
                                . "VALUES (%s, %s, %s, %s, %s, %s, %s, %s)",
                                $safe_id,
                                DB::stringify($name),
                                DB::stringify($grid),
                                DB::stringify($gridid),
                                DB::stringify($args),
                                DB::stringify($tag),
                                DB::stringify('INIT'),
                                DB::stringify($this->userid)),
                        sprintf("INSERT INTO cg_inputs "
                                . "(id, localname, url, md5, filesize) "
                                . "VALUES (%s, %%s, %%s, %%s, %%d)",
                                $safe_id),
                        sprintf("INSERT INTO cg_outputs "
                                . "(id, localname, path) "
                                . "VALUES (%s, %%s, %%s)",
                                $safe_id),
                        sprintf("INSERT INTO cg_env (id, name, val) "
                                . "VALUES (%s, %%s, %%s)",
                                $safe_id));
        }

        private function is_mj_def($fname) {
                return preg_match('/^_3gb-metajob/', $fname);
        }
        /*
          Determines actual cg_job.grid and cg_job.grid_id values to be
          inserted, based on whether the submitted job is a meta-job.
         */
        private function chk_metajob($infiles, $realgrid) {
                $mjd_cnt = count(array_filter(array_keys($infiles),
                                              array($this, 'is_mj_def')));
                if ($mjd_cnt == 0)
                        return array($realgrid, '');
                elseif ($mjd_cnt == 1) {
                        $this->ismetajob = true;
                        return array(METAJOB_GRID, $realgrid);
                }
                else
                        throw new BadRequest(
                                'Too many meta-job definition files');
        }

        private function submit_job($descr) {
                $p = (array)$descr;
                $id = C::gen_uuid();
                $p['id'] = $id;

                $infiles = $this->input_files($p);
                $outfiles = $this->output_files($p);
                $envvars = $this->envvars($p);

                list($grid, $gridid) =
                        $this->chk_metajob($infiles,
                                           $this->checked_get($p, 'grid'));

                list($addjob, $addinput_fmt, $addoutput_fmt, $addenv_fmt) =
                        $this->build_queries($id,
                                             $this->checked_get($p, 'name'),
                                             $grid, $gridid,
                                             $this->checked_get($p, 'args', ''),
                                             $this->checked_get($p, 'tag', ''));


                DB::begin();
                try {
                        DB::q($addjob);
                        foreach ($infiles as $key=>$value) {
                                DB::q(sprintf($addinput_fmt,
                                              DB::stringify($key),
                                              DB::stringify($value['url']),
                                              DB::stringify($value['md5']),
                                              isset($value['size'])
                                              ? $value['size'] : 'NULL'));
                        }
                        foreach ($outfiles as $key=>$value) {
                                DB::q(sprintf($addoutput_fmt,
                                              DB::stringify($key),
                                              DB::stringify($value)));
                        }
                        foreach ($envvars as $key=>$value) {
                                DB::q(sprintf($addenv_fmt,
                                              DB::stringify($key),
                                              DB::stringify($value)));
                        }

                        DB::commit();
                }
                catch (Exception $ex) {
                        DB::rollback();
                        throw $ex;
                }

                return $id;
        }
}

/*
  Handle listing of finished jobs. Also handles normal job submission, but it's
  irrelevant.
 */
class FinishedJobsHandler extends JobsHandler
{
        protected function handleGet() {
                Log::log('DEBUG', 'Querying finished jobs');

                $field = $this->get_selected_attrs();
                if ($field == 'output') {
                        Log::log('AUDIT', "Querying output of all finished jobs $this->auth_audit");
                        $this->handleGetOutput($ids);
                }
                else {
                        Log::log('AUDIT', "Querying all finished jobs $this->auth_audit");

                        $q = "SELECT {$field} FROM cg_job WHERE status='FINISHED'"
                                . $this->auth_sql_filter(' AND ')
                                . $this->limit_clause();
                        $r = mysql_query($q);
                        while ($line = mysql_fetch_array($r, MYSQL_ASSOC)) {
                                $this->output_dataitem($line, $line['id']);
                        }
                }
        }
        private function handleGetOutput($ids) {
                $c = $this->request->cfg;
                $urlprefix = $c['wssubmitter']['output-url-prefix'];
                if (!$urlprefix)
                        throw new ConfigError('wssubmitter/output-url-prefix');
                $query = "SELECT j.id, concat('{$urlprefix}/', "
                        .                  "substr(j.id, 1, 2), "
                        .                  "'/', j.id, "
                        .                  " '/', localname) URL "
                        . "FROM cg_outputs o "
                        . "  JOIN cg_job j ON o.id=j.id "
                        . "WHERE status='FINISHED' "
                        . $this->auth_sql_filter(' AND ')
                        . " ORDER by j.id"
                        . $this->limit_clause();
                $r = new ResWrapper(DB::q($query));

                while ($line = mysql_fetch_array($r->res, MYSQL_ASSOC))
                        $this->output_dataitem($line);
        }
        public static function pathRegex() {
                return '|^/jobs/finished(/(?<attr>[^/]*)/?)?$|';
        }
}

/*
  Handle listing of finished jobs of a specific application. Also handles normal job submission,
  but it's irrelevant.
 */
class AppFinishedJobsHandler extends JobsHandler
{
        protected function handleGet() {
                Log::log('DEBUG', 'Querying finished jobs');

                $appname = $this->matches['app'];

                $field = $this->get_selected_attrs();
                if ($field == 'output') {
                        Log::log('AUDIT', "Querying output of finished jobs of app '$appname' $this->auth_audit");
                        $this->handleGetOutput($ids);
                }
                else {
                        Log::log('AUDIT', "Querying finished jobs of app '$appname' $this->auth_audit");

                        $status = strtoupper($this->matches['status']);
                        $q = "SELECT {$field} FROM cg_job WHERE status='{$status}' AND alg='$appname' "
                                . $this->auth_sql_filter(' AND ')
                                . $this->limit_clause();
                        Log::log('DEBUG', $q);
                        $r = new ResWrapper(DB::q($q));
                        while ($line = mysql_fetch_array($r->res, MYSQL_ASSOC)) {
                                $this->output_dataitem($line, $line['id']);
                        }
                }
        }
        private function handleGetOutput($ids) {
                $c = $this->request->cfg;
                $urlprefix = $c['wssubmitter']['output-url-prefix'];
                if (!$urlprefix)
                        throw new ConfigError('wssubmitter/output-url-prefix');
                $r = new ResWrapper(
                        DB::q("SELECT j.id, concat('{$urlprefix}/', "
                              .                  "substr(j.id, 1, 2), "
                              .                  "'/', j.id, "
                              .                  " '/', localname) URL "
                              . "FROM cg_outputs o "
                              . "  JOIN cg_job j ON o.id=j.id "
                              . "WHERE status='FINISHED' AND alg='$appname' "
                              . $this->auth_sql_filter(' AND ')
                              . " ORDER by j.id"
                              . $this->limit_clause()));

                while ($line = mysql_fetch_array($r->res, MYSQL_ASSOC))
                        $this->output_dataitem($line);
        }
        public static function pathRegex() {
                return '@^/apps/(?<app>[^/]+)/(?<status>finished|error)(/(?<attr>[^/]*)/?)?$@';
        }
}


/*
  Handles querying individual jobs and attribute (field) selection.
  Also handles job deletion.
 */
class JobHandler extends cg_job_Handler
{
        protected function handleGet() {
                Log::log('DEBUG', 'Querying job information');

                $ids = $this->get_selected_ids();

                if (array_key_exists('attr', $this->matches)
                    and $this->matches['attr'] == 'output')
                {
                        Log::log('AUDIT', "Querying output of job$s $ids $this->auth_audit");
                        $this->handleGetOutput($ids);
                }
                else {
                        $field = $this->get_selected_attrs();
			$field = preg_replace("/mjrunning/", "(select count(*) from cg_job as j2 where j2.metajobid=cg_job.id and j2.status='RUNNING') as mjrunning", $field);
			$field = preg_replace("/mjerror/", "(select count(*) from cg_job as j2 where j2.metajobid=cg_job.id and j2.status='ERROR') as mjerror", $field);
                        $s = count(explode(", ", $ids)) != 1 ? 's' : '';
                        Log::log('AUDIT', "Querying information on job$s $ids $this->auth_audit");

                        $r = new ResWrapper(
                                DB::q("SELECT {$field} FROM cg_job "
                                      . "WHERE id in ({$ids})"
                                      . $this->auth_sql_filter(' AND ')));

                        while ($line = mysql_fetch_array($r->res, MYSQL_ASSOC))
                                $this->output_dataitem($line);
                }
        }
        private function handleGetOutput($ids) {
                $c = $this->request->cfg;
                $urlprefix = $c['wssubmitter']['output-url-prefix'];
                if (!$urlprefix)
                        throw new ConfigError('wssubmitter/output-url-prefix');
                $r = new ResWrapper(
                        DB::q("SELECT j.id, localname, concat('{$urlprefix}/', "
                              .                  "substr(j.id, 1, 2), "
                              .                  "'/', j.id, "
                              .                  " '/', localname) URL "
                              . "FROM cg_outputs o "
                              . "  JOIN cg_job j ON o.id=j.id "
                              . "WHERE j.id in ({$ids}) "
                              . $this->auth_sql_filter(' AND ')));

                while ($line = mysql_fetch_array($r->res, MYSQL_ASSOC))
                        $this->output_dataitem($line);
        }
        protected function handleDelete() {
                Log::log('DEBUG', 'Canceling jobs');
                $ids = $this->get_selected_ids();
                $this->cancel_jobs($ids);
        }
        protected function allowed() {
                return "GET, DELETE";
        }
        public static function pathRegex() {
                return '|^/jobs/(?<id>[^/]+)(/(?<attr>[^/]*)/?)?$|';
        }
}


/*
  Lists available queues
 */
class QueuesHandler extends RESTHandler
{
        protected function handleGet() {
                Log::log('AUDIT', 'Querying all queues');

                $field = $this->get_selected_attrs();

                $r = new ResWrapper(DB::q("SELECT {$field} "
                                          . "FROM cg_algqueue "));

                while ($line = mysql_fetch_array($r->res, MYSQL_ASSOC)) {
                        $this->output_dataitem($line);
                }
        }
        protected function allowed() {
                return "GET";
        }
        protected function allowedFields() {
                return array("grid", "alg", "batchsize", "statistics");
        }
        public static function pathRegex() {
                return '|^/queues(/(?<attr>[^/]*)/?)?$|';
        }
}

/*
  Lists available grids
 */
class GridsHandler extends RESTHandler
{
        protected function handleGet() {
                Log::log('AUDIT', 'Querying all grids');
                $r = new ResWrapper(DB::q('SELECT DISTINCT grid FROM cg_algqueue'));
                while ($line = mysql_fetch_array($r->res, MYSQL_ASSOC))
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

/*
  Lists available algorithms in a specified grid with their attributes
 */
class GridHandler extends RESTHandler
{
        protected function handleGet() {
                Log::log('DEBUG', 'Querying information on grids');
                $ids_arr = explode($this->request->list_separator,
                                   $this->matches['id']);
                $ids = join(', ', array_map('DB::stringify', $ids_arr));
                $field = $this->get_selected_attrs();

                $s = count($ids_arr) != 1 ? 's' : '';
                Log::log('AUDIT', "Querying information on queue$s $ids");

                $r = new ResWrapper(DB::q("SELECT {$field} "
                                          . "FROM cg_algqueue "
                                          . "WHERE grid in ({$ids})"));

                while ($line = mysql_fetch_array($r->res, MYSQL_ASSOC)) {
                        $this->output_dataitem($line);
                }
        }
        protected function allowed() {
                return "GET";
        }
        protected function allowedFields() {
                return array("grid", "alg", "batchsize", "statistics");
        }
        public static function pathRegex() {
                return '|^/grids/(?<id>[^/]+)(/(?<attr>[^/]*)/?)?$|';
        }
}

?>
