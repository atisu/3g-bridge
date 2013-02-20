create view accouting_info as
	select id
	from cg_job, workunit;


-- cg_job --[?]-- workunit 
--                        \__[workunitid]__workunit
--                                                |
--                                host__[hostid]__/
--
-- start time ? result.sent_time
-- stop time  ? result.received_time
-- wallclock = stop time - start time
-- cpu time   ? result.cpu_time
-- memory     ? workunit.rsc_memory_bound
-- benchmark values
--            ? host.p_fpops / host.p_iops
-- ncpus      host.p_ncpus
