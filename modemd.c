#include <gio/gio.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <termios.h>

#include "privdata.h"
#include "modem_power.h"
#include "leds.h"
#include "connman.h"
#include "contexts.h"
#include "modemd.h"
#include "ofono-props.h"
#include "voicecall.h"
#include "debug.h"

struct privdata modemdata;

static GMainLoop *loop = NULL;

const char *gsmledfile = "/sys/class/leds/crux:yellow";
const char *clientledfile = "/sys/class/leds/crux:green";
const char *modemdev = "/dev/ttySAC0";
const char *amppath = "/sys/devices/platform/reg-userspace-consumer.5/state";
int mic_vol = 100;
int spk_vol = 40;
gboolean power_off_modem = 1;
static GOptionEntry opts[] = {
	{"gsmstatusled", 'l', 0, G_OPTION_ARG_STRING, &gsmledfile, "path to GSM status led control file", NULL},
	{"clientstatusled", 'c', 0, G_OPTION_ARG_STRING, &clientledfile, "path to client status led control file", NULL},
	{"amppath", 'a', 0, G_OPTION_ARG_STRING, &amppath, "path to amplifier control regulator", NULL},
	{"micvolume", 'i', 0, G_OPTION_ARG_INT, &mic_vol, "microphone volume (level)", NULL},
	{"spkvolume", 'v', 0, G_OPTION_ARG_INT, &spk_vol, "speaker volume (level)", NULL},
	{"modemdev", 'm', 0, G_OPTION_ARG_STRING, &modemdev, "path to alarm input file", NULL},
	{"poweroff", 'p', 0, G_OPTION_ARG_NONE, &power_off_modem, "power off modem at exit", NULL},
	{NULL},
};

static int cold_start = 0;

static void modem_init_termios(int fd)
{
	struct termios tio;
	memset(&tio, 0, sizeof(tio));
	tio.c_iflag=0;
	tio.c_oflag=0;
	tio.c_cflag=CS8|CREAD|CLOCAL;
	tio.c_lflag=0;
	tio.c_cc[VMIN]=1;
	tio.c_cc[VTIME]=5;
	cfsetospeed(&tio,B115200);
	cfsetispeed(&tio,B115200);
	tcsetattr(fd,TCSANOW,&tio);
}

const char *modem_cmd1 = "AT\r";
const char *modem_cmd2 = "AT+CSMS=0\r";
const char *modem_cmd3 = "AT+CNMI=2,1,2,0,0\r";
const char *modem_cmd4 = "AT+CGCLASS=\"B\"\r";
const char *modem_cmd5 = "AT+CLCC=1\r";
const char *modem_shd_cmd1 = "AT+CPOWD=1\r";

static int do_modem_command_reply(int fd, const char *cmd, const char *reply)
{
	int cmd_done = 0;
	guchar buf[128];
	int c;
	int timeout = 100;
	while (!cmd_done) {
		write(fd, cmd, strlen(cmd));
		for (c = 0; c < 5; c++) {
			g_usleep(300000);
			memset(buf, 0, sizeof(buf));
			read(fd, buf, sizeof(buf) - 1);
			d_info("reply: %s\n", buf);
			if (strstr((char *)buf, reply)) {
				cmd_done = 1;
				d_info("got %s\n", reply);
				break;
			}
		}
		timeout--;
		if (!timeout)
			return -1;
		g_usleep(500000);
	}
	if (cmd_done)
		return 1;
	return 0;
}
static void do_modem_command(int fd, const char *cmd)
{
	int r = do_modem_command_reply(fd, cmd, "OK");
	if (r <= 0) {
		d_info("unable to execute command %s\n", cmd);
		terminate_disable_modem();
	}
}
static void do_modem_command_noreply(int fd, const char *cmd)
{
		guchar buf[128];
		int len = 0, i, j;

		for(i = 0; i < 10; i++) {
			write(fd, cmd, strlen(cmd));
			g_usleep(1000000);
			for (j = 0; j < 10; j++) {
				memset(buf, 0, sizeof(buf));
				len = read(fd, buf, sizeof(buf) - 1);
				if (len > 0)
					break;
				g_usleep(500000);
			}
			if (len > 0)
				break;
		}
		if (len > 0)
			d_info("buf: %s\n", buf);
}

