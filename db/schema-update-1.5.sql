ALTER TABLE cg_job
	ADD COLUMN	griddata	VARCHAR(2048),
	ADD COLUMN	tag		VARCHAR(254),
	CHANGE COLUMN	gridid gridid	VARCHAR(2048),
	ADD INDEX idx_cgjob_griddata (griddata);

ALTER TABLE cg_inputs
	ADD COLUMN	md5		CHAR(32),
	ADD COLUMN	filesize	INT,
	CHANGE COLUMN	path url	VARCHAR(2048);

ALTER TABLE cg_outputs
	CHANGE COLUMN	path path	VARCHAR(2048);

ALTER TABLE cg_download
	CHANGE COLUMN	url url		VARCHAR(2048);

CREATE TABLE IF NOT EXISTS cg_env (
	id		CHAR(36)	NOT NULL,
	name		VARCHAR(254)	NOT NULL,
	val		VARCHAR(254)	NOT NULL,
	PRIMARY KEY ENTRY (id, name),
	FOREIGN KEY (id) REFERENCES cg_job(id) ON DELETE CASCADE
) ENGINE=InnoDB;
