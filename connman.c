#include <gio/gio.h>
#include "privdata.h"
#include "connman.h"
#include "contexts.h"
#include "modemd.h"
#include "ofono-props.h"

void check_connman_prop(struct privdata *data, const char *key, GVariant *value)
{
	g_print("value type: %s\n",
		g_variant_get_type_string(value));
	if (g_strcmp0(key, "Attached") == 0) {
		gboolean val;
		g_variant_get(value, "b", &val);
		data->gprs_attached = val;
		if (data->gprs_attached) {
			get_connection_contexts(data);
			data->state = MODEM_CONNMAN;
		}
	} else if (g_strcmp0(key, "Powered") == 0) {
		gboolean val;
		g_variant_get(value, "b", &val);
		data->gprs_powered = val;
		if (!data->gprs_powered) {
			data->gprs_attached = 0;
			data->context_active = 0;
			data->state = MODEM_INIT;
		}
	}
}

static void connman_signal_cb(GDBusProxy *connman, gchar *sender_name,
			      gchar *signal_name, GVariant *parameters,
			      gpointer data)
{
	const char *key;
	GVariant *value;
	struct privdata *priv = data;
	if (g_strcmp0(signal_name, "PropertyChanged") == 0) {
		g_variant_get(parameters, "(sv)", &key, &value);
		g_print("connman property changed: %s\n", key);
		check_connman_prop(priv, key, value);
		g_variant_unref(value);
	}
}

void connman_stuff(struct privdata *data)
{
	GError *err = NULL;
	data->connman = g_dbus_proxy_new_for_bus_sync(G_BUS_TYPE_SYSTEM,
			G_DBUS_PROXY_FLAGS_NONE,
			NULL,
			OFONO_SERVICE,
			"/sim900_0", /* FIXME */
			OFONO_CONNMAN_INTERFACE,
			NULL, &err);
	if (err) {
		g_warning("No connman interface proxy: %s\n", err->message);
		g_error_free(err);
		return;
	}
	g_signal_connect(data->connman, "g-signal", G_CALLBACK(connman_signal_cb), data);
	get_process_props(data->connman, data, check_connman_prop);
}

