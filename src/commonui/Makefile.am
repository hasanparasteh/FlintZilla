AUTOMAKE_OPTIONS = subdir-objects

lib_LTLIBRARIES = libfzclient-commonui-private.la

libfzclient_commonui_private_la_CPPFLAGS = -I$(top_builddir)/config
libfzclient_commonui_private_la_CPPFLAGS += $(LIBFILEZILLA_CFLAGS)
libfzclient_commonui_private_la_CPPFLAGS += -DBUILDING_FZ_COMMONUI

libfzclient_commonui_private_la_CXXFLAGS = -fvisibility=hidden
libfzclient_commonui_private_la_LDFLAGS = -no-undefined -release $(PACKAGE_VERSION_MAJOR).$(PACKAGE_VERSION_MINOR).$(PACKAGE_VERSION_MICRO)
libfzclient_commonui_private_la_LDFLAGS += ../engine/libfzclient-private.la $(LIBFILEZILLA_LIBS) $(PUGIXML_LIBS)

libfzclient_commonui_private_la_DEPENDENCIES = ../engine/libfzclient-private.la

libfzclient_commonui_private_la_SOURCES = \
	buildinfo.cpp \
	cert_store.cpp \
	chmod_data.cpp \
	file_utils.cpp \
	filter.cpp \
	fz_paths.cpp \
	ipcmutex.cpp \
	local_recursive_operation.cpp \
	login_manager.cpp \
	remote_recursive_operation.cpp \
	options.cpp \
	site.cpp \
	site_manager.cpp \
	updater.cpp \
	updater_cert.cpp \
	xml_cert_store.cpp \
	xml_file.cpp

noinst_HEADERS = \
	buildinfo.h \
	cert_store.h \
	chmod_data.h \
	file_utils.h \
	filter.h \
	fz_paths.h \
	ipcmutex.h \
	local_recursive_operation.h \
	login_manager.h \
	options.h \
	recursive_operation.h \
	remote_recursive_operation.h \
	registry.h \
	site.h \
	site_manager.h \
	updater.h \
	updater_cert.h \
	visibility.h \
	xml_cert_store.h \
	xml_file.h

if MINGW
libfzclient_commonui_private_la_SOURCES += \
	registry.cpp

libfzclient_commonui_private_la_LDFLAGS += -lole32 -Wl,-luuid
endif

if MACAPPBUNDLE

noinst_LTLIBRARIES = libfzclient-commonui-private-mac.la

libfzclient_commonui_private_mac_la_CPPFLAGS = -I$(top_builddir)/config
libfzclient_commonui_private_mac_la_CPPFLAGS += $(LIBFILEZILLA_CFLAGS)
libfzclient_commonui_private_mac_la_CPPFLAGS += -DBUILDING_FZ_COMMONUI

libfzclient_commonui_private_mac_la_CXXFLAGS = -fvisibility=hidden

libfzclient_commonui_private_mac_la_SOURCES = \
	fz_paths_osx.mm

libfzclient_commonui_private_mac_la_LDFLAGS = -framework Cocoa

libfzclient_commonui_private_la_LIBADD = libfzclient-commonui-private-mac.la
libfzclient_commonui_private_la_DEPENDENCIES += libfzclient-commonui-private-mac.la

endif
