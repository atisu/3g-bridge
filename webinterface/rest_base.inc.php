<?php

define('JOBDEFS', 'jobdefs');
define('HTML_HEADER', "<!DOCTYPE html>\n<html>\n<head>\n	<title>3G Bridge Webinterface</title>\n	<link rel=\"stylesheet\" type=\"text/css\" href=\"wss.css\" />\n</head>\n");

require_once('common.inc.php');
require_once('error.inc.php');

class CacheControl {
	public static function setCacheable($iscacheable) {
	}
	public static function setCacheTimeLimit($timelimit) {
	}
}

/*
  General REST handler.

  - Parses request according to content-type (see parsers below).

  - Request is then handled by a RESTHandler based on the URL (application
    specific). The result has to be a list of associative arrays (e.g. table
    rows), which will be rendered according to the specified output format. A
    particular handler can return a single TRUE value instead, specifying that
    the result will only contain headers.

  - Finally, the resulting rows --if any-- are rendered according to output
    format specified by GET parameter 'format'. (See renderers below)

 */

class RESTRequest {
	public $path;
	public $path_elements;
	public $verb;
	public $parameters;
        public $contentType = 'text/plain';

	private static $inst = Null;
	/*
	  Singleton class.

	  $cfg: arbitrary configuration. Unused by these classes, stored only
		to be used in actual, application specific handlers

	  _GET parameters used:

	    sep:  field separator, default: <space>
	    rsep: record separator, default: <newline>
	    lsep: list separator used in the URL to separate resources,
		  default: +
	    hdr:  generate header for table (plain and html output)
	 */
	public static function instance($cfg) {
		if (!RESTRequest::$inst)
			RESTRequest::$inst = new RESTRequest($cfg);
		return RESTRequest::$inst;
	}
	private static function getpar($key, $default) {
		return isset($_GET[$key])
			? $_GET[$key]
			: $default;
	}

	public static function get_output_format($defval = 'plain') {
		return RESTRequest::getpar('format', $defval);
	}
        public static function get_content_type() {
                $ct='plain';
		if (isset($_SERVER['CONTENT_TYPE']))
			$ct = preg_replace('|([^;]+);.*|', '\\1',
					   $_SERVER['CONTENT_TYPE']);
                Log::log('INFO', "Request content type: '$ct'");
		if (array_key_exists($ct, RESTParser::$handlers))
			return $ct;
		else
			throw new NotImplemented("request format: '{$ct}'");
        }

        private function checked_int($key, $default) {
                $val = RESTRequest::getpar($key, $default);
                //Default value is accepted whatever it is; e.g. Null
                Log::log('DEBUG', "VAL={$val}");
                if ($val == $default) return $val;

                if (!is_numeric($val))
                        throw new BadRequest("Invalid value for GET parameter '{$key}'");

                return intval($val);
        }

	private function __construct($cfg) {
		$this->verb = $_SERVER['REQUEST_METHOD'];
		$this->path = preg_replace('|/+$|', '', $_SERVER['PATH_INFO']);
		$this->path_elements = explode('/', $this->path);
		$this->cfg=$cfg;

		// Parse GET parameters
                $this->contentType = RESTRequest::get_content_type();
		$this->format = RESTRequest::get_output_format($this->contentType);
		$this->redirect_after_submit = RESTRequest::getpar('redir', 1);
		$this->field_separator = RESTRequest::getpar('sep', ' ');
		$this->record_separator = RESTRequest::getpar('rsep', "\n");
		$this->list_separator = RESTRequest::getpar('lsep', '+');
		$this->header = RESTRequest::getpar(
			'hdr', $this->format == 'html' ? TRUE : FALSE);
                $this->select_limit =
                        $this->checked_int('limit', Null);
		$this->parser = RESTParser::create($this);
		$this->renderer = RESTRenderer::create($this);
		$this->parameters = $this->parser->parse();
		return true;
	}
}

/*
  General REST Handler class. Create subclasses to handle requests.

  Actual handlers have to be registered first. A particular handler will be
  selected for a request based on matching path regex. If more than a single
  handler can handle a path, the one registered later will be used (later
  registrations overwrite older ones).

  The regex can contain group definitions. The result of the matching will be
  stored to be available as extra information.

  E.g.: |^/collection/(?<resource_id>[^/]+)| will match /collection/*, and
	extract the resource identifier from the path.

  If a group named 'attr' is specified and exists in the path, it can be
  converted to a comma-separated list of DB field names using
  get_selected_attrs().

  Likewise, the group 'id' can be parsed using get_selected_ids(). The joined
  ids will be safely escaped to be used in a DB query.
 */
