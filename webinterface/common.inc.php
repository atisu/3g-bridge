<?php

class C {

	public static $HTML_HEADER= <<<EOF
<!DOCTYPE html>
<html>
<head>
	<title>3G Bridge Webinterface</title>
	<link rel="stylesheet" type="text/css" href="wss.css" />
</head>
EOF;

	public static function p($x, $y = "") {
		if ($y) print "{$y}: ";
		print_r($x);
		print "\n";
	}

	public static function defval($value, $defval, $selector=Null) {
		if ($value == Null) return $defval;
		elseif ($selector==Null) return $value;
		else return $value->$selector;
	}

	public static function cond_join($basestr, $detail) {
		$reply = $basestr;
		if ($detail && $detail != '')
			$reply .= ": {$detail}";
		return $reply;
	}
}
?>
