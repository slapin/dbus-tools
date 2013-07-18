#include <gio/gio.h>
#include <sqlite3.h>
#include <stdint.h>
#include <string.h>
#include <math.h>
#include <unistd.h>
#include <fcntl.h>
#include <linux/input.h>
#include <stdlib.h>

static GMainLoop *loop = NULL;
static GIOChannel *alarmdev;
struct dbconn {
	const char *path;
	const char *create_sql;
	const char *insert_sql;
	sqlite3_stmt *insert;
	sqlite3 *db;
};

static struct dbconn dbraw = {
	.path = "/var/db/rawgeo.db",
	.create_sql = "CREATE TABLE IF NOT EXISTS geo(time INTEGER PRIMARY KEY UNIQUE NOT NULL, \
		     latitude INTEGER NOT NULL, \
		     longitude INTEGER NOT NULL, \
		     speed INTEGER NOT NULL, \
		     heading INTEGER NOT NULL, \
		     gdop INTEGER NOT NULL);",
	.insert_sql = "INSERT INTO geo VALUES(?,?,?,?,?,?)",
};
static struct dbconn dbgeo = {
	.path = "/var/db/geo.db",
	.create_sql = "CREATE TABLE IF NOT EXISTS geo(time INTEGER PRIMARY KEY UNIQUE NOT NULL, \
		     latitude INTEGER NOT NULL, \
		     longitude INTEGER NOT NULL, \
		     speed INTEGER NOT NULL, \
		     heading INTEGER NOT NULL, \
		     gdop INTEGER NOT NULL);",
	.insert_sql = "INSERT INTO geo VALUES(?,?,?,?,?,?)",
};
static struct dbconn dbgnss = {
	.path = "/var/db/gnss.db",
	.create_sql = "CREATE TABLE IF NOT EXISTS status(time INTEGER PRIMARY KEY UNIQUE NOT NULL, \
		     value INTEGER NOT NULL);",
	.insert_sql = "INSERT INTO status VALUES(?,?)",
};
static struct dbconn dbpwr = {
	.path = "/var/db/power.db",
	.create_sql = "CREATE TABLE IF NOT EXISTS power(time INTEGER PRIMARY KEY UNIQUE NOT NULL, \
		     value INTEGER NOT NULL);",
	.insert_sql = "INSERT INTO power VALUES(?,?)",
};
static struct dbconn dbalarm = {
	.path = "/var/db/alarm.db",
	.create_sql = "CREATE TABLE IF NOT EXISTS alarm(time INTEGER PRIMARY KEY UNIQUE NOT NULL, \
			source INTEGER NOT NULL,   	\
			gtime INTEGER NOT NULL,    	\
			latitude INTEGER NOT NULL, 	\
			longitude INTEGER NOT NULL,	\
			speed INTEGER NOT NULL,		\
			heading INTEGER NOT NULL,	\
			gdop INTEGER NOT NULL);",
	.insert_sql = "INSERT INTO alarm VALUES(?,?,?,?,?,?,?,?)",
};
static struct dbconn dbfuel = {
	.path = "/var/db/channels.db",
	.create_sql = "CREATE TABLE IF NOT EXISTS channels(time INTEGER PRIMARY KEY UNIQUE NOT NULL, \
			addr INTEGER NOT NULL,   	\
			temp INTEGER NOT NULL,    	\
			level INTEGER NOT NULL, 	\
			freq INTEGER NOT NULL,		\
			status INTEGER NOT NULL);",
	.insert_sql = "INSERT INTO channels VALUES(?,?,?,?,?,?)",
};



struct statusdata {
	uint32_t time;
	uint32_t mode;
	uint32_t sol;
	uint32_t value;
	uint32_t ant;
	uint32_t lastval;
};

static struct statusdata status;

const char *ledfile = "/sys/class/leds/crux:red/brightness";
const char *alarmfile = "/dev/input/event1";

static GOptionEntry opts[] = {
	{"ledpath", 'l', 0, G_OPTION_ARG_STRING, &ledfile, "path to led control file", NULL},
	{"alarmpath", 'a', 0, G_OPTION_ARG_STRING, &alarmfile, "path to alarm input file", NULL},
	{NULL},
};

