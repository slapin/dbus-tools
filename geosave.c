#include <gio/gio.h>
#include <sqlite3.h>
#include <stdint.h>
#include <string.h>
#include <math.h>
#include <unistd.h>
#include <fcntl.h>

static GMainLoop *loop = NULL;
struct dbconn {
	sqlite3_stmt *insert;
	sqlite3 *db;
};

struct statusdata {
	uint32_t time;
	uint32_t mode;
	uint32_t sol;
	uint32_t value;
	uint32_t ant;
	uint32_t lastval;
};

static struct dbconn dbraw, dbgeo, dbgnss, dbpwr;
static struct statusdata status;

const char *ledfile = "/sys/class/leds/crux:red/brightness";

static GOptionEntry opts[] = {
	{"ledpath", 'l', 0, G_OPTION_ARG_STRING, &ledfile, "path to led control file", NULL},
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
		ret = sqlite3_step(data->insert);
		if (ret != SQLITE_DONE && ret != SQLITE_CONSTRAINT)
			g_error("execute statement :( = %d\n", ret);
		if (ret == SQLITE_CONSTRAINT)
			g_print("failed to insert (time = %d)\n", gd.time);
		sqlite3_reset(data->insert);
		oldstate = state;
	    }
	} else
	    g_print("params are: %s\n",
		g_variant_get_type_string (parameters));
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
	if (g_variant_is_of_type (parameters, G_VARIANT_TYPE ("(a{sv})"))) {
            char *str;
	    GVariant *v;
	    int i;
	    GVariantIter *iter;
	    g_print("eek!\n");
	    i = 0;
	    g_variant_get(parameters, "(a{sv})", &iter);
            while(g_variant_iter_loop(iter, "(sv)", &str, &v)) {
		if (!strcmp(str, "mode"))
			g_variant_get(v, "i", &status.mode);
			
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
	} else
	    g_print("params are: %s\n",
		g_variant_get_type_string (parameters));
}

