#include <gio/gio.h>
#include "ofono-props.h"

static void check_gdbus_error(char *what, GError *err)
{
	if (err) {
		g_warning("%s failed: %s\n",  what, err->message);
		g_error_free(err);
	}
}

static GVariant *get_proxy_props(GDBusProxy *proxy)
{
	GError *err = NULL;
	GVariant *props;
	props = g_dbus_proxy_call_sync(proxy,
		 "GetProperties", NULL, G_DBUS_CALL_FLAGS_NONE,
		 -1, NULL, &err);
	if (err) {
		g_warning("GetProperties failed: %s\n", err->message);
		g_error_free(err);
		return NULL;
	}
	return props;
}

static void process_proplist(void *data, GVariant *props,
			     void (*func)(void *data,
					  const char *k, GVariant *val))
{
	GVariantIter *iter;
	const char *key;
	GVariant *value;
	g_variant_get (props, "(a{sv})", &iter);
	while (g_variant_iter_loop (iter, "{sv}", &key, &value)) {
		func(data, key, value);
	}
	g_variant_iter_free(iter);
}


void get_process_props(GDBusProxy *proxy, void *data,
			     void (*func)(void *data,
					  const char *k, GVariant *val))
{
	GVariant *props = get_proxy_props(proxy);
	if (props)
		process_proplist(data, props, func);
	g_variant_unref(props);
}

static GVariant *_set_proxy_property(GDBusProxy *proxy,
				const char *prop,
				GVariant *val,
				GError **err)
{
	GVariant *retv = NULL;
	retv = g_dbus_proxy_call_sync(proxy, "SetProperty",
		 g_variant_new("(sv)", prop, val),
		 G_DBUS_CALL_FLAGS_NONE, 120000000, NULL, err);
	return retv;
}
int set_proxy_property(GDBusProxy *proxy,
				const char *prop,
				GVariant *val)
{
	GError *err = NULL;
	GVariant *retv;
	retv = _set_proxy_property(proxy, prop, val, &err);
	if (err) {
		g_warning("Can't set prop %s\n", prop);
		check_gdbus_error("SetProperty", err);
		return -1;
	}
	if (retv)
		g_print("set %s: return value type: %s\n",
			prop,
			g_variant_get_type_string(retv));
	return 0;
}