struct geodata {
	uint32_t time;
	double lat;
	double lon;
	double speed;
	double head;
	uint32_t gdop; /* ??? */
	int mode;
	uint32_t dblat;
	uint32_t dblon;
	uint32_t dbspeed;
	uint32_t dbhead;
};
static struct geodata gd;
static int ledfd;

static void convert_db(void)
{
	gd.dblat = (int)(gd.lat * 1e7);
	gd.dblon = (int)(gd.lon * 1e7);
	gd.dbhead = (int)round(gd.head);
	gd.dbspeed = (gd.speed > 30000.0) ? 0 : (int)gd.speed;
}

static void blink(float duration)
{
	int fd = ledfd;
	if (duration > 0) {
		char l;
		read(fd, &l, sizeof(l));
		if (l == '0') {
			write(fd, "1", 1);
			usleep((int)(0.15 * 1000000.0));
		}
		write(fd, "0", 1);
		fsync(fd);
		usleep((int)(duration * 1000000.0));
		write(fd, "1", 1);
		fsync(fd);
	} else {
		write(fd, "0", 1);
		fsync(fd);
	}
}

static void fix_signal(GDBusConnection *connection,
		       const gchar *sender_name,
		       const gchar *object_path,
		       const gchar *interface_name,
		       const gchar *signal_name,
		       GVariant *parameters,
		       gpointer userdata)
{
	GVariantIter *iter;
	if (g_variant_is_of_type (parameters, G_VARIANT_TYPE ("(a{sv})"))) {
            char *str;
	    GVariant *v;
	    g_variant_get(parameters, "(a{sv})", &iter);
	    memset(&gd, 0, sizeof(gd));
            while(g_variant_iter_loop(iter, "(sv)", &str, &v)) {
		if (!strcmp(str, "lat"))
			g_variant_get(v, "d", &gd.lat);
		else if (!strcmp(str, "lon"))
			g_variant_get(v, "d", &gd.lon);
		else if (!strcmp(str, "mode"))
			g_variant_get(v, "i", &gd.mode);
		else if (!strcmp(str, "gdop"))
			g_variant_get(v, "u", &gd.gdop);
		else if (!strcmp(str, "time"))
			g_variant_get(v, "u", &gd.time);
		else if (!strcmp(str, "head"))
			g_variant_get(v, "d", &gd.head);
		else if (!strcmp(str, "speed"))
			g_variant_get(v, "d", &gd.speed);
		else
			g_print("DAMN!!! %s\n", str);
	    }
            convert_db();
	    // g_print("lat:%lf, lon %lf, mode: %d\n", gd.lat, gd.lon, gd.mode);
	    g_variant_iter_free(iter);
	    blink(0.3);
	} else
	    g_print("params are: %s\n",
		g_variant_get_type_string (parameters));
}

static void telemetry_signal(GDBusConnection *connection,
			     const gchar *sender_name,
			     const gchar *object_path,
			     const gchar *interface_name,
			     const gchar *signal_name,
			     GVariant *parameters,
			     gpointer userdata)
{
	if (g_variant_is_of_type (parameters, G_VARIANT_TYPE ("(a{sv})"))) {
            char *str;
	    GVariant *v;
	    int i;
	    GVariantIter *iter;
	    i = 0;
	    status.mode = 0;
	    g_variant_get(parameters, "(a{sv})", &iter);
            while(g_variant_iter_loop(iter, "(sv)", &str, &v)) {
		if (!strcmp(str, "mode"))
			g_variant_get(v, "i", &status.mode);
	    }
	    status.ant = 1;
	    status.value = 1;
            if (status.mode == 1)
		status.sol = 0;
	    else if (status.mode > 1)
		status.sol = 1;
	    if (status.sol)
		status.value |= 0x02;
	    if (status.ant)
		status.value |= 0x04;
	    status.time = time(NULL);
	    g_variant_iter_free(iter);
	} else
	    g_print("params are: %s\n",
		g_variant_get_type_string (parameters));
}