abstract class RESTHandler {
	public static $handlers = array();
	public static function addHandler($type) {
		array_push(RESTHandler::$handlers,
			   array('regex'=>$type::pathRegex(),
				 'type'=>$type));
	}
	public static function create($request) {
		arsort(RESTHandler::$handlers);
		foreach (RESTHandler::$handlers as $value) {
			$matches=array();
			preg_match($value['regex'],
				   $request->path,
				   $matches);
			if (count($matches))
				return new $value['type']($request, $matches);
		}

		throw new NotFound();
	}

	protected function __construct($request, $matches) {
		CacheControl::setCacheable(FALSE);
		$this->request = $request;
		$this->matches = $matches;
	}

	protected function handleGet() {
		throw new NotSupported('GET', $this->allowed()); }
	protected function handlePost() {
		throw new NotSupported('POST', $this->allowed()); }
	protected function handlePut() {
		throw new NotSupported('PUT', $this->allowed()); }
	protected function handleDelete() {
		throw new NotSupported('DELETE', $this->allowed()); }
	protected function allowed() { return ""; }
        protected function allowedFields() { return array(); }

	public static function pathRegex() { return '|^.*$|'; }
	public function render_html_form() { /* NOOP; to be overridden */ }

	protected function get_selected_attrs() {
		$lsep = $this->request->list_separator;
		if (array_key_exists('attr', $this->matches)
		    and $this->matches['attr'])
		{
                        $attrs = explode($lsep, $this->matches['attr']);
                        $allowed = $this->allowedFields();
                        foreach ($attrs as $k) {
                                Log::log('DEBUG', "Checking field: '{$k}'");
                                if (!in_array($k, $allowed)) {
                                        $allowed_s = join(', ', $allowed);
                                        throw new BadRequest(
                                                "Invalid field specified: '{$k}'. Available fields: {$allowed_s}");
                                }
                        }

			return join(', ', $attrs);
		}
		return '*';
	}

	public function get_selected_ids() {
		return join(', ',
			    array_map('DB::stringify',
				      explode($this->request->list_separator,
					      $this->matches['id'])));
	}

	public function handle() {
		ob_start();
		$this->request->renderer->render_header($this);

		try {
			$header_only = FALSE;
			switch ($this->request->verb) {
			case 'GET':
				$header_only = $this->handleGet(); break;
			case 'PUT':
				$header_only = $this->handlePut(); break;
			case 'POST':
				$header_only = $this->handlePost(); break;
			case 'DELETE':
				$header_only = $this->handleDelete(); break;
			default:
				throw new NotSupported($this->request->verb,
						       $this->allowed());
			}

			if ($header_only) {
				ob_end_clean();
			}
			else {
				$this->request->renderer->render_footer();
				ob_end_flush();
			}
		}
		catch (Exception $ex) {
			ob_end_flush();
			throw $ex;
		}
	}
	protected function output_dataitem($data, $id=Null) {
		$this->request->renderer->render_dataitem($data, $id);
	}
}

/*
 *  ****  Parsers ********************************

    Generally, these classes interpret the request and return it as an
    associative array, which will be stored in the RESTRequest object as
    $parameters.
 */

class RESTParser {
	public static $handlers=array();
	public static function register_handler($content_type, $handler_type) {
		RESTParser::$handlers[$content_type] = $handler_type;
	}
	public static function create($request) {
                return new RESTParser::$handlers[$request->contentType]($request);
	}

	public function __construct($request) {
		$this->request = $request;
	}
	public function parse() {
                $body = file_get_contents("php://input");
		return explode(PHP_EOL, $body);
	}
}

class JSONParser extends RESTParser {
	public function __construct($request) {
		parent::__construct($request);
	}
	public function parse() {
		$body = file_get_contents("php://input");
		$parameters = array();
		$parameters[JOBDEFS] = array();
		$body_params = json_decode($body);
		if ($body_params)
			foreach ($body_params as $pname => $pval)
				$parameters[JOBDEFS][$pname] = $pval;
		return $parameters;
	}
}
class FormPostParser extends RESTParser {
	public function __construct($request) {
		parent::__construct($request);
	}
	public function parse() {
		$body = file_get_contents("php://input");
		$parameters = array();
		parse_str($body, $postvars);
		foreach ($_POST as $key=>$value)
			$parameters[$key] = $value;
		foreach ($postvars as $field=>$value)
			$parameters[$field]=$value;
		if (isset($parameters['json'])) {
			$parameters[JOBDEFS] = array();
			if ($json_pars = json_decode($parameters['json'])) {
				foreach ($json_pars as $key=>$value)
					$parameters[JOBDEFS][$key]=$value;
			}
		}
		return $parameters;
	}
}


