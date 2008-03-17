/*
 * Example data
 */

DELETE FROM cg_job;
DELETE FROM cg_inputs;
DELETE FROM cg_outputs;

ALTER TABLE cg_job AUTO_INCREMENT = 1;
ALTER TABLE cg_inputs AUTO_INCREMENT = 1;
ALTER TABLE cg_outputs AUTO_INCREMENT = 1;

INSERT INTO cg_job (name, args, algname) VALUES ("Test Job 1", "-p 1 -a 2", "flexmol", "CG_INIT", "");


INSERT INTO cg_inputs(localname, path, jobid) VALUES ("INPUT1", "/tmp/INPUT.1", 1);
INSERT INTO cg_inputs(localname, path, jobid) VALUES ("INPUT2", "/tmp/INPUT.2", 1);

INSERT INTO cg_outputs(localname, path, jobid) VALUES ("OUTPUT1", NULL, 1);
INSERT INTO cg_outputs(localname, path, jobid) VALUES ("OUTPUT2", NULL, 1);
