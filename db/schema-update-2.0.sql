ALTER TABLE cg_job
    ADD COLUMN  userid varchar(256),
    ADD COLUMN  error_info varchar(2048),
    ADD INDEX   idx_cgjob_userid (userid);

