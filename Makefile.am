AUTOMAKE_OPTIONS = 1.11
ACLOCAL_AMFLAGS = -I m4

LDADD = $(GLIB_LIBS) $(SQLITE_LIBS)
AM_CPPFLAGS = $(GLIB_CFLAGS) $(SQLITE_CFLAGS)
noinst_PROGRAMS = exp1 exp2 exp3
bin_PROGRAMS = geosave

exp1_SOURCES = experiment1.c
exp1_CPPFLAGS = $(DBUS_CFLAGS)
exp1_LDADD = $(DBUS_LIBS)
exp2_SOURCES = experiment2.c
exp2_CPPFLAGS = $(DBUS_CFLAGS)
exp2_LDADD = $(DBUS_LIBS)
exp3_SOURCES = experiment3.c gio_gpsd.h
exp3_LDADD = $(LDADD) -lm
geosave_SOURCES = geosave.c
geosave_LDADD = $(LDADD) -lm

#exp1: experiment1.o
#	$(CC) $(LDFLAGS) -o $@ $< $(LIBS)


#exp2: experiment2.o
#	$(CC) $(LDFLAGS) -o $@ $< $(LIBS)

#geowriter: geowriter.o
#	$(CC) $(LDFLAGS) -o $@ $< $(LIBS)

#exp3: experiment3.o gio_gpsd.o gio_gpsd.h
#	$(CC) $(LDFLAGS) -o $@ $< gio_gpsd.o $(LIBS) -lsqlite3 -lm

gio_gpsd.c gio_gpsd.h: org.gpsd.xml
	gdbus-codegen --generate-c-code gio_gpsd --c-namespace gpsd $<

#clean:
#	rm -f *.o gio_gpsd.c gio_gpsd.h exp1 exp2 geowriter
#experiment3.o: experiment3.c gio_gpsd.h

