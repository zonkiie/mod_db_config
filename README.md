# mod_db_config
Read Apache Config from Database
# Warning
If using the apache module, you should know what you are doing!

## Required Libraries
 * unixodbc
 * unixodbc-dev
 * libapr1-dev
 * libaprutil1
 * libaprutil1-dbd-odbc
 * libaprutil1-dbd-sqlite3
 * libaprutil1-dev

## How to compile
 * Build
 
    make
 * Install
 
    make install

## Example apache config file /etc/apache2/mods-available/db_config.conf
### Without a Macro
	DBC_DBDriver "sqlite3"
	DBC_DBDSN "/dev/shm/mydb.sqlite"
	ExecuteSQL "select '<VirtualHost *:80>'||x'0a'||'ServerName ' || vhost_name ||x'0a'||'DocumentRoot ' || target || x'0a'||'</VirtualHost>'||x'0a' as vhostdata from vhosts;"
### With a Macro
	DBC_DBDriver "sqlite3"
	DBC_DBDSN "/dev/shm/mydb.sqlite"

	<Macro Vhost_NonSSL $domain >
	<VirtualHost *:80>
	ServerName $domain
	ServerAlias www.$domain
	DocumentRoot "/var/www/$domain"
	ErrorLog "/var/log/apache2/$domain/error.log"
	CustomLog "/var/log/apache2/$domain/access_log" combined
	</VirtualHost>
	</Macro>

	ExecuteSQL "select 'Use Vhost_NonSSL myexample2.com '||x'0a' as vhostdata;"

The x'0a' is an expression in sqlite to add a line break. Look in the manual of your prefered database how to add a linebreak to the created string if you use other database systems.

## Description
Every Line which contains an ExecuteSQL Command is replaced with the Database Result of this query.
Note that this module does only support one Database Connection.
If you use a Database Server on the same host, please order the startup sequence so that the Database Server is started before the Apache Webserver.
### NOTE:
Because of naming conflicts with mod_dbd, we use here other directives than mod_dbd.
When this module was called mod_db_config while developing, everything worked fine when using the directive DBDriver. After renaming to db_config, I've got segfaults and illegal reads when debugging the module. Then after some sleepless nights, I tried to rename the directive DBDriver to DBC_DBDriver and after another minor bug, everything worked fine again. So the lesson fpr module developer is: Use apache2ctl -L to find out wether your directives are already defined. When you don't see a directive with your designated name, you can choose the name.

## Example DB Create script
see create_db.sh

## Note
I recommend it to use macros. Remember to activate mod_macro.
The old version of mod_db_config with an integrated template engine remains in the archive.

## Hints for independent operation from database server
One possibility is to use a file-based Database System like sqlite. Maybe there are alternatives to sqlite.
You can use postgres with sqlite by using the fdw Wrapper for sqlite. So you can use sqlite from postgres and store your vhost-table in sqlite.

