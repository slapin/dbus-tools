#include <gio/gio.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

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
	modem_power_off();
	g_usleep(5000000);
	modem_power_on();
	g_usleep(10000000);
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
		modem_power_off();
		/* We assume here that ofono will be restarted in this case */
		system("pkill ofonod"); /* FIXME */
		/* And we'll be restarted too */
		g_main_loop_quit(loop);
	}
	d_debug("done\n");
}

void terminate_disable_modem(void)
{
	modem_power_off();
	/* We assume here that ofono will be restarted in this case */
	system("pkill ofonod"); /* FIXME */
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
		if (hif.have_connman)
			connman_stuff(data);
		g_variant_iter_free(iter);
	}
}
static gboolean check_network_registration(gpointer data)
{
	struct modemdata *modem = data;
	if (!modem->registered) {
		/* Restart ofonod as we run too
		   long without registration
		*/
		d_info("no network registration for too long, exiting");
		system("pkill ofonod");
		/* Quit */
		modem_power_off();
		g_main_loop_quit(loop);
	}
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
	if (!data->modem_online && data->modem_powered)
		online_modem(proxy, data);
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
	d_info("registered: %d\n", priv->registered);
	d_info("have_connman: %d\n", priv->have_connman);
	d_info("failcount: %d\n", priv->failcount);
	d_info("gprs_attached: %d\n", priv->gprs_attached);
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
#if 0
		get_connection_contexts(modem);
#endif
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
	g_hash_table_insert(priv->modem_hash, g_strdup(path), modem);
	modem_power_on();
	modem->state = MODEM_INIT;
	modem->modem = g_dbus_proxy_new_for_bus_sync(G_BUS_TYPE_SYSTEM, G_DBUS_PROXY_FLAGS_NONE,
				  NULL, OFONO_SERVICE, path, OFONO_MODEM_INTERFACE,
				  NULL, &err);
	if (err) {
		g_warning("No Modem proxy: %s", err->message);
		g_error_free(err);
		return;
	}
	g_signal_connect(modem->modem, "g-signal", G_CALLBACK (modem_obj_cb), modem);
	get_process_props(modem->modem, modem, check_modem_property);
	set_properties(modem->modem, modem);
	modem->check_modem_id = g_timeout_add_seconds(3, check_modem_state, modem);
	modem->check_connman_id = g_timeout_add_seconds(6, check_connman_powered, modem);
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
	char *modempath;
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
	voicecall_init(conn, priv);

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

	g_type_init();
	gpio_init();
	debug_init();
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

