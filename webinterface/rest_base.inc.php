<?php

require_once('error.inc.php');
require_once('rest_renderer.inc.php');
require_once('rest_parser.inc.php');

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

	private static $inst = Null;
	public static function instance() {
		if (!RESTRequest::$inst)
			RESTRequest::$inst = new RESTRequest;
		return RESTRequest::$inst;
	}
	private static function getpar($key, $default) {
		if (isset($_GET[$key]))
			return $_GET[$key];
		else
			return $default;
	}

	public static function get_output_format() {
		return RESTRequest::getpar('format', 'plain');
	}

	private function __construct() {
		$this->verb = $_SERVER['REQUEST_METHOD'];
		$this->path = preg_replace('|/+$|', '', $_SERVER['PATH_INFO']);
		$this->path_elements = explode('/', $this->path);

		// Parse GET parameters
		$this->format = RESTRequest::get_output_format();
		$this->field_separator = RESTRequest::getpar('sep', ' ');
		$this->record_separator = RESTRequest::getpar('rsep', "\n");
		$this->list_separator = RESTRequest::getpar('lsep', '+');
		$this->header = RESTRequest::getpar(
			'hdr', $this->format == 'html' ? TRUE : FALSE);

		$this->parser = RESTParser::create($this);
		$this->renderer = RESTRenderer::create($this);
		$this->parameters = $this->parser->parse();
		return true;
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

	protected function __construct($request, $matches) {
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
	public function render_html_form() { /* NOOP; to be overridden */ }

	public function handle() {
		$this->request->renderer->render_header($this);

		switch ($this->request->verb) {
		case 'GET': return $this->handleGet();
		case 'PUT': return $this->handlePut();
		case 'POST': return $this->handlePost();
		case 'DELETE': return $this->handleDelete();
		default:
			throw new NotSupported($this->request->verb,
					       $this->allowed());
		}

		$this->request->renderer->render_footer();
	}
	protected function output_dataitem($data, $id=Null) {
		$this->request->renderer->render_dataitem($data, $id);
	}
}
?>
