create table accounting_info_subjobs (
  metajobid   char(36),
  id          char(36),
  start_time  int(11),
  stop_time   int(11),
  wallclock_time
              bigint(12),
  cpu_time    double,
  memory      double,
  host_flops  double,
  host_intops double,
  host_ncpus  int(11),
  credit      double,

  index(metajobid),
  foreign key (metajobid) references cg_job(id) on delete cascade
) ENGINE=InnoDB;

create view accounting_info_nonmetajob as
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
    h.p_ncpus       "host_ncpus",
    r.granted_credit "credit"
  from
    cg_job j
    join workunit w on w.name=concat(j.gridid, "_", j.id)
    join result r on r.id = w.canonical_resultid
    join host h on h.id = r.hostid
  where
    j.grid <> 'Metajob';
create view accounting_info_metajob as
  select
    metajobid        "id",
    min(start_time)  "start_time",
    max(stop_time)   "stop_time",
    max(stop_time) - min(start_time)
                     "wallclock_time",
    sum(cpu_time)    "cpu_time",
    sum(memory)      "memory",
    sum(cpu_time*host_flops)/sum(cpu_time)
                     "host_flops",
    sum(cpu_time*host_intops)/sum(cpu_time)
                     "host_intops",
    sum(host_ncpus)  "host_ncpus",
    sum(credit)      "credit"
  from accounting_info_subjobs
  group by metajobid;

\d//
create trigger save_subjob_accounting_info
before delete on cg_job
for each row begin
  if old.metajobid is not null then
    insert into accounting_info_subjobs
      select old.metajobid, a.* from accounting_info_nonmetajob a where id=old.id;
  end if;
end//
\d;

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