static void modem_init(void)
{
	int fd, i, r, len;
	char buf[256];
	if (!cold_start) {
		d_info("warm start\n");
		return;
	}
	fd = open(modemdev, O_RDWR | O_NONBLOCK);
	if (fd < 0)
		g_error("modem init fault: %s\n", modemdev);
	modem_init_termios(fd);
	d_info("running command: %s\n", modem_cmd1);
	do_modem_command(fd, modem_cmd1);
	d_info("running command: %s\n", modem_cmd2);
	do_modem_command(fd, modem_cmd2);
	d_info("running command: %s\n", modem_cmd3);
	do_modem_command(fd, modem_cmd3);
	d_info("running command: %s\n", modem_cmd4);
	do_modem_command(fd, modem_cmd4);
	d_info("running command: %s\n", modem_cmd5);
	do_modem_command(fd, modem_cmd5);
	while(1) {
		r = do_modem_command_reply(fd, "AT+CCALR?\r", "+CCALR: 1");
		if (r > 0)
			break;
		if (r < 0) {
			d_info("unable to execute command AT+CCALR\n");
			terminate_disable_modem();
		}
		g_usleep(1000000);
	}
	close(fd);
}

static void modem_shutdown(void)
{
	int fd;
	fd = open(modemdev, O_RDWR | O_NONBLOCK);
	if (!fd)
		return; /* Something big happened */
	modem_init_termios(fd);
	d_info("running command: %s\n", modem_shd_cmd1);
	do_modem_command_noreply(fd, modem_shd_cmd1);
	close(fd);
}

static void lockdown_modem(GDBusProxy *proxy, int r)
{
	int tries = 10, g;
	do {
		g = set_proxy_property(proxy, "Lockdown",
			g_variant_new_boolean((r) ? TRUE: FALSE));
		if (g)
			g_usleep(3000000);
	} while (g && tries--);

}

static void recover_modem(GDBusProxy *proxy)
{
	lockdown_modem(proxy, 1);
	modem_shutdown();
	modem_power_off();
	g_usleep(5000000);
	modem_power_on();
	g_usleep(10000000);
	modem_init();
	lockdown_modem(proxy, 0);
	g_usleep(10000000);
}

/* Checking netreg status value */
static void check_netreg_status(struct modemdata *data, const char *status)
{
	if (!g_strcmp0(status, "registered")) {
		data->registered = 1;
		/* As we're registered, we no longer need timeout */
		g_source_remove(data->check_netreg_id);
	} else
		data->registered = 0;
}

static void check_netreg_property(void *data, const char *key, GVariant *value)
{
	struct modemdata *modem = data;
	const char *val;
	d_info("value type: %s\n",
		g_variant_get_type_string(value));
	if (g_strcmp0(key, "Status") == 0) {
		g_variant_get(value, "s", &val);
		d_info("value: %s\n", val);
		check_netreg_status(modem, val);
	}
}

static void netreg_signal_cb(GDBusProxy *netreg, gchar *sender_name,
			      gchar *signal_name, GVariant *parameters,
			      gpointer data)
{
	const char *key;
	GVariant *value;
	struct privdata *priv = data;
	if (g_strcmp0(signal_name, "PropertyChanged") == 0) {
		g_variant_get(parameters, "(sv)", &key, &value);
		d_info("netreg property changed: %s\n", key);
		check_netreg_property(priv, key, value);
		g_variant_unref(value);
	}
}

static void netreg_stuff(struct modemdata *data)
{
	GError *err = NULL;
	data->netreg = g_dbus_proxy_new_for_bus_sync(G_BUS_TYPE_SYSTEM,
			G_DBUS_PROXY_FLAGS_NONE,
			NULL,
			OFONO_SERVICE,
			"/sim900_0", /* FIXME */
			OFONO_NETREG_INTERFACE,
			NULL, &err);
	if (err) {
		g_warning("No netreg interface proxy: %s\n", err->message);
		g_error_free(err);
		return;
	}
	g_signal_connect(data->netreg, "g-signal", G_CALLBACK(netreg_signal_cb), data);
	get_process_props(data->netreg, data, check_netreg_property);
}

