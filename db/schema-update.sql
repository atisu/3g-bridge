ALTER TABLE cg_job
	ADD COLUMN	griddata	VARCHAR(2048),
	CHANGE COLUMN	gridid gridid	VARCHAR(2048);

ALTER TABLE cg_inputs
	CHANGE COLUMN	path path	VARCHAR(2048);

ALTER TABLE cg_outputs
	CHANGE COLUMN	path path	VARCHAR(2048);
