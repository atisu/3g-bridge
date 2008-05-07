/*
 * Schema description for CG
 */
drop table if exists cg_job;
drop table if exists cg_inputs;
drop table if exists cg_outputs;


/*
 * Job table
 */
create table cg_job (
    id			varchar(254) not null /* Job identifier */,
    args		varchar(254) not null /* Arguments */,
    alg			varchar(254) not null /* Algorithm (executable) name */,
    status		varchar(254) not null /* Job's status */,
    gridid		varchar(254) not null /* Job's grid identifier */,
    property		varchar(254) not null /* Property? */,
    dsttype		varchar(254)	      /* Destination type */,
    dstloc		varchar(254)	      /* Destination location */,
    creation_time	timestamp    null default current_timestamp /* Creation time */,
    primary key (id)
) type=InnoDB;


/*
 * Job input table
 */
create table cg_inputs (
    id                  varchar(254) not null /* Job's identifier */,
    localname           varchar(254) not null /* Basename of the file */,
    path		varchar(254) not null /* Location of the file */,
    primary key (id)
) type=InnoDB;


/*
 * Job output table
 */
create table cg_outputs (
    id                  varchar(254) not null /* Job's identifier */,
    localname           varchar(254) not null /* Basename of the file */,
    path		varchar(254)          /* Expected location of the file */,
    primary key (id)
) type=InnoDB;
