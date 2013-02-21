<?php

require_once('common.inc.php');
require_once('rest_base.inc.php');

class HTTPException extends Exception {
	public $code;
	public $what;

	public function __construct($code, $what=Null) {
		$this->code = $code;
		$this->what = $what;
	}
	public function render() {
		$code = $this->code;
		$msg = C::cond_join("HTTP {$code}",
				    C::defval($this->what, ''));
		$t = RESTRequest::get_output_format();

		header('Status: '.$code);
		header('X-PHP-Response-Code: $code', true, $code);
		switch ($t) {
		case 'html':
			header("Content-Type: text/html");
			print C::$HTML_HEADER
				. "<body><h1>{$msg}</h1></body></html>";
			break;
		case 'json':
			header("Content-Type: application/json");
			print json_encode(array('error'=>$msg));
			break;
		case 'plain':
		default:
			header("Content-Type: text/plain");
			print $msg."\n";
			break;
		}
	}
}

class NotImplemented extends HTTPException {
	public function __construct($what) {
		parent::__construct(501, C::cond_join("Not implemented",
						      $what));
	}
}
class NotSupported extends HTTPException {
	public $allowed;
	public $what;
	public function __construct($what, $allowed) {
		parent::__construct(405, C::cond_join("Operation not supported",
						      $what));
		$this->allowed = $allowed;
	}
	public function render() {
		header("Allow: " . $ns->allowed);
		return parent::render();
	}
}

class NotFound extends HTTPException {
	public function __construct($what=Null) {
		parent::__construct(
			404,
			C::cond_join(
				"Resource, collection or attribute not found",
				$what));
	}
}
class AuthorizationError extends HTTPException {
	public function __construct($what=Null) {
		parent::__construct(403, C::cond_join("Not authorized",
						      $what));
	}
}
class BadRequest extends HTTPException {
	public function __construct($what=Null) {
		parent::__construct(400, C::cond_join("Bad request",
						      $what));
	}
}
class ServerError extends HTTPException {
	public function __construct($what) {
		parent::__construct(500, C::cond_join("Server error",
						      $what));
	}
}
class ConfigError extends ServerError {
	public function __construct($key) {
		parent::__construct("Server configuration error ({$key})");
	}
}

class DBError extends ServerError {
	public function __construct($what) {
		parent::__construct(C::cond_join("Database error",
						 $what));
	}
}
class UnknownError extends ServerError {
	public function __construct($what) {
		parent::__construct(C::cond_join("Unknown error",
						 $what));
	}
}

// For unhandled exceptions
function final_handler($ex) {
	ob_end_flush();
	$httpex = new UnknownError($ex);
	$httpex->render();
	exit(1);
}

?>