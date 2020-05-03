mod_db_config.la: mod_db_config.slo
	$(SH_LINK) -rpath $(libexecdir) -module -avoid-version  mod_db_config.lo
DISTCLEAN_TARGETS = modules.mk
shared =  mod_db_config.la
