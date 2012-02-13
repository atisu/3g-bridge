ALTER TABLE cg_job
	ADD COLUMN      metajobid       CHAR(36) NULL,
	ADD FOREIGN KEY (metajobid) REFERENCES cg_job(id)
		ON DELETE CASCADE,
	ADD INDEX idx_cgjob_metajobid (metajobid);