static void power_modem(GDBusProxy *proxy, struct modemdata *data)
{
	int att = 10;
	d_info("modem power on\n");
	do {
		int tries = 10, r;
		do {
			r = set_proxy_property(proxy, "Powered", g_variant_new_boolean(TRUE));
			if (r)
				g_usleep(2000000);
		} while (r && tries--);
		if (!tries)
			recover_modem(proxy);
		else
			break;
	} while (att--);
	if (!att) {
		d_notice("unable to power modem up\n");
		terminate_disable_modem();
	} else
		d_debug("done\n");
}

static void terminate_disable_modem_atexit(void)
{
	if (!modem_check_power())
		return;
	d_info("switching modem off and terminating\n");
	system("pkill ofonod"); /* FIXME */
	modem_shutdown();
	modem_power_off();
}
void terminate_disable_modem(void)
{
	if (!modem_check_power())
		return;
	d_info("switching modem off and terminating\n");
	system("pkill ofonod"); /* FIXME */
	modem_shutdown();
	modem_power_off();
	/* We assume here that ofono will be restarted in this case */
	/* And we'll be restarted too */
	g_main_loop_quit(loop);
}
static void online_modem(GDBusProxy *proxy, struct modemdata *data)
{
	d_info("modem online\n");
	set_proxy_property(proxy, "Online", g_variant_new_boolean(TRUE));
	d_debug("done\n");
}

struct have_ifaces {
	int have_connman;
	int have_voice;
	int have_netreg;
};

static void check_if(char *name, char *pcmp, int *val)
{
	if (g_strcmp0(name, pcmp) == 0)
		*val = 1;
}

static gboolean check_modem_connman(gpointer data)
{
	struct modemdata *modem = data;
	if (!modem->have_connman)
		/* Restart ofonod as we run too
		   long without being online
		*/
		d_info("have no ConnectionManager interface for too long\n");
		terminate_disable_modem();
	return FALSE;
}

static char *find_firmware(void)
{
	GError *err = NULL;
	GDir *d = g_dir_open("/lib/firmware", 0, &err);
	const char *name;
	if (err) {
		g_warning("error openning /lib/firmware\n");
		return NULL;
	}
	while ((name = g_dir_read_name(d))) {
		if (strstr(name, ".cla"))
			break;
	}
	g_dir_close(d);
	if (name)
		return g_build_filename("/lib/firmware", name);
	else
		return NULL;
	
}

