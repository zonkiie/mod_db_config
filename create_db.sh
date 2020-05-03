#!/bin/sh
cat << ENDOFSQL | sqlite3 /etc/apache2/vhosts.sqlite
drop table if exists vhosts;
create table vhosts(id integer primary key autoincrement, vhost_name varchar(255), target varchar(255));
insert into vhosts(vhost_name, target) values('example1.com', '/home/example/htdocs');
ENDOFSQL
