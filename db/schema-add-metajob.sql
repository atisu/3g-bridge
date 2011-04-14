ALTER TABLE cg_job
      ADD COLUMN	metajobid	CHAR(36)	NULL,	/* Id referencing a sub-job's parent meta-job. */

      ADD FOREIGN KEY (metajobid) REFERENCES cg_job(id) ON DELETE CASCADE;