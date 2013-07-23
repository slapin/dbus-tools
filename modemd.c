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

struct privdata modemdata;

static GMainLoop *loop = NULL;
static void check_netreg_status(struct privdata *data, const char *status)
{
	if (!g_strcmp0(status, "registered"))
		data->registered = 1;
	else
		data->registered = 0;
}

static void check_netreg_property(struct privdata *data, const char *key, GVariant *value)
{
	const char *val;
	g_print("value type: %s\n",
		g_variant_get_type_string(value));
	if (g_strcmp0(key, "Status") == 0) {
		g_variant_get(value, "s", &val);
		g_print("value: %s\n", val);
		check_netreg_status(data, val);
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
		g_print("netreg property changed: %s\n", key);
		check_netreg_property(priv, key, value);
		g_variant_unref(value);
	}
}

static void netreg_stuff(struct privdata *data)
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

static void power_modem(GDBusProxy *proxy, struct privdata *data)
{
	g_print("modem power on\n");
	set_proxy_property(proxy, "Powered", g_variant_new_boolean(TRUE));
	g_print("done\n");
}
static void online_modem(GDBusProxy *proxy, struct privdata *data)
{
	g_print("modem power on\n");
	set_proxy_property(proxy, "Online", g_variant_new_boolean(TRUE));
	g_print("done\n");
}

static void check_interfaces(struct privdata *data, const char *iface)
{
	int have_connman = data->have_connman;
	if (g_strcmp0(iface, OFONO_NETREG_INTERFACE) == 0)
		netreg_stuff(data);
	else if (g_strcmp0(iface, OFONO_CONNMAN_INTERFACE) == 0)
		have_connman = 1;
	data->have_connman = have_connman;
	if (have_connman)
		connman_stuff(data);
}

static void check_modem_property(struct privdata *data, const char *key, GVariant *value)
{
	g_print("value type: %s\n",
		g_variant_get_type_string(value));

	if (g_strcmp0(key, "Powered") == 0) {
		g_variant_get(value, "b", &data->modem_powered);
		g_print("value data: %d\n", data->modem_powered);
	} else if (g_strcmp0(key, "Online") == 0) {
		g_variant_get(value, "b", &data->modem_online);
		g_print("value data: %d\n", data->modem_online);
	} else if (g_strcmp0(key, "Interfaces") == 0) {
		GVariantIter *iter;
		char *v;
		g_variant_get(value, "as", &iter);
		while (g_variant_iter_loop (iter, "s", &v)) {
			g_print("Interface: %s\n", v);
			if (g_strcmp0(v, OFONO_CONNMAN_INTERFACE) == 0)
				g_print("CONNMAN\n");
			check_interfaces(data, v);
		}
		g_variant_iter_free(iter);
	}
}
static void set_properties(GDBusProxy *proxy, struct privdata *data)
{
	g_print("setting props\n");
	if (!data->modem_powered)
		power_modem(proxy, data);
	if (!data->modem_online && data->modem_powered)
		online_modem(proxy, data);
	g_print("all done\n");
}

static void modem_obj_cb(GDBusProxy *proxy, gchar *sender_name,
			 gchar *signal_name, GVariant *parameters,
			 gpointer data)
{
	GVariant *value;
	const char *key;
	struct privdata *priv = data;
	if (g_strcmp0(signal_name, "PropertyChanged") == 0) {
		g_variant_get(parameters, "(sv)", &key, &value);
		g_print("modem property changed: %s\n", key);
		check_modem_property(priv, key, value);
		g_variant_unref(value);
	}
	set_properties(proxy, priv);
}

static void enable_modem(struct privdata *priv, const char *path)
{
	GError *err = NULL;
	modem_power_on();
	priv->modem = g_dbus_proxy_new_for_bus_sync(G_BUS_TYPE_SYSTEM, G_DBUS_PROXY_FLAGS_NONE,
				  NULL, OFONO_SERVICE, path, OFONO_MODEM_INTERFACE,
				  NULL, &err);
	if (err) {
		g_warning("No Modem proxy: %s", err->message);
		g_error_free(err);
		return;
	}
	g_signal_connect(priv->modem, "g-signal", G_CALLBACK (modem_obj_cb), priv);
	get_process_props(priv->modem, priv, check_modem_property);
	set_properties(priv->modem, priv);
}

