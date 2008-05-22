/*
 * Example data
 */

DELETE FROM cg_job;
DELETE FROM cg_inputs;
DELETE FROM cg_outputs;
DELETE FROM cg_algqueue;

ALTER TABLE cg_job AUTO_INCREMENT = 1;
ALTER TABLE cg_inputs AUTO_INCREMENT = 1;
ALTER TABLE cg_outputs AUTO_INCREMENT = 1;
ALTER TABLE cg_algqueue AUTO_INCREMENT = 1;

INSERT INTO cg_job (id, alg, status, args) VALUES ("0ee009fb-ff8f-4950-82b7-84fa5c4bba8a", "cmol3d", "INIT", "-p 1 -a 2");

INSERT INTO cg_inputs(id, localname, path) VALUES ("0ee009fb-ff8f-4950-82b7-84fa5c4bba8a", "INPUT1", "/tmp/INPUT.1");
INSERT INTO cg_inputs(id, localname, path) VALUES ("0ee009fb-ff8f-4950-82b7-84fa5c4bba8a", "INPUT2", "/tmp/INPUT.2");

INSERT INTO cg_outputs(id, localname, path) VALUES ("0ee009fb-ff8f-4950-82b7-84fa5c4bba8a", "OUTPUT1", "/tmp/OUTPUT.1");
INSERT INTO cg_outputs(id, localname, path) VALUES ("0ee009fb-ff8f-4950-82b7-84fa5c4bba8a", "OUTPUT2", "/tmp/OUTPUT.2");

INSERT INTO cg_algqueue(dsttype, alg) VALUES ("dst1", "cmol3d");