static void check_modem_property(void *data, const char *key, GVariant *value)
{
	struct modemdata *modem = data;
	d_debug("value type: %s\n",
		g_variant_get_type_string(value));

	if (g_strcmp0(key, "Powered") == 0) {
		g_variant_get(value, "b", &modem->modem_powered);
		d_debug("value data: %d\n", modem->modem_powered);
	} else if (g_strcmp0(key, "Online") == 0) {
		g_variant_get(value, "b", &modem->modem_online);
		d_debug("value data: %d\n", modem->modem_online);
	} else if (g_strcmp0(key, "Interfaces") == 0) {
		GVariantIter *iter;
		char *v;
		struct have_ifaces hif;
		memset(&hif, 0, sizeof(hif));
		g_variant_get(value, "as", &iter);
		while (g_variant_iter_loop (iter, "s", &v)) {
			d_info("Interface: %s\n", v);
			if (g_strcmp0(v, OFONO_CONNMAN_INTERFACE) == 0)
				d_debug("CONNMAN\n");
			check_if(v, OFONO_CONNMAN_INTERFACE, &hif.have_connman);
			check_if(v, OFONO_VOICECALL_INTERFACE, &hif.have_voice);
			check_if(v, OFONO_NETREG_INTERFACE, &hif.have_netreg);
		}
		modem->have_connman = hif.have_connman;
		if (hif.have_netreg)
			netreg_stuff(data);
		if (hif.have_connman) {
			d_info("have_connman: 1\n");
			g_source_remove(modem->modem_connman_id);
			connman_stuff(data);
		}
		g_variant_iter_free(iter);
	} else if (g_strcmp0(key, "Revision") == 0) {
		char *rev;
		const char *fpath = "/lib/firmware/%s.cla";
		char buf[PATH_MAX];
		const char *loader = "/lib/firmware/flash_nor_16bits_hwasic_evp_4902_rel.hex";
		g_variant_get(value, "s", &rev);
		if (!strncmp("Revision:", rev, 9))
			rev += 9;
		d_info("modem firmware revision: %s\n", rev);
		snprintf(buf, sizeof(buf), fpath, rev);
		if (g_file_test(buf, G_FILE_TEST_EXISTS))
			d_info("modem firmware needs no upgrade\n");
		else {
			const char *fw = find_firmware();
			d_info("modem firmware needs upgrade\n");
			d_info("upgrade firmware from %s\n", fw);
			lockdown_modem(modem->modem, 1);
			prepare_modem_reflash();
			snprintf(buf, sizeof(buf),
				"cd /lib/firmware && /usr/bin/simflasher -d /dev/ttySAC0 -s 460800 -f %s -l %s", fw, loader);
			system(buf);
			finish_modem_reflash();
			lockdown_modem(modem->modem, 0);
			terminate_disable_modem();
		}
	}
}
static gboolean check_network_registration(gpointer data)
{
	struct modemdata *modem = data;
	if (!modem->registered)
		/* Restart ofonod as we run too
		   long without registration
		*/
		terminate_disable_modem();
	return FALSE;
}
static gboolean check_modem_online(gpointer data)
{
	struct modemdata *modem = data;
	if (!modem->modem_online)
		/* Restart ofonod as we run too
		   long without being online
		*/
		terminate_disable_modem();
	return FALSE;
}

static void set_properties(GDBusProxy *proxy, struct modemdata *data)
{
	d_debug("setting props\n");
	if (!data->modem_powered) {
		power_modem(proxy, data);
		data->check_netreg_id =
			g_timeout_add_seconds(40,
				check_network_registration, data);
	}
	if (!data->modem_online && data->modem_powered) {
		online_modem(proxy, data);
		data->modem_online_id = g_timeout_add_seconds(120, check_modem_online, data);
	}
	d_debug("all done\n");
}

static void modem_obj_cb(GDBusProxy *proxy, gchar *sender_name,
			 gchar *signal_name, GVariant *parameters,
			 gpointer data)
{
	GVariant *value;
	const char *key;
	struct modemdata *priv = data;
	if (g_strcmp0(signal_name, "PropertyChanged") == 0) {
		g_variant_get(parameters, "(sv)", &key, &value);
		d_info("modem property changed: %s\n", key);
		check_modem_property(priv, key, value);
		g_variant_unref(value);
	}
	set_properties(proxy, priv);
}

static gboolean check_modem_state(gpointer data)
{
	struct modemdata *priv = (struct modemdata *)data;
	set_led_state(priv);
	d_info("registered: %d have_connman: %d failcount: %d gprs_attached: %d\n",
		priv->registered,
		priv->have_connman,
		priv->failcount,
		priv->gprs_attached);
	return TRUE;
}
static gboolean check_connman_powered(gpointer data)
{
	struct modemdata *modem = (struct modemdata *)data;
	if (!modem)
		/* No modem */
		goto out;
	if (!modem->have_connman)
		modem->ip_configured = 0;
	if (!modem->connman) {
		modem->have_connman = 0;
		modem->gprs_attached = 0;
		modem->gprs_powered = 0;
	}

	if (modem->have_connman && (!modem->gprs_attached || !modem->context_active)) {
		modem->ip_configured = 0;
#if 0
		get_process_props(modem->connman, data, check_connman_prop);
#endif
	}
	if (modem->gprs_attached && !modem->context_active) {
		modem->ip_configured = 0;
	}
	if (!modem->gprs_powered && modem->have_connman) {
		modem->ip_configured = 0;
		set_proxy_property(modem->connman, "Powered", g_variant_new_boolean(TRUE));
	}
	if (modem->context_active && !modem->ip_configured) {
		do_ipv4_config();
		do_ipv6_config();
		modem->state = MODEM_GPRS;
		modem->ip_configured = 1;
	}
out:
	return TRUE;
}

