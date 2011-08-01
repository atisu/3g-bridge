drop table if exists log_job_insert;
drop table if exists log_job_finish;
drop trigger if exists tr_log_job_insert;
drop trigger if exists tr_log_job_finish;

create table log_job_insert
(
	id	int		not null auto_increment,
	jobid	char(36)	not null,
	grid	varchar(128)	not null,
	alg	varchar(128)	not null,
	creation_time
		timestamp	not null default current_timestamp,

	primary key (id),
	index(jobid),
	index(grid), index(alg), index(grid, alg),
	index(creation_time)
);

create table log_job_finish
(
	id	int		not null auto_increment,
	jobid	char(36)	not null,
	success	bool		not null,
	finish_time
		timestamp	not null default current_timestamp,

	primary key (id),
	index(jobid),
	index(success),
	index(finish_time)
);

delimiter //

create trigger tr_log_job_insert
       after insert on cg_job
       for each row
       	   insert into log_job_insert
	   	  (jobid, grid, alg)
	   values
		  (NEW.id, NEW.grid, NEW.alg);

//
		  
create trigger tr_log_job_finish
  after update on cg_job
  for each row begin
    if NEW.status <> OLD.status
     and NEW.status in ('FINISHED', 'CANCEL', 'ERROR')
    then
      insert into log_job_finish
       (jobid, success)
      values
       (NEW.id, NEW.status = 'FINISHED');
    end if;
  end;

//