-- -*- mode: sql; coding: utf-8-unix -*-
-- These indexes are necessary for the MonLog-BOINC plugin to work efficiently.

ALTER TABLE RESULT
      ADD INDEX idx_result_crtime        (create_time),
      ADD INDEX idx_result_fp            (appid, server_state, create_time),
      ADD INDEX idx_result_appid_outcome (appid, outcome);