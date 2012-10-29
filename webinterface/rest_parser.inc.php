<?php

class RESTParser {
	private static $handlers=array();
	public static function register_handler($content_type, $handler_type) {
		RESTParser::$handlers[$content_type] = $handler_type;
	}
	public static function create($request) {
		$content_type='plain';
		if (isset($_SERVER['CONTENT_TYPE']))
			$content_type = preg_replace('|([^;]+);.*|', '\\1', $_SERVER['CONTENT_TYPE']);
		if (array_key_exists($content_type, RESTParser::$handlers))
			return new RESTParser::$handlers[$content_type]($request);
		else
			throw new NotImplemented("request format: '{$content_type}'");
	}
	
	public function __construct($request) {
		$this->request = $request;		
	}
	public function parse() {
		return array();
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
RESTParser::register_handler('application/json', 'JSONParser');
RESTParser::register_handler('application/x-www-form-urlencoded',
			     'FormPostParser');
RESTParser::register_handler('multipart/form-data', 'FormPostParser');


?>
