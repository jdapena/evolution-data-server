noinst_LTLIBRARIES = libclient-test-utils.la

libclient_test_utils_la_SOURCES = client-test-utils.c client-test-utils.h

libclient_test_utils_la_CPPFLAGS =				\
	$(AM_CPPFLAGS)						\
	-I$(top_srcdir)						\
	-I$(top_builddir)					\
	-I$(top_srcdir)/addressbook     			\
	-I$(top_builddir)/addressbook   			\
	-I$(top_srcdir)/calendar				\
	-I$(top_builddir)/calendar				\
	-I$(top_srcdir)/tests/test-server-utils     		\
	-I$(top_builddir)/tests/test-server-utils   		\
	-DSRCDIR=\""$(abs_srcdir)"\"				\
	$(EVOLUTION_CALENDAR_CFLAGS)				\
	$(CAMEL_CFLAGS)						\
	$(NULL)

libclient_test_utils_la_LIBADD = 				\
	$(top_builddir)/calendar/libecal/libecal-1.2.la		\
	$(top_builddir)/tests/test-server-utils/libetestserverutils.la	\
	$(EVOLUTION_CALENDAR_LIBS)				\
	$(CAMEL_LIBS)						\
	$(NULL)

# ordered by relative complexity
TESTS = 					\
	test-client-refresh			\
	test-client-get-free-busy		\
	test-client-add-timezone		\
	test-client-create-object		\
	test-client-remove-object		\
	test-client-get-object-list		\
	test-client-modify-object		\
	test-client-get-revision		\
	test-client-send-objects		\
	test-client-receive-objects		\
        test-client-bulk-methods		\
	test-client-get-attachment-uris		\
	test-client-get-view			\
	test-client-revision-view		\
	$(NULL)

# The test program
noinst_PROGRAMS = $(TESTS)	\
	test-client-open	\
	test-client-examine			\
	test-client-stress-views		\
	test-client-stress-factory--serial	\
	test-client-stress-factory--fifo	\
	test-client-stress-factory--single-cal	\
	$(NULL)

TEST_CPPFLAGS=					\
	$(libclient_test_utils_la_CPPFLAGS)	\
	$(NULL)

TEST_LIBS =					\
	$(libclient_test_utils_la_LIBADD)	\
	libclient-test-utils.la			\
	$(NULL)

# ordered alphanumerically
test_client_add_timezone_LDADD=$(TEST_LIBS)
test_client_add_timezone_CPPFLAGS=$(TEST_CPPFLAGS)
test_client_create_object_LDADD=$(TEST_LIBS)
test_client_create_object_CPPFLAGS=$(TEST_CPPFLAGS)
test_client_examine_LDADD=$(TEST_LIBS)
test_client_examine_CPPFLAGS=$(TEST_CPPFLAGS)
test_client_get_attachment_uris_LDADD=$(TEST_LIBS)
test_client_get_attachment_uris_CPPFLAGS=$(TEST_CPPFLAGS)
test_client_get_free_busy_LDADD=$(TEST_LIBS)
test_client_get_free_busy_CPPFLAGS=$(TEST_CPPFLAGS)
test_client_get_object_list_LDADD=$(TEST_LIBS)
test_client_get_object_list_CPPFLAGS=$(TEST_CPPFLAGS)
test_client_get_view_LDADD=$(TEST_LIBS)
test_client_get_view_CPPFLAGS=$(TEST_CPPFLAGS)
test_client_revision_view_LDADD=$(TEST_LIBS)
test_client_revision_view_CPPFLAGS=$(TEST_CPPFLAGS)
test_client_modify_object_LDADD=$(TEST_LIBS)
test_client_modify_object_CPPFLAGS=$(TEST_CPPFLAGS)
test_client_get_revision_LDADD=$(TEST_LIBS)
test_client_get_revision_CPPFLAGS=$(TEST_CPPFLAGS)
test_client_open_LDADD=$(TEST_LIBS)
test_client_open_CPPFLAGS=$(TEST_CPPFLAGS)
test_client_receive_objects_LDADD=$(TEST_LIBS)
test_client_receive_objects_CPPFLAGS=$(TEST_CPPFLAGS)
test_client_bulk_methods_LDADD=$(TEST_LIBS)
test_client_bulk_methods_CPPFLAGS=$(TEST_CPPFLAGS)
test_client_refresh_LDADD=$(TEST_LIBS)
test_client_refresh_CPPFLAGS=$(TEST_CPPFLAGS)
test_client_remove_object_LDADD=$(TEST_LIBS)
test_client_remove_object_CPPFLAGS=$(TEST_CPPFLAGS)
test_client_send_objects_LDADD=$(TEST_LIBS)
test_client_send_objects_CPPFLAGS=$(TEST_CPPFLAGS)
test_client_stress_factory__fifo_LDADD=$(TEST_LIBS)
test_client_stress_factory__fifo_CPPFLAGS=$(TEST_CPPFLAGS)
test_client_stress_factory__serial_LDADD=$(TEST_LIBS)
test_client_stress_factory__serial_CPPFLAGS=$(TEST_CPPFLAGS)
test_client_stress_factory__single_cal_LDADD=$(TEST_LIBS)
test_client_stress_factory__single_cal_CPPFLAGS=$(TEST_CPPFLAGS)
test_client_stress_views_LDADD=$(TEST_LIBS)
test_client_stress_views_CPPFLAGS=$(TEST_CPPFLAGS)

-include $(top_srcdir)/git.mk
