<?php

require_once('common.php');

function condJoin($basestr, $detail) {
	$reply = $basestr;
	if ($detail && $detail != '')
		$reply .= ": {$detail}";
	return $reply;
}

class HTTPException extends Exception {
	public $code;
	public $what;
	public function __construct($code, $what=Null) {
		$this->code = $code;
		$this->what = $what;
	}
	public function render($req) {
		$code = $this->code;
		$msg = condJoin("HTTP {$code}", defval($this->what, ''));
		$t = defval($req, 'html', 'format');

		header('Status: '.$code);
		header('X-PHP-Response-Code: $code', true, $code);
		switch ($t) {
		case 'html':
			header("Content-Type: text/html");
			print "<!DOCTYPE html>\n<html><head><title>3G Bridge Web Interface &endash; Error</title></head><body><h1>{$msg}</h1></body></html>";
			break;
		case 'json':
			header("Content-Type: application/json");
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
		parent::__construct(501, condJoin("Not implemented", $what));
	}
}
class NotSupported extends HTTPException {
	public $allowed;
	public $what;
	public function __construct($what, $allowed) {
		parent::__cosntruct(405, condJoin("Operation not supported", $what));
		$this->allowed = $allowed;
	}
	public function render($req) {
		header("Allow: " . $ns->allowed);
		return parent::render($req);
	}
}

class NotFound extends HTTPException {
	public function __construct($what=Null) {
		parent::__construct(
			404,
			condJoin("Resource, collection or attribute not found",
				 $what));
	}
}

class BadRequest extends HTTPException {
	public function __construct($what) {
		parent::__construct(400, condJoin("Bad request", $what));
	}
}

?>