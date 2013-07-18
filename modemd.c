#include <gio/gio.h>

#define OFONO_SERVICE "org.ofono"
#define OFONO_MANAGER_PATH "/"
#define OFONO_MANAGER_INTERFACE OFONO_SERVICE ".Manager"
#define OFONO_MODEM_INTERFACE OFONO_SERVICE ".Modem"
#define OFONO_SIM_INTERFACE OFONO_SERVICE ".SimManager"

struct privdata {
	GDBusProxy *mgr;
	GDBusProxy *modem;
	gboolean modem_online;
	gboolean modem_powered;
};
struct privdata modemdata;

static GMainLoop *loop = NULL;

static void propset_cb(GDBusProxy *modem_proxy, GAsyncResult *res, gpointer data)
{
	GError *err = NULL;
	GVariant *retv;
	char *type = (char *)data;
	retv = g_dbus_proxy_call_finish (modem_proxy, res, &err);
	if (err) {
		g_warning("SetProperty failed: %s: %s", type, err->message);
		g_error_free(err);
		g_free(data);
		return;
	}
	g_print("Successfully set %s property\n", type);
	g_free(data);
	g_variant_unref(retv);
}
static void power_modem(GDBusProxy *proxy, struct privdata *data)
{
	g_dbus_proxy_call(proxy, "SetProperty",
		 g_variant_new("(sv)", "Powered", g_variant_new_boolean(TRUE)),
		 G_DBUS_CALL_FLAGS_NONE, 120000000, NULL, (GAsyncReadyCallback)propset_cb, g_strdup("Powered"));
}
static void online_modem(GDBusProxy *proxy, struct privdata *data)
{
	g_dbus_proxy_call(proxy, "SetProperty",
		 g_variant_new("(sv)", "Online", g_variant_new_boolean(TRUE)),
		 G_DBUS_CALL_FLAGS_NONE, 120000000, NULL, (GAsyncReadyCallback)propset_cb, g_strdup("Online"));
}

static void check_property(struct privdata *data, const char *key, GVariant *value)
{
	g_print("value type: %s\n",
		g_variant_get_type_string(value));

	if (g_strcmp0(key, "Powered") == 0) {
		g_variant_get(value, "b", &data->modem_powered);
		g_print("value data: %d\n", data->modem_powered);
	} else if (g_strcmp0(key, "Online") == 0) {
		g_variant_get(value, "b", &data->modem_online);
		g_print("value data: %d\n", data->modem_online);
	}
}
static void set_properties(GDBusProxy *proxy, struct privdata *data)
{
	g_print("setting props\n");
	if (!data->modem_powered)
		power_modem(proxy, data);
	if (!data->modem_online && data->modem_powered)
		online_modem(proxy, data);
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
		g_print("property changed: %s\n", key);
		check_property(priv, key, value);
		g_variant_unref(value);
	}
	set_properties(proxy, priv);
}

static void get_modem_props_cb(GDBusProxy *modem_proxy,
			       GAsyncResult *res,
			       gpointer data)
{
	GError *err = NULL;
	GVariant *props, *value;
	GVariantIter *iter;
	const char *key;
	struct privdata *priv = data;
	props = g_dbus_proxy_call_finish (modem_proxy, res, &err);
	if (err) {
		g_warning("GetProperties failed: %s", err->message);
		g_error_free(err);
		return;
	}
	g_variant_get (props, "(a{sv})", &iter);
	while (g_variant_iter_loop (iter, "{sv}", &key, &value)) {
		g_print("prop: %s\n", key);
		check_property(priv, key, value);
	}
	set_properties(modem_proxy, priv);
	g_variant_iter_free(iter);
	g_variant_unref(props);
	
}

static void modem_proxy_new_cb(GObject *source_object, 
			       GAsyncResult *res,
			       gpointer data)
{
	GError *err = NULL;
	GDBusProxy *modem_proxy;
	struct privdata *priv = data;
	modem_proxy = g_dbus_proxy_new_for_bus_finish (res, &err);
	if (err) {
		g_warning("No Modem proxy: %s", err->message);
		return;
	}
	priv->modem = modem_proxy;
	g_signal_connect(modem_proxy, "g-signal", G_CALLBACK (modem_obj_cb), priv);
	g_dbus_proxy_call (modem_proxy,
		 "GetProperties", NULL, G_DBUS_CALL_FLAGS_NONE,
		 -1, NULL, (GAsyncReadyCallback)get_modem_props_cb,
		 priv);
}

static void enable_modem(struct privdata *priv, const char *path)
{
	g_dbus_proxy_new_for_bus (G_BUS_TYPE_SYSTEM, G_DBUS_PROXY_FLAGS_NONE,
				  NULL, OFONO_SERVICE, path, OFONO_MODEM_INTERFACE,
				  NULL, (GAsyncReadyCallback)modem_proxy_new_cb,
				  priv);
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
	}
}
static void get_modems_cb(GObject *object, GAsyncResult *res, gpointer data)
{
	struct privdata *priv = data;
	GVariant *modems;
	char *obj_path;
	GVariantIter *iter, *iterg;
	GError *err = NULL;
	GDBusProxy *mgr = (GDBusProxy *)object;
	modems = g_dbus_proxy_call_finish (mgr, res, &err);
	if (err) {
		g_warning ("GetModems failed: %s", err->message);
		return;
	}
	g_variant_get (modems, "(a(oa{sv}))", &iter);
	while (g_variant_iter_loop (iter, "(oa{sv})", &obj_path, NULL)) {
		g_print("modem: %s\n", obj_path);
		if (!g_strcmp0(obj_path, "/sim900_0")) {
			enable_modem(priv, obj_path);
		}
	}
	g_variant_iter_free(iter);
	g_variant_unref(modems);
}

static void ofono_proxy_new_for_bus_cb (GObject *object,
		GAsyncResult *res,
		gpointer data)
{
	struct privdata *priv = data;
	GError *err = NULL;
	priv->mgr = g_dbus_proxy_new_for_bus_finish (res, &err);
	if (err) {
		g_warning("No ofono proxy: %s\n", err->message);
		return;
	}
	g_signal_connect(priv->mgr, "g-signal", G_CALLBACK(manager_signal_cb), priv);
	g_dbus_proxy_call(priv->mgr, "GetModems", NULL, G_DBUS_CALL_FLAGS_NONE,
			-1, NULL, get_modems_cb, priv);
}

int main()
{
	g_type_init();
	loop = g_main_loop_new(NULL, FALSE);
	g_dbus_proxy_new_for_bus(G_BUS_TYPE_SYSTEM,
			G_DBUS_PROXY_FLAGS_NONE,
			NULL,
			OFONO_SERVICE,
			OFONO_MANAGER_PATH,
			OFONO_MANAGER_INTERFACE,
			NULL,
			ofono_proxy_new_for_bus_cb,
			&modemdata);
	g_main_loop_run(loop);
	return 0;
}

