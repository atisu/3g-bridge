<?php
header("Content-Type: text/plain");

function curPageURL() {
	$pageURL = 'http';
	if ($_SERVER["HTTPS"] == "on") {$pageURL .= "s";}
	$pageURL .= "://";
	if ($_SERVER["SERVER_PORT"] != "80") {
		$pageURL .= $_SERVER["SERVER_NAME"].":".$_SERVER["SERVER_PORT"].$_SERVER["REQUEST_URI"];
	} else {
		$pageURL .= $_SERVER["SERVER_NAME"].$_SERVER["REQUEST_URI"];
	}
	return $pageURL;
 }
function getResname()
{
	$parsedUrl=parse_url(curPageURL());
	$path=$parsedUrl['path'];
	$scriptname=$_SERVER["SCRIPT_NAME"];
	$dirname=dirname($scriptname);
	$resname =
		ereg_replace("^${dirname}(.*)$", '\\1',
			     ereg_replace("^${scriptname}(.*)$", "\\1",
					  $path));
	if (FALSE)
	{
		p($path, "Path");
		p($scriptname, "Script name");
		p(preg_quote($dirname), "Dir name");
		//header('X-PHP-Response-Code: 501', true, 501);
		exit(0);
	}
	if ($resname[0] == '/')
		return substr($resname, 1);
	else
		return $resname;
}
function p($x, $y = "")
{
	if ($y) print "{$y}: ";
	print_r($x);
	print "\n";
}

$resourceName=explode('/', $_SERVER['PATH_INFO']);
p($_SERVER['REQUEST_METHOD'], "Method");
p($_GET, "GET");
p($_POST, "POST");
p($_FILES, "FILES");
p($_SERVER, "SERVER");
p($resourceName, "Resource name");
#$config_file='/home/avisegradi/Inst/etc/3g-bridge.conf';
#p($config_file, "Config file");
#$cfg=parse_ini_file($config_file, true, INI_SCANNER_RAW);
#p($cfg['database'], "DB");
#p($cfg['wssubmitter'], "WSSubmitter");
#p($cfg, "Config");
?>