static void satellites_signal(GDBusConnection *connection,
			     const gchar *sender_name,
			     const gchar *object_path,
			     const gchar *interface_name,
			     const gchar *signal_name,
			     GVariant *parameters,
			     gpointer userdata)
{
	if (g_variant_is_of_type (parameters, G_VARIANT_TYPE ("(a{sv})"))) {
#if 0
            char *str;
	    GVariant *v;
	    int i;
	    GVariantIter *iter;
	    i = 0;
	    g_variant_get(parameters, "(a{sv})", &iter);
            while(g_variant_iter_loop(iter, "(sv)", &str, &v)) {
		switch(i) {
		case 0:
		case 1:
		case 2:
		case 3:
		case 4:
		case 5:
		case 6:
		case 7:
		case 8:
		case 9:
		case 10:
			g_print("%s: %s\n", str, g_variant_get_type_string(v));
			break;
		default:
			g_print("dunno\n");
			break;
		}
		i++;
	    }
	    g_variant_iter_free(iter);
#endif
	} else
	    g_print("params are: %s\n",
		g_variant_get_type_string (parameters));
	if (time(NULL) - gd.time > 20) {
		blink(0.03);
		usleep((int)(0.03 * 1000000.0));
		blink(0.03);
		usleep((int)(0.03 * 1000000.0));
	}
}
static int oldstate;
static int voltage;
static gboolean update_voltage(gpointer userdata)
{
	int fd, l;
	char buf[64];
	const char *path = "/sys/class/power_supply/tps6501x_bat/voltage_now";
	fd = open(path, O_RDONLY);
	if (fd > 0) {
		memset(buf, 0, sizeof(buf));
		l = read(fd, buf, sizeof(buf));
		if (l > 0)
			voltage = atoi(buf);
		close(fd);
		g_print("voltage = %d\n", voltage);
	}
	return TRUE;
}

static int db_do_step(struct dbconn *data)
{
	int ret;
	while((ret = sqlite3_step(data->insert)) == SQLITE_BUSY)
		usleep(2000000);
	if (ret != SQLITE_DONE && ret != SQLITE_CONSTRAINT && ret != SQLITE_BUSY && ret != SQLITE_LOCKED)
		g_error("execute statement :( = %d\n", ret);
	if (ret == SQLITE_CONSTRAINT)
		g_print("failed to insert\n");
	sqlite3_reset(data->insert);
	return ret;
}

static void battery_signal(GDBusConnection *connection,
			     const gchar *sender_name,
			     const gchar *object_path,
			     const gchar *interface_name,
			     const gchar *signal_name,
			     GVariant *parameters,
			     gpointer userdata)
{
	int ret;
	struct dbconn *data = userdata;
	g_print("battery\n");
	if (g_variant_is_of_type (parameters, G_VARIANT_TYPE ("(i)"))) {
	    int state, volcode, voldata;
	    g_print("eek!\n");
	    g_variant_get(parameters, "(i)", &state);
	    if (oldstate != state) {
		sqlite3_bind_int(data->insert, 1, (int32_t)time(NULL));
		volcode = voltage * 0x3ff / 5000000;
		voldata = state | ((volcode & 0x3ff) << 16);
		sqlite3_bind_int(data->insert, 2, (int32_t)voldata);
		db_do_step(data);
		oldstate = state;
	    }
	} else
	    g_print("params are: %s\n",
		g_variant_get_type_string (parameters));
}

static void insert_alarm(struct dbconn *db)
{
	int ret;

	g_print("Alarm!!!\n");
	sqlite3_bind_int(db->insert, 1, (int32_t)time(NULL));
	sqlite3_bind_int(db->insert, 2, 1);
	sqlite3_bind_int(db->insert, 3, gd.time);
	sqlite3_bind_int(db->insert, 4, gd.dblat);
	sqlite3_bind_int(db->insert, 5, gd.dblon);
	sqlite3_bind_int(db->insert, 6, gd.dbspeed);
	sqlite3_bind_int(db->insert, 7, gd.dbhead);
	sqlite3_bind_int(db->insert, 8, gd.gdop);
	db_do_step(db);
}

static gboolean watch_alarm(GIOChannel *s, GIOCondition c, gpointer data)
{
	struct input_event ev;
	gsize len;
	GError *error;
	int rstatus;
	struct dbconn *db = data;
	rstatus = g_io_channel_read_chars(s, (gchar *)&ev, sizeof(ev), &len, &error);
	if (rstatus == G_IO_STATUS_NORMAL && len == sizeof(ev)) {
		if (ev.type == 1 && ev.value == 1 && ev.code == 256)
			insert_alarm(db);
		
	} else {
		g_printerr("invalid read of %d bytes\n", len);
		if (rstatus == G_IO_STATUS_ERROR) {
			g_printerr("error occured: %s\n", error->message);
			g_error_free(error);
		}
	}
	return TRUE;
}


