/*
 * Purge CG tables;
 */ 

DELETE FROM cg_job;
DELETE FROM cg_inputs;
DELETE FROM cg_outputs;

ALTER TABLE cg_job AUTO_INCREMENT = 1;
ALTER TABLE cg_inputs AUTO_INCREMENT = 1;
ALTER TABLE cg_outputs AUTO_INCREMENT = 1;
