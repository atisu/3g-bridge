<?php

class C {
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

	public static function cond_push(&$arr, $value) {
		if ($value) {
			array_push($arr, $value);
		}
	}

	public static function gen_uuid() {
		return sprintf( '%04x%04x-%04x-%04x-%04x-%04x%04x%04x',
				// 32 bits for "time_low"
				mt_rand( 0, 0xffff ), mt_rand( 0, 0xffff ),

				// 16 bits for "time_mid"
				mt_rand( 0, 0xffff ),

				// 16 bits for "time_hi_and_version",
				// four most significant bits holds
				// version number 4
				mt_rand( 0, 0x0fff ) | 0x4000,

				// 16 bits, 8 bits for "clk_seq_hi_res",
				// 8 bits for "clk_seq_low",
				// two most significant bits holds zero and one
				// for variant DCE1.1
				mt_rand( 0, 0x3fff ) | 0x8000,

				// 48 bits for "node"
				mt_rand( 0, 0xffff ),
				mt_rand( 0, 0xffff ),
				mt_rand( 0, 0xffff ));
	}

	public static function hash_dir($id) {
		return preg_replace('/(..)(.*)/', '\1/\1\2', $id);
	}
}
?>