static void insert_signal(GDBusConnection *connection,
			     const gchar *sender_name,
			     const gchar *object_path,
			     const gchar *interface_name,
			     const gchar *signal_name,
			     GVariant *parameters,
			     gpointer userdata)
{
	g_print("insert\n");
	if (g_variant_is_of_type (parameters, G_VARIANT_TYPE ("(sav)"))) {
            char *str;
	    GVariant *v;
	    GVariantIter *iter;
	    g_print("eek!\n");
	    g_variant_get(parameters, "(sav)", &str, &iter);
            while(g_variant_iter_loop(iter, "v", &v)) {
		g_print("%s: %s\n", str, g_variant_get_type_string(v));
	    }
	    g_variant_iter_free(iter);
	} else
	    g_print("params are: %s\n",
		g_variant_get_type_string (parameters));
}

static int fuel_can_insert = 1;
static void fuel_signal(GDBusConnection *connection,
			     const gchar *sender_name,
			     const gchar *object_path,
			     const gchar *interface_name,
			     const gchar *signal_name,
			     GVariant *parameters,
			     gpointer userdata)
{
	struct dbconn *data = userdata;
	guint32 d1, d2, d3, d4, d5;
	g_print("fuel\n");
	if (g_variant_is_of_type (parameters, G_VARIANT_TYPE ("(uuuuu)"))) {
	    g_print("eek!\n");
	    g_variant_get(parameters, "(uuuuu)", &d1, &d2, &d3, &d4, &d5);
	    if (fuel_can_insert) {
		int ret;
		sqlite3_bind_int(data->insert, 1, (int32_t)time(NULL));
		sqlite3_bind_int(data->insert, 2, (int32_t)d1);
		sqlite3_bind_int(data->insert, 3, (int32_t)d2);
		sqlite3_bind_int(data->insert, 4, (int32_t)d3);
		sqlite3_bind_int(data->insert, 5, (int32_t)d4);
		sqlite3_bind_int(data->insert, 6, (int32_t)d5);
		db_do_step(data);
	    }
	    if (d5 == 2)
		fuel_can_insert = 0;
	    else
		fuel_can_insert = 1;
	} else
	    g_print("params are: %s\n",
		g_variant_get_type_string (parameters));
}



static void do_create_db(struct dbconn *data)
{
	char *serror;
	int ret;
	ret = sqlite3_exec(data->db, data->create_sql, NULL, 0, &serror);
	if (ret != SQLITE_OK) {
		g_error("database creation error: %s\n", serror);
		sqlite3_free(serror);
	}
}

static gboolean geo_write(gpointer userdata)
{
	int ret;
	struct dbconn *data = userdata;
	if (gd.gdop >= 9)
		goto out;
	sqlite3_bind_int(data->insert, 1, (int32_t)gd.time);
	sqlite3_bind_int(data->insert, 2, (int32_t)gd.dblat);
	sqlite3_bind_int(data->insert, 3, (int32_t)gd.dblon);
	sqlite3_bind_int(data->insert, 4, (int32_t)gd.dbspeed);
	sqlite3_bind_int(data->insert, 5, (int32_t)gd.dbhead);
	sqlite3_bind_int(data->insert, 6, (int32_t)gd.gdop);
	db_do_step(data);
out:
	return TRUE;
}
static gboolean gnss_write(gpointer userdata)
{
	int ret;
	struct dbconn *data = userdata;
	if (status.lastval == status.value)
		goto out;
	sqlite3_bind_int(data->insert, 1, (int32_t)status.time);
	sqlite3_bind_int(data->insert, 2, (int32_t)status.value);
	db_do_step(data);
	status.lastval = status.value;
out:
	return TRUE;
}
static gboolean geo_freemem(gpointer userdata)
{
	struct dbconn *data = userdata;
	sqlite3_db_release_memory(data->db);
	return TRUE;
}


