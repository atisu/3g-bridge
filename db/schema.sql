/*
 * Schema description for CancerGrid job database
 */
DROP TABLE IF EXISTS cg_inputs;
DROP TABLE IF EXISTS cg_outputs;
DROP TABLE IF EXISTS cg_download;
DROP TABLE IF EXISTS cg_job;
DROP TABLE IF EXISTS cg_algqueue;

/*
 * Algorithm queue table
 */
CREATE TABLE cg_algqueue (
	grid		VARCHAR(128)	NOT NULL,	/* Relevant grid name */
	alg		VARCHAR(128)	NOT NULL,	/* Algorithm (executable) name */
	batchsize	INT		NOT NULL,	/* Maximum batch size */
	statistics	TEXT,				/* Statistics data */
	PRIMARY KEY entry (grid, alg)
) TYPE=InnoDB;

/*
 * Job table
 */
CREATE TABLE cg_job (
	id		CHAR(36)	NOT NULL,	/* Unique job id, generated by the submitter */
	grid		VARCHAR(128)	NOT NULL,	/* Target grid name */
	alg		VARCHAR(128)	NOT NULL,	/* Algorithm (executable) name */
	status		CHAR(10)	NOT NULL,	/* Status of job [PREPARE, INIT, RUNNING, FINISHED, ERROR, CANCEL] */
	gridid		VARCHAR(254),			/* Job's grid identifier */
	args		VARCHAR(254),			/* Command-line arguments to be passed to the algorithm at job execution */
	creation_time	TIMESTAMP	NULL DEFAULT CURRENT_TIMESTAMP,	/* Creation time of job descriptor */
	PRIMARY KEY (id),
	INDEX (grid, alg),
	FOREIGN KEY (grid) REFERENCES cg_algqueue(grid) ON DELETE RESTRICT
) TYPE=InnoDB;


/*
 * Job input table
 */
CREATE TABLE cg_inputs (
	id		CHAR(36)	NOT NULL,	/* Unique id of job, foreign key of job.id */
	localname	VARCHAR(254)	NOT NULL,	/* Basename of the file */
	path		VARCHAR(254)	NOT NULL,	/* Absolute path of the file */
	PRIMARY KEY ENTRY (id, localname),
	FOREIGN KEY (id) REFERENCES cg_job(id) ON DELETE CASCADE
) TYPE=InnoDB;


/*
 * Job output table
 */
CREATE TABLE cg_outputs (
	id		CHAR(36)	NOT NULL,	/* Job's identifier */
	localname	VARCHAR(254)	NOT NULL,	/* Basename of the file */
	path		VARCHAR(254)	NOT NULL,	/* Expected absolute path of the file */
	PRIMARY KEY ENTRY (id, localname),
	FOREIGN KEY (id) REFERENCES cg_job(id) ON DELETE CASCADE
) TYPE=InnoDB;


/*
 * Download manager state. Used by the web service frontend
 */
CREATE TABLE cg_download (
	jobid		CHAR(36)	NOT NULL,	/* Job identifier */
	localname	VARCHAR(254)	NOT NULL,	/* Name of the input file */
	url		VARCHAR(254)	NOT NULL,	/* Remote URL */
	next_try	TIMESTAMP	NOT NULL DEFAULT CURRENT_TIMESTAMP,	/* Next download attempt */
	retries		INT		NOT NULL DEFAULT 0,			/* No. of failed DL attempts */
	PRIMARY KEY entry (jobid, localname),
	FOREIGN KEY (jobid) REFERENCES cg_job(id) ON DELETE CASCADE
) TYPE=InnoDB;