static void manager_signal_cb(GDBusProxy *mgr, gchar *sender_name,
			      gchar *signal_name, GVariant *parameters,
			      gpointer data)
{
	struct privdata *priv = data;
	const char *obj_path;
	if (g_strcmp0 (signal_name, "ModemAdded") == 0) {
		g_variant_get (parameters, "(oa{sv})", &obj_path, NULL);
		g_print("Modem added: %s\n", obj_path);
		enable_modem(data, obj_path);
	} else if (g_strcmp0 (signal_name, "ModemRemoved") == 0) {
		g_variant_get (parameters, "(o)", &obj_path);
		g_print("Modem removed: %s\n", obj_path);
		modem_power_off();
	}
}

static gboolean check_modem_state(gpointer data)
{
	struct privdata *priv = (struct privdata *)data;
#if 0
	get_process_props(priv->modem, priv, check_modem_property);
#endif
	set_led_state(priv);
	g_print("registered: %d\n", priv->registered);
	g_print("have_connman: %d\n", priv->have_connman);
	g_print("failcount: %d\n", priv->failcount);
	g_print("gprs_attached: %d\n", priv->gprs_attached);
	return TRUE;
}
static gboolean check_connman_powered(gpointer data)
{
	struct privdata *priv = (struct privdata *)data;
	int power_on = 0;
	if (!priv->have_connman)
		priv->ip_configured = 0;

	if (priv->have_connman && (!priv->gprs_attached || !priv->context_active)) {
		priv->ip_configured = 0;
		get_process_props(priv->connman, data, check_connman_prop);
	}
	if (priv->gprs_attached && !priv->context_active) {
		priv->ip_configured = 0;
		get_connection_contexts(priv);
	}
	if (!priv->gprs_powered) {
		priv->ip_configured = 0;
		set_proxy_property(priv->connman, "Powered", g_variant_new_boolean(TRUE));
	}
	if (priv->context_active && !priv->ip_configured) {
		do_ipv4_config();
		do_ipv6_config();
		priv->state = MODEM_GPRS;
		priv->ip_configured = 1;
	}
out:
	return TRUE;
}

static void add_call(GDBusConnection *connection,
		       const gchar *sender_name,
		       const gchar *object_path,
		       const gchar *interface_name,
		       const gchar *signal_name,
		       GVariant *parameters,
		       gpointer userdata)
{
	g_print("AddCall: %s\n", g_variant_get_type_string(parameters));
}

static void remove_call(GDBusConnection *connection,
		       const gchar *sender_name,
		       const gchar *object_path,
		       const gchar *interface_name,
		       const gchar *signal_name,
		       GVariant *parameters,
		       gpointer userdata)
{
	g_print("RemoveCall: %s\n", g_variant_get_type_string(parameters));
}


int main(int argc, char *argv[])
{
	GError *err = NULL;
	struct privdata *priv = &modemdata;
	GVariant *modems;
	char *obj_path;
	GVariantIter *iter;
	GDBusConnection *conn;
	g_type_init();
	gpio_init();
	priv->state = MODEM_INIT;
	loop = g_main_loop_new(NULL, FALSE);
	conn = g_bus_get_sync(G_BUS_TYPE_SYSTEM, NULL, &err);
	if (!conn) {
		g_printerr("Error connecting to D-Bus: %s\n", err->message);
		g_error_free(err);
		return 1;
	}
	err = NULL;
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
		g_print("modem: %s\n", obj_path);
		if (!g_strcmp0(obj_path, "/sim900_0")) {
			enable_modem(priv, obj_path);
		}
	}
	g_variant_iter_free(iter);
	g_variant_unref(modems);
	g_timeout_add_seconds(3, check_modem_state, priv);
	g_timeout_add_seconds(6, check_connman_powered, priv);

	g_dbus_connection_signal_subscribe(conn, NULL, OFONO_VOICECALL_INTERFACE, "CallAdded", NULL, NULL, G_DBUS_SIGNAL_FLAGS_NONE,
		add_call, priv, NULL);
	g_dbus_connection_signal_subscribe(conn, NULL, OFONO_VOICECALL_INTERFACE, "CallRemoved", NULL, NULL, G_DBUS_SIGNAL_FLAGS_NONE,
		remove_call, priv, NULL);
	
	g_main_loop_run(loop);
	return 0;
}

