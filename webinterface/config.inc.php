<?php

// BASE_URL must point to the REST interface itself.
// This setting is used for redirection.
define('BASE_URL', 'https://<project_url>/download/wss');
// The path of the 3G Bridge condfiguration.
// You should probably leave this as is, and create a hard link to the
// file. This way, it should work no matter how Apache is configured.
define('CONFIG_FILE', '3g-bridge.conf');
// Command used to query the version of the Bridge used.
// Make sure it executes the binary being used.
define('BRIDGE_VER_CMD',
       '/bin/bash -c /usr/sbin/3g-bridge\ -V');
// Destination grid for incoming meta-jobs.
define('METAJOB_GRID', 'Metajob');

?>