static void create_db(struct dbconn *data)
{
	const char *ins = data->insert_sql;
	int ret;

	ret = sqlite3_open(data->path, &data->db);
	if (data->db)
		do_create_db(data);
	else {
		unlink(data->path);
		ret = sqlite3_open(data->path, &data->db);
		if (data->db)
			do_create_db(data);
		else
			g_error("unable to open/create database\n");
	}
	ret = sqlite3_prepare_v2(data->db, data->insert_sql, strlen(ins) + 1, &data->insert, NULL);
	if (ret != SQLITE_OK)
		g_error("failed to prepare statement (%s, %s) %d\n", data->path, data->insert_sql, ret);
}

int main(int argc, char *argv[])
{
	GError *error = NULL;
	GDBusConnection *conn;
	GOptionContext *ocontext;
	GFileMonitor *mon;
	ocontext = g_option_context_new("- insert geodata from D-Bus into databases");

	/* Command line options stuff */
	g_option_context_add_main_entries(ocontext, opts, "exp3");
	// g_option_context_add_group(ocontext, gtk_get_option_group(TRUE));
	if (!g_option_context_parse (ocontext, &argc, &argv, &error))
		g_error("option parsing failed: %s\n", error->message);

	g_type_init();
	ledfd = open(ledfile, O_RDWR|O_APPEND);
	if (ledfd < 2)
		g_error("Can't open LED sysfs entry\n");
	loop = g_main_loop_new (NULL, FALSE);
	conn = g_bus_get_sync(G_BUS_TYPE_SYSTEM, NULL, &error);
	if (!conn) {
		g_printerr("Error connecting to D-Bus: %s\n", error->message);
		g_error_free (error);
		return 1;
	}
	create_db(&dbraw);
	create_db(&dbgeo);
	create_db(&dbgnss);
	create_db(&dbpwr);
	create_db(&dbalarm);
	create_db(&dbfuel);
	g_timeout_add_seconds(3, geo_write, &dbraw);
	g_timeout_add_seconds(10, geo_write, &dbgeo);
	g_timeout_add_seconds(30, geo_freemem, &dbraw);
	g_timeout_add_seconds(300, geo_freemem, &dbgeo);
	g_timeout_add_seconds(1, gnss_write, &dbgnss);
	g_timeout_add_seconds(120, update_voltage, &dbpwr);
	g_dbus_connection_signal_subscribe(conn, NULL, "org.gpsd", "fix", "/org/gpsd", NULL, G_DBUS_SIGNAL_FLAGS_NONE,
		fix_signal, NULL, NULL);
	g_dbus_connection_signal_subscribe(conn, NULL, "org.gpsd", "telemetry", "/org/gpsd", NULL, G_DBUS_SIGNAL_FLAGS_NONE,
		telemetry_signal, NULL, NULL);
	g_dbus_connection_signal_subscribe(conn, NULL, "org.gpsd", "satellites", "/org/gpsd", NULL, G_DBUS_SIGNAL_FLAGS_NONE,
		satellites_signal, NULL, NULL);
	g_dbus_connection_signal_subscribe(conn, NULL, "ru.itetra.Database", "battery", "/", NULL, G_DBUS_SIGNAL_FLAGS_NONE,
		battery_signal, &dbpwr, NULL);
	g_dbus_connection_signal_subscribe(conn, NULL, "ru.itetra.Database", "insert", "/", NULL, G_DBUS_SIGNAL_FLAGS_NONE,
		insert_signal, NULL, NULL);
	g_dbus_connection_signal_subscribe(conn, NULL, "ru.itetra.lls.data", "data", "/ru/itetra/lls/data", NULL, G_DBUS_SIGNAL_FLAGS_NONE,
		fuel_signal, &dbfuel, NULL);
	update_voltage(NULL);
	alarmdev = g_io_channel_new_file(alarmfile, "r", &error);
	if (!alarmdev) {
		g_error("can't open %s: %s\n", alarmfile, error->message);
		g_error_free(error);
		return 1;
	}
	g_io_channel_set_encoding(alarmdev, NULL, &error);
	g_io_add_watch(alarmdev, G_IO_IN, watch_alarm, &dbalarm);

	g_print("All stuffed, working\n");
	g_main_loop_run(loop);
	sqlite3_close(dbraw.db);
	sqlite3_close(dbgeo.db);
	sqlite3_close(dbgnss.db);
	return 0;
}

