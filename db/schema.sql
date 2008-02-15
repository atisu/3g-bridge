/*
 * Schema description for CG
 */


create table cg_job (
    id                  integer     not null auto_increment,
    name                varchar(254) not null,
    args		varchar(254) not null,
    algname		varchar(254) not null,
    primary key (id)
) type=InnoDB;

create table cg_inputs (
    id                  integer     not null auto_increment,
    localname           varchar(254) not null,
    path		varchar(254) not null,
    jobid		integer,
    primary key (id)
) type=InnoDB;

create table cg_outputs (
    id                  integer     not null auto_increment,
    localname           varchar(254) not null,
    path		varchar(254),
    jobid		integer,
    primary key (id)
) type=InnoDB;
