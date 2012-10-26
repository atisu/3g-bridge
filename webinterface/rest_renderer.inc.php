<?php

require_once('common.inc.php');
require_once('error.inc.php');

abstract class RESTRenderer {
	private static $handlers=array();
	public static function register_handler($content_type, $handler_type) {
		RESTRenderer::$handlers[$content_type] = $handler_type;
	}
	public static function create($request) {
		$format=$request->format;
		if (array_key_exists($format, RESTRenderer::$handlers))
			return new RESTRenderer::$handlers[$format]($request);
		else
			throw new NotImplemented("output format: '{$format}'");
	}

	public function __construct($request) {
		$this->request = $request;
	}

	public function render_header($handler) {
		$this->first = TRUE;
		return $this->render_header_i($handler);
	}
	public function render_footer() {
		$this->render_footer_i();
	}
	public function render_dataitem($data, $id=Null) {
		$retval = $this->render_dataitem_i($data, $id);
		if ($this->first) $this->first = FALSE;
		return $retval;
	}

	protected abstract function render_header_i($handler);
	protected abstract function render_footer_i();
	protected abstract function render_dataitem_i($data, $id=Null);
}
class HTMLRenderer extends RESTRenderer {
	public function __construct($request) {
		parent::__construct($request);
	}

	protected function render_header_i($handler) {
		header("Content-Type: text/html");
		print C::$HTML_HEADER;
		print "<body>\n";
		$handler->render_html_form();
		print '<table id="data">';
	}
	protected function render_footer_i() {
		print "</table></body></html>\n";
	}
	protected function render_dataitem_i($data, $id=Null) {
		// Print header if needed
		if ($this->first and $this->request->header) {
			print '<tr class="header"';
			if ($id) print " id=\"{$id}\"";
			print '>';
			foreach ($data as $key=>$value)
				print "<th id=\"{$key}\">{$key}</th>";
			print "</tr>\n";
		}

		//Print dataitem
		print '<tr class="dataitem"';
		if ($id) print " id=\"{$id}\"";
		print '>';
		foreach ($data as $key=>$value)
			print "<td id=\"{$key}\">{$value}</td>";
		print "</tr>\n";
	}
}
class JSONRenderer extends RESTRenderer {
	public function __construct($request) {
		parent::__construct($request);
	}

	protected function render_header_i($handler) {
		header("Content-Type: application/json");
		print '[';
	}
	protected function render_footer_i() {
		print ']';
	}
	protected function render_dataitem_i($data, $id=Null) {
		if (!$this->first) print ', ';
		print json_encode($data);
	}
}
class PlainRenderer extends RESTRenderer {
	public function __construct($request) {
		parent::__construct($request);
		$this->sep = $this->request->field_separator;
		$this->rsep = $this->request->record_separator;
	}

	protected function render_header_i($handler) {
		header("Content-Type: text/plain");
	}
	protected function render_footer_i() {
		/* NOOP */
	}
	protected function render_dataitem_i($data, $id=Null) {
		$sep = $this->sep;
		// Separate dataitems
		if (!$this->first)
			print $this->rsep;
		// Print header if needed
		elseif ($this->request->header) {
			$f = TRUE;
			foreach ($data as $key=>$value) {
				if ($f) $f=FALSE; else print $sep;
				print $key;
			}
			print "\n";
		}

		//Print dataitem
		$f = TRUE;
		foreach ($data as $value) {
			if ($f) $f=FALSE; else print $sep;
			print $value;
		}
	}
}

RESTRenderer::register_handler('', 'PlainRenderer');
RESTRenderer::register_handler('plain', 'PlainRenderer');
RESTRenderer::register_handler('json', 'JSONRenderer');
RESTRenderer::register_handler('html', 'HTMLRenderer');

?>