static void green_led_control(GDBusConnection *connection,
		       const gchar *sender_name,
		       const gchar *object_path,
		       const gchar *interface_name,
		       const gchar *signal_name,
		       GVariant *parameters,
		       gpointer userdata)
{
	/* Enable/disable green LED here */
	struct modemdata *priv = userdata;

	g_variant_get (parameters, "(b)", &priv->cstate, NULL);
	d_info("green led status:%d\n", priv->cstate);
	if (priv->cstate)
		priv->fatal_count = 0;
	set_cled_state(priv);
}

static void gprs_stall_control(GDBusConnection *connection,
		       const gchar *sender_name,
		       const gchar *object_path,
		       const gchar *interface_name,
		       const gchar *signal_name,
		       GVariant *parameters,
		       gpointer userdata)
{
	struct modemdata *priv = userdata;
	if (priv->in_voicecall)
		return;
	priv->fatal_count++;

	d_info("fatal count: %d\n", priv->fatal_count);
	if (priv->fatal_count >= 2 && priv->gprs_attached) {
		d_notice("terminating: excessive client failures\n");
		terminate_disable_modem();
	}
}
static gboolean reset_fatal(gpointer data)
{
	struct modemdata *priv = data;
	d_info("resetting fatal counter\n");
	priv->fatal_count = 0;
	return TRUE;
}


static void add_modem(struct privdata *priv, const char *path)
{
	GError *err = NULL;
	struct modemdata *modem = g_hash_table_lookup(priv->modem_hash, path);
	if (modem)
		return;
	modem = g_try_new0(struct modemdata, 1);
	if (!modem)
		return;
	modem->path = g_strdup(path);
	modem->priv = priv;
	g_hash_table_insert(priv->modem_hash, g_strdup(path), modem);
	modem_power_on();
	modem_init();
	modem->state = MODEM_INIT;
	modem->modem = g_dbus_proxy_new_for_bus_sync(G_BUS_TYPE_SYSTEM, G_DBUS_PROXY_FLAGS_NONE,
				  NULL, OFONO_SERVICE, path, OFONO_MODEM_INTERFACE,
				  NULL, &err);
	if (err) {
		g_warning("No Modem proxy: %s", err->message);
		g_error_free(err);
		return;
	}
	modem->modem_connman_id = g_timeout_add_seconds(10, check_modem_connman, modem);
	modem->check_modem_id = g_timeout_add_seconds(3, check_modem_state, modem);
	modem->check_connman_id = g_timeout_add_seconds(6, check_connman_powered, modem);
	g_timeout_add_seconds(480, reset_fatal, modem);
	g_signal_connect(modem->modem, "g-signal", G_CALLBACK (modem_obj_cb), modem);
	get_process_props(modem->modem, modem, check_modem_property);
	set_properties(modem->modem, modem);
	g_dbus_connection_signal_subscribe(priv->conn, NULL, "ru.itetra.Connectivity", "status", NULL, NULL, G_DBUS_SIGNAL_FLAGS_NONE,
		green_led_control, modem, NULL);
	g_dbus_connection_signal_subscribe(priv->conn, NULL, "ru.itetra.Connectivity", "fatal", NULL, NULL, G_DBUS_SIGNAL_FLAGS_NONE,
		gprs_stall_control, modem, NULL);
	voicecall_init(priv->conn, modem);
}
static void removed_modem(struct privdata *priv, const char *path)
{
	struct modemdata *modem = g_hash_table_lookup(priv->modem_hash, path);
	if (!modem)
		return;
	if (modem->check_modem_id) {
		g_source_remove(modem->check_modem_id);
		g_source_remove(modem->check_connman_id);
	}
	g_hash_table_remove(priv->modem_hash, path);
}
static void remove_modem(gpointer data)
{
	struct modemdata *d = data;
	g_free(d->path);
	g_free(d);
}

