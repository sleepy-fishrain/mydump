SUBDIRS = common mgmapi ndbapi . kernel mgmsrv mgmclient cw

include $(top_srcdir)/ndb/config/common.mk.am

ndblib_LTLIBRARIES = libndbclient.la

libndbclient_la_SOURCES =

libndbclient_la_LIBADD =  \
              ndbapi/libndbapi.la \
              common/transporter/libtransporter.la   \
              common/debugger/libtrace.la     \
              common/debugger/signaldata/libsignaldataprint.la \
              mgmapi/libmgmapi.la \
              common/mgmcommon/libmgmsrvcommon.la \
              common/logger/liblogger.la       \
              common/portlib/libportlib.la      \
              common/util/libgeneral.la 

windoze-dsp: libndbclient.dsp

libndbclient.dsp: Makefile \
               $(top_srcdir)/ndb/config/win-lib.am \
               $(top_srcdir)/ndb/config/win-name \
               $(top_srcdir)/ndb/config/win-includes \
               $(top_srcdir)/ndb/config/win-sources \
               $(top_srcdir)/ndb/config/win-libraries
	cat $(top_srcdir)/ndb/config/win-lib.am > $@
	@$(top_srcdir)/ndb/config/win-name $@ $(ndblib_LTLIBRARIES)
	@$(top_srcdir)/ndb/config/win-includes $@ $(INCLUDES)
	@$(top_srcdir)/ndb/config/win-sources $@ dummy.cpp
	@$(top_srcdir)/ndb/config/win-libraries $@ LIB $(libndbclient_la_LIBADD)
	@touch dummy.cpp
