<?php

function p($x, $y = "")
{
	if ($y) print "{$y}: ";
	print_r($x);
	print "\n";
}

function defval($value, $defval, $selector=Null)
{
	if ($value == Null) return $defval;
	elseif ($selector==Null) return $value;
	else return $value->$selector;
}

?>