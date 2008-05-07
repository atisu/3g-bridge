/*
 * Schema description for CG
 */
drop table if exists cg_job;
drop table if exists cg_inputs;
drop table if exists cg_outputs;

create table cg_job (
    id			varchar(254) not null,
    args		varchar(254) not null,
    alg			varchar(254) not null,
    status		varchar(254) not null,
    wuid		varchar(254) not null,
    property		varchar(254) not null,
    creation_time	timestamp    null default current_timestamp,
    primary key (id)
) type=InnoDB;

create table cg_inputs (
    id                  varchar(254) not null,
    localname           varchar(254) not null,
    path		varchar(254) not null,
    primary key (id)
) type=InnoDB;

create table cg_outputs (
    id                  varchar(254) not null,
    localname           varchar(254) not null,
    path		varchar(254),
    primary key (id)
) type=InnoDB;