RESTParser::register_handler('plain', 'RESTParser');
RESTParser::register_handler('text/plain', 'RESTParser');
RESTParser::register_handler('application/json', 'JSONParser');
RESTParser::register_handler('application/x-www-form-urlencoded',
			     'FormPostParser');
RESTParser::register_handler('multipart/form-data', 'FormPostParser');

/*
 *  ****  Renderers  ********************************

    These classes generate output based on the specified output format. Errors
    are not rendered by these classes.


    The output will have the following structure:
    1)  Data-independent header part
    2a) If specified, upon rendering the first data item, a header may be
	generated based on the keys of the first data item
    2b) List of data items
    3)  Data-independent footer part

 */

abstract class RESTRenderer {
	private static $handlers=array();
	public static function register_handler($content_type, $handler_type) {
		RESTRenderer::$handlers[$content_type] = $handler_type;
	}
	public static function create($request) {
		$format=$request->format;
                Log::log("DEBUG", "Output format: $format");
		if (array_key_exists($format, RESTRenderer::$handlers))
			return new RESTRenderer::$handlers[$format]($request);
		else
			throw new NotImplemented("output format: '{$format}'");
	}

	public function __construct($request) {
		$this->request = $request;
	}

	public function render_header($handler) {
		$this->first = TRUE;
		return $this->render_header_i($handler);
	}
	public function render_footer() {
		$this->render_footer_i();
	}
	public function render_dataitem($data, $id=Null) {
		$retval = $this->render_dataitem_i($data, $id);
		if ($this->first) $this->first = FALSE;
		return $retval;
	}

	protected abstract function render_header_i($handler);
	protected abstract function render_footer_i();
	protected abstract function render_dataitem_i($data, $id=Null);
}
class HTMLRenderer extends RESTRenderer {
	public function __construct($request) {
		parent::__construct($request);
	}

	protected function render_header_i($handler) {
		header("Content-Type: text/html");
		print HTML_HEADER;
		print "<body>\n";
		$handler->render_html_form();
		print '<table id="data">';
	}
	protected function render_footer_i() {
		print "</table></body></html>\n";
	}
	protected function render_dataitem_i($data, $id=Null) {
		// Print table header if needed
		if ($this->first and $this->request->header) {
			print '<tr class="header"';
			if ($id) print " id=\"{$id}\"";
			print '>';
			foreach ($data as $key=>$value)
				print "<th id=\"{$key}\">{$key}</th>";
			print "</tr>\n";
		}

		//Print dataitem
		print '<tr class="dataitem"';
		if ($id) print " id=\"{$id}\"";
		print '>';
		foreach ($data as $key=>$value)
			print "<td id=\"{$key}\">{$value}</td>";
		print "</tr>\n";
	}
}
class JSONRenderer extends RESTRenderer {
	public function __construct($request) {
		parent::__construct($request);
	}

	protected function render_header_i($handler) {
		header("Content-Type: application/json");
		print '[';
	}
	protected function render_footer_i() {
		print ']';
	}
	protected function render_dataitem_i($data, $id=Null) {
		if (!$this->first) print ', ';
		print json_encode($data);
	}
}
class PlainRenderer extends RESTRenderer {
	public function __construct($request) {
		parent::__construct($request);
		$this->sep = $this->request->field_separator;
		$this->rsep = $this->request->record_separator;
	}

	protected function render_header_i($handler) {
		header("Content-Type: text/plain");
	}
	protected function render_footer_i() {
		/* NOOP */
	}
	protected function render_dataitem_i($data, $id=Null) {
		$sep = $this->sep;
		// Separate dataitems
		if (!$this->first)
			print $this->rsep;
		// Print header if needed
		elseif ($this->request->header) {
			$f = TRUE;
			foreach ($data as $key=>$value) {
				if ($f) $f=FALSE; else print $sep;
				print $key;
			}
			print $this->rsep;
		}

		//Print dataitem
		$f = TRUE;
		foreach ($data as $value) {
			if ($f) $f=FALSE; else print $sep;
			print $value;
		}
	}
}

RESTRenderer::register_handler('', 'PlainRenderer');
RESTRenderer::register_handler('text/plain', 'PlainRenderer');
RESTRenderer::register_handler('plain', 'PlainRenderer');
RESTRenderer::register_handler('json', 'JSONRenderer');
RESTRenderer::register_handler('application/json', 'JSONRenderer');
RESTRenderer::register_handler('html', 'HTMLRenderer');

?>