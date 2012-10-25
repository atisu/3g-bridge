<?php

require_once('error.php');

class CacheControl {
	public static function setCacheable($iscacheable) {
	}
	public static function setCacheTimeLimit($timelimit) {
	}
}

abstract class RESTParser {
	public static function create($request) {
		// TODO: factory($format)
	}
	
	public function __construct($request) {
	}
	public abstract function parse();
	     
}

abstract class RESTRenderer {
	public static function create($request) {
		// TODO: factory($format)
	}
		
	public function __construct($request) {
	}	
	protected abstract function render_header($handler);
	protected abstract function render_footer();
	protected abstract function render_dataitem($data, $id=Null);
}

class RESTRequest {
	public $path;
	public $path_elements;
	public $verb;
	public $parameters;

	private static $inst = Null;
	public static function instance() {
		if (!RESTRequest::$inst)
			RESTRequest::$inst = new RESTRequest;
		return RESTRequest::$inst;
	}
	private function getpar($key, $dest, $default) {
		if (isset($this->parameters[$key]))
			$this->$dest = $this->parameters[$key];
		else
			$this->$dest = $default;
	}
	private function __construct() {
		$this->verb = $_SERVER['REQUEST_METHOD'];
		$this->path = preg_replace('|/+$|', '', $_SERVER['PATH_INFO']);
		$this->path_elements = explode('/', $this->path);
		$this->parseIncomingParams();
		$this->getpar('format', 'format', 'plain');
		$this->getpar('sep', 'plain_separator', ' ');
		$this->getpar('hdr', 'header', $this->format == 'html' ? TRUE : FALSE);
		return true;
	}
	private function parseIncomingParams() {
		$parameters=array();
		if (isset($_SERVER['QUERY_STRING']))
			parse_str($_SERVER['QUERY_STRING'], $parameters);

		$body = file_get_contents("php://input");
		$content_type=false;
		if (isset($_SERVER['CONTENT_TYPE']))
			$content_type = $_SERVER['CONTENT_TYPE'];

		switch ($content_type) {
		case 'application/json':
			$body_params = json_decode($body);
			if ($body_params)
				foreach ($body_params as $pname => $pval)
					$parameters[$pname] = $pval;
			$this->format = 'json';
			break;
		case 'application/x-www-form-urlencoded':
			parse_str($body, $postvars);
			foreach ($postvars as $field=>$value)
				$parameters[$field]=$value;
			$this->format = 'html';
			break;
		default:
			$this->format = 'plain';
			break;
		}

		$this->parameters = $parameters;
	}
}

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

	private function __construct($request, $matches) {
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

	public static function pathRegex() { return '|^.*$|'; }

	public function handle() {
		$this->render_header($data);
		$this->handle_inner();
		$this->render_footer($data);
	}

	private function handle_inner() {
		switch ($this->request->verb) {
		case 'GET': return $this->handleGet();
		case 'PUT': return $this->handlePut();
		case 'POST': return $this->handlePost();
		case 'DELETE': return $this->handleDelete();
		default:
			throw new NotSupported($this->request->verb,
					       $this->allowed());
		}
	}

	protected function render_html_form() { /* NOOP; to be overridden */ }
	protected function render_header() {
		// For dataitem separators
		$this->first = TRUE;

		switch ($this->request->format) {
		case 'html':
			header("Content-Type: text/html");
			print C::$HTML_HEADER;
			print "<body>\n";
			$this->renderform();
			print '<table id="data">';
			break;
		case 'plain':
			header("Content-Type: text/plain");
			break;
		case 'json':
			header("Content-Type: application/json");
			print '[';
			break;
		default:
			throw new BadRequest("format: '{$this->request->format}'");
		}
	}
	protected function render_footer() {
		switch ($this->request->format) {
		case 'html': print "</table></body></html>\n"; break;
		case 'plain': /* NOOP */ break;
		case 'json': print ']'; break;
		default: throw new BadRequest("format: '{$this->request->format}'");
		}
	}

	protected function render_dataitem($data, $id=Null) {
		switch ($this->request->format) {
		case 'html':
			// Print header if needed
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
			break;
		case 'plain':
			$sep = $this->request->plain_separator;

			// Separate dataitems
			if (!$this->first)
				print "\n";
			// Print header if needed
			elseif ($this->request->header) {
				$f = TRUE;
				foreach ($data as $key=>$value) {
					if ($f) $f=FALSE; else print $sep;
					print $key;
				}
				print "\n";
			}

			//Print dataitem
			$f = TRUE;
			foreach ($data as $value) {
				if ($f) $f=FALSE; else print $sep;
				print $value;
			}
			break;
		case 'json':
			if (!$this->first) print ', ';
			print json_encode($data);
			break;
		default:
			throw new BadRequest("format: '{$this->request->format}'");
		}

		// For dataitem separators
		if ($this->first) $this->first = FALSE;
	}
}
?>