static void fuel_signal(GDBusConnection *connection,
			     const gchar *sender_name,
			     const gchar *object_path,
			     const gchar *interface_name,
			     const gchar *signal_name,
			     GVariant *parameters,
			     gpointer userdata)
{
	g_print("fuel\n");
	if (g_variant_is_of_type (parameters, G_VARIANT_TYPE ("(a{sv})"))) {
            char *str;
	    GVariant *v;
	    int i;
	    GVariantIter *iter;
	    g_print("eek!\n");
	    i = 0;
	    g_variant_get(parameters, "(a{sv})", &iter);
            while(g_variant_iter_loop(iter, "(sv)", &str, &v)) {
		if (!strcmp(str, "mode"))
			g_variant_get(v, "i", &status.mode);
			
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
	} else
	    g_print("params are: %s\n",
		g_variant_get_type_string (parameters));
}



static void do_create_geo_db(sqlite3 *db)
{
	char *serror;
	int ret;
	const char *sql = "CREATE TABLE IF NOT EXISTS geo(time INTEGER PRIMARY KEY UNIQUE NOT NULL, \
		     latitude INTEGER NOT NULL, \
		     longitude INTEGER NOT NULL, \
		     speed INTEGER NOT NULL, \
		     heading INTEGER NOT NULL, \
		     gdop INTEGER NOT NULL);";
	ret = sqlite3_exec(db, sql, NULL, 0, &serror);
	if (ret != SQLITE_OK) {
		g_error("database creation error: %s\n", serror);
		sqlite3_free(serror);
	}
}

static void do_create_status_db(sqlite3 *db)
{
	char *serror;
	int ret;
	const char *sql = "CREATE TABLE IF NOT EXISTS status(time INTEGER PRIMARY KEY UNIQUE NOT NULL, \
		     value INTEGER NOT NULL);";
	ret = sqlite3_exec(db, sql, NULL, 0, &serror);
	if (ret != SQLITE_OK) {
		g_error("database creation error: %s\n", serror);
		sqlite3_free(serror);
	}
}

static void do_create_power_db(sqlite3 *db)
{
	char *serror;
	int ret;
	const char *sql = "CREATE TABLE IF NOT EXISTS power(time INTEGER PRIMARY KEY UNIQUE NOT NULL, \
		     value INTEGER NOT NULL);";
	ret = sqlite3_exec(db, sql, NULL, 0, &serror);
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
	ret = sqlite3_step(data->insert);
	if (ret != SQLITE_DONE && ret != SQLITE_CONSTRAINT)
		g_error("execute statement :( = %d\n", ret);
	if (ret == SQLITE_CONSTRAINT)
		g_print("failed to insert (time = %d)\n", gd.time);
	sqlite3_reset(data->insert);
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
	ret = sqlite3_step(data->insert);
	if (ret != SQLITE_DONE && ret != SQLITE_CONSTRAINT)
		g_error("execute statement :( = %d\n", ret);
	if (ret == SQLITE_CONSTRAINT)
		g_print("failed to insert (time = %d)\n", gd.time);
	sqlite3_reset(data->insert);
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

static void create_geo_db(struct dbconn *data, char *path)
{
	const char *ins = "INSERT INTO geo VALUES(?,?,?,?,?,?)";
	int ret;

	ret = sqlite3_open(path, &data->db);
	if (data->db)
		do_create_geo_db(data->db);
	else {
		unlink(path);
		ret = sqlite3_open(path, &data->db);
		if (data->db)
			do_create_geo_db(data->db);
		else
			g_error("unable to open/create database\n");
	}
	ret = sqlite3_prepare_v2(data->db, ins, strlen(ins) + 1, &data->insert, NULL);
	if (ret != SQLITE_OK)
		g_error("failed to prepare statement (%s, %s) %d\n", path, ins, ret);
}
static void create_status_db(struct dbconn *data, char *path)
{
	const char *ins = "INSERT INTO status VALUES(?,?)";
	int ret;

	ret = sqlite3_open(path, &data->db);
	if (data->db)
		do_create_status_db(data->db);
	else {
		unlink(path);
		ret = sqlite3_open(path, &data->db);
		if (data->db)
			do_create_status_db(data->db);
		else
			g_error("unable to open/create database\n");
	}
	ret = sqlite3_prepare_v2(data->db, ins, strlen(ins) + 1, &data->insert, NULL);
	if (ret != SQLITE_OK)
		g_error("failed to prepare statement (%s, %s) %d\n", path, ins, ret);
}

static void create_power_db(struct dbconn *data, char *path)
{
	const char *ins = "INSERT INTO power VALUES(?,?)";
	int ret;

	ret = sqlite3_open(path, &data->db);
	if (data->db)
		do_create_power_db(data->db);
	else {
		unlink(path);
		ret = sqlite3_open(path, &data->db);
		if (data->db)
			do_create_power_db(data->db);
		else
			g_error("unable to open/create database\n");
	}
	ret = sqlite3_prepare_v2(data->db, ins, strlen(ins) + 1, &data->insert, NULL);
	if (ret != SQLITE_OK)
		g_error("failed to prepare statement (%s, %s) %d\n", path, ins, ret);
}


int main(int argc, char *argv[])
{
	GError *error = NULL;
	GDBusConnection *conn;
	GOptionContext *ocontext;
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
	create_geo_db(&dbraw, "/var/db/rawgeo.db");
	create_geo_db(&dbgeo, "/var/db/geo.db");
	create_status_db(&dbgnss, "/var/db/gnss.db");
	create_power_db(&dbpwr, "/var/db/power.db");
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
	g_dbus_connection_signal_subscribe(conn, NULL, "ru.itetra.lls.data", "data", "/", NULL, G_DBUS_SIGNAL_FLAGS_NONE,
		fuel_signal, NULL, NULL);
	update_voltage(NULL);
	g_print("All stuffed, working\n");
	g_main_loop_run(loop);
	sqlite3_close(dbraw.db);
	sqlite3_close(dbgeo.db);
	sqlite3_close(dbgnss.db);
	return 0;
}

