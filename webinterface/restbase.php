<?php

require_once('error.php');

class CacheControl {
	public static function setCacheable($iscacheable) {
	}
	public static function setCacheTimeLimit($timelimit) {
	}
}

class RESTRequest {
	public $path;
	public $path_elements;
	public $verb;
	public $parameters;

	public function __construct() {
		$this->verb = $_SERVER['REQUEST_METHOD'];
		$this->path = preg_replace('|/+$|', '', $_SERVER['PATH_INFO']);
		$this->path_elements = explode('/', $this->path);
		$this->format = 'plain';
		$this->parseIncomingParams();
		if (isset($this->parameters['format']))
			$this->format = $this->parameters['format'];
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

	public $request;

	private function __construct($request, $path_parts=array()) {
		$this->request = $request;
		$this->path_parts = $path_parts;
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
		return $this->render($this->handle_inner());
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

	protected function renderform() {}
	protected function render_header($data) {

		switch ($this->request->format) {
		case 'html':
			header("Content-Type: text/html");
			print "<html>\n";
			print "<head><title>{$data['title']}</title></head>\n";
			print "<body>\n";
			break;
		case 'plain':
			header("Content-Type: text/plain");
			break;
		case 'json':
			throw new NotImplemented("format: '{$this->request->format}'");
			header("Content-Type: application/json");
			break;
		default:
			throw new BadRequest("format: '{$this->request->format}'");
		}
	}
	protected function render_footer($data) {
		switch ($this->request->format) {
		case 'html':
			print "</html>\n";
			break;
		case 'plain':
			break;
		case 'json':
			break;
		default:
			throw new NotImplemented("format: '{$this->request->format}'");
		}
	}

	protected function render_data($data) {
		switch ($this->request->format) {
		case 'html':
			$this->renderform();
			break;
		case 'plain':
			break;
		case 'json':
			break;
		default:
			throw new NotImplemented("format: '{$this->request->format}'");
		}
	}

	private function render($data) {
		$this->render_header($data);
		$this->render_data($data);
		$this->render_footer($data);
	}
}
?>