CFLAGS_GLIB=$(shell pkg-config dbus-glib-1 gio-2.0 gio-unix-2.0 --cflags)
LIBS_GLIB=$(shell pkg-config dbus-glib-1 gio-2.0 gio-unix-2.0 --libs)
all: exp1 exp2 exp3

CFLAGS:= -Wall $(CFLAGS) $(CFLAGS_GLIB)
LIBS:= $(LIBS) $(LIBS_GLIB)

exp1: experiment1.o
	$(CC) $(LDFLAGS) -o $@ $< $(LIBS)


exp2: experiment2.o
	$(CC) $(LDFLAGS) -o $@ $< $(LIBS)

geowriter: geowriter.o
	$(CC) $(LDFLAGS) -o $@ $< $(LIBS)

exp3: experiment3.o gio_gpsd.o gio_gpsd.h
	$(CC) $(LDFLAGS) -o $@ $< gio_gpsd.o $(LIBS) -lsqlite3 -lm

gio_gpsd.c gio_gpsd.h: org.gpsd.xml
	gdbus-codegen --generate-c-code gio_gpsd --c-namespace gpsd $<

clean:
	rm -f *.o gio_gpsd.c gio_gpsd.h exp1 exp2 geowriter
experiment3.o: experiment3.c gio_gpsd.h

