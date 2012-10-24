<?php

function p($x, $y = "")
{
	if ($y) print "{$y}: ";
	print_r($x);
	print "\n";
}

function httpcode($code)
{
	header('Status: '.$code);
	header('X-PHP-Response-Code: $code', true, $code);
}

class NotSupported extends Exception {
	public $allowed;
	public function __construct($allowed) {
		$this->allowed = $allowed;
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
		$this->parseIncomingParams();
		$this->format = 'json';
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
			$body_params = json_deconde($body);
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
			break;
		}

		$this->parameters = $parameters;
	}
}

abstract class RESTHandler
{
	public static $handlers = array();
	public static function addHandler($regex, $type)
	{
		array_push(RESTHandler::$handlers, array($regex, $type));
	}
	public static function create($request)
	{
		arsort(RESTHandler::$handlers);
		foreach (RESTHandler::$handlers as $key=>$value)
		{
			if (preg_match('|^'.$value[0].'|',
				       $request->path))
				return new $value[1]($request);
		}

		return FALSE;
	}

	public $request;
		
	private function __construct($request)
	{
		$this->request = $request;
	}

	protected function handleGet() { throw new NotSupported($this->allowed()); }
	protected function handlePost() { throw new NotSupported($this->allowed()); }
	protected function handlePut() { throw new NotSupported($this->allowed()); }
	protected function handleDelete() { throw new NotSupported($this->allowed()); }
	protected function allowed() { return ""; }

	public function handle()
	{
		switch ($this->request->verb)
		{
		case 'GET': return $this->handleGet();
		case 'PUT': return $this->handlePut();
		case 'POST': return $this->handlePost();
		case 'DELETE': return $this->handleDelete();
		default:
			throw new NotSupported($this->allowed());
		}
	}
}

?>