static void manager_signal_cb(GDBusProxy *mgr, gchar *sender_name,
			      gchar *signal_name, GVariant *parameters,
			      gpointer data)
{
	const char *obj_path;
	if (g_strcmp0 (signal_name, "ModemAdded") == 0) {
		g_variant_get (parameters, "(oa{sv})", &obj_path, NULL);
		d_info("Modem added: %s\n", obj_path);
		add_modem(data, obj_path);
	} else if (g_strcmp0 (signal_name, "ModemRemoved") == 0) {
		g_variant_get (parameters, "(o)", &obj_path);
		d_info("Modem removed: %s\n", obj_path);
		removed_modem(data, obj_path);
		modem_shutdown();
		modem_power_off();
	}
}


static void ofono_connect(GDBusConnection *conn,
			  const gchar *name,
			  const gchar *name_owner,
			  gpointer user_data)
{
	GError *err = NULL;
	struct privdata *priv = &modemdata;
	GVariantIter *iter;
	GVariant *modems;
	char *obj_path;
	err = NULL;

	priv->modem_hash = g_hash_table_new_full(g_str_hash, g_str_equal,
						g_free, remove_modem);
	if (priv->modem_hash == NULL)
		return;
	priv->context_hash = g_hash_table_new_full(g_str_hash, g_str_equal,
						g_free, NULL);
	if (priv->context_hash == NULL) {
		g_hash_table_destroy(priv->modem_hash);
		return;
	}

	priv->mgr = g_dbus_proxy_new_sync(conn,
			G_DBUS_PROXY_FLAGS_NONE,
			NULL,
			OFONO_SERVICE,
			OFONO_MANAGER_PATH,
			OFONO_MANAGER_INTERFACE,
			NULL,
			&err);
	if (err)
		g_error("No ofono proxy: %s\n", err->message);
	g_signal_connect(priv->mgr, "g-signal", G_CALLBACK(manager_signal_cb), priv);
	err = NULL;
	modems = g_dbus_proxy_call_sync(priv->mgr, "GetModems", NULL, G_DBUS_CALL_FLAGS_NONE,
			-1, NULL, &err);
	if (err)
		g_error("can't get list of modems %s\n", err->message);
	g_variant_get (modems, "(a(oa{sv}))", &iter);
	while (g_variant_iter_loop (iter, "(oa{sv})", &obj_path, NULL)) {
		d_info("modem: %s\n", obj_path);
			add_modem(priv, obj_path);
	}
	g_variant_iter_free(iter);
	g_variant_unref(modems);

}
static void ofono_disconnect(GDBusConnection *conn,
			     const gchar *name,
			     gpointer user_data)
{
	g_main_loop_quit(loop);
}

static guint watch;
int main(int argc, char *argv[])
{
	struct privdata *priv = &modemdata;
	GError *err = NULL;
	GOptionContext *ocontext;

	ocontext = g_option_context_new("- modem control tool");
	/* Command line options stuff */
	g_option_context_add_main_entries(ocontext, opts, "modemd");
	if (!g_option_context_parse (ocontext, &argc, &argv, &err))
		g_error("option parsing failed: %s\n", err->message);

	g_type_init();
	gpio_init();
	cold_start = !modem_check_power();
	debug_init();
	if (power_off_modem)
		g_atexit(terminate_disable_modem_atexit);
	loop = g_main_loop_new(NULL, FALSE);
	priv->conn = g_bus_get_sync(G_BUS_TYPE_SYSTEM, NULL, &err);
	if (!priv->conn) {
		g_printerr("Error connecting to D-Bus: %s\n", err->message);
		g_error_free(err);
		return 1;
	}
	watch = g_bus_watch_name(G_BUS_TYPE_SYSTEM, OFONO_SERVICE,
				 G_BUS_NAME_WATCHER_FLAGS_NONE,
				 ofono_connect, ofono_disconnect, NULL, NULL);
	if (!watch)
		return 1;

	
	g_main_loop_run(loop);
	return 0;
}

