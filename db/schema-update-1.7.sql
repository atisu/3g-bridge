ALTER TABLE cg_job DROP FOREIGN KEY cg_job_ibfk_1;
ALTER TABLE cg_job ADD FOREIGN KEY (grid, alg) REFERENCES cg_algqueue(grid, alg) ON DELETE RESTRICT;