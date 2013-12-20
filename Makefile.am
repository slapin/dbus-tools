AUTOMAKE_OPTIONS = 1.11
ACLOCAL_AMFLAGS = -I m4

LDADD = $(GLIB_LIBS) $(SQLITE_LIBS)
LDFLAGS = -g
AM_CPPFLAGS = -Wall $(GLIB_CFLAGS) $(SQLITE_CFLAGS) -g2 -ggdb
noinst_PROGRAMS = exp1 exp2 exp3
bin_PROGRAMS = geosave modemd

exp1_SOURCES = experiment1.c
exp1_CPPFLAGS = $(DBUS_CFLAGS)
exp1_LDADD = $(DBUS_LIBS)
exp2_SOURCES = experiment2.c
exp2_CPPFLAGS = $(DBUS_CFLAGS)
exp2_LDADD = $(DBUS_LIBS)
exp3_SOURCES = experiment3.c
exp3_LDADD = $(LDADD) -lm
geosave_SOURCES = geosave.c
geosave_LDADD = $(LDADD) -lm
modemd_SOURCES = modemd.c privdata.h modem_power.c modem_power.h \
		 leds.c leds.h connman.c connman.h contexts.c \
		 contexts.h ofono-props.c ofono-props.h debug.c \
		 sms.c debug.h voicecall.c voicecall.h u-boot.c \
		 u-boot.h
modemd_LDADD = $(LDADD) -lm

