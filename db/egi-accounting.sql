create view accounting_info as
  select
    j.id            "id",
    r.sent_time     "start_time",
    r.received_time "stop_time",
    r.received_time-r.sent_time
                    "wallclock_time",
    r.cpu_time      "cpu_time",
    w.rsc_memory_bound
                    "memory",
    h.p_fpops       "host_flops",
    h.p_iops        "host_intops",
    h.p_ncpus       "host_ncpus"
  from
    cg_job j
    join workunit w on w.name=j.gridid
    join result r on r.id = w.canonical_resultid
    join host h on h.id = r.hostid;


-- cg_job --[gridid<>name]-- workunit 
--                           |
--                            \__[workunitid]__workunit
--                                                    |
--                     result__[canonical_resultid]__/
--                     |
--                      \__[hostid]__host
--
-- start time ? result.sent_time
-- stop time  ? result.received_time
-- wallclock = stop time - start time
-- cpu time   ? result.cpu_time
-- memory     ? workunit.rsc_memory_bound
-- benchmark values
--            ? host.p_fpops / host.p_iops
-- ncpus      host.p_ncpus
