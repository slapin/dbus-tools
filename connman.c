#include <gio/gio.h>
#include "privdata.h"
#include "connman.h"
#include "contexts.h"
#include "modemd.h"
#include "ofono-props.h"

static gboolean check_active_context(gpointer data)
{
	struct modemdata *modem = data;
	if (!modem->context_active) {
		if (modem->active_context_counter) {
			get_connection_contexts(modem);
			modem->active_context_counter--;
			return TRUE;
		} else /* Modem stuck */
			terminate_disable_modem();
	}
	return FALSE;
}

void check_connman_prop(void *data, const char *key, GVariant *value)
{
	struct modemdata *modem = data;
	g_print("value type: %s\n",
		g_variant_get_type_string(value));
	if (g_strcmp0(key, "Attached") == 0) {
		gboolean val;
		g_variant_get(value, "b", &val);
		modem->gprs_attached = val;
		if (modem->gprs_attached) {
			if (!modem->active_context_counter) {
				modem->active_context_counter = 10;
				get_connection_contexts(modem);
				modem->state = MODEM_CONNMAN;
				g_timeout_add_seconds(15, check_active_context, modem);
			}
		}
	} else if (g_strcmp0(key, "Powered") == 0) {
		gboolean val;
		g_variant_get(value, "b", &val);
		modem->gprs_powered = val;
		if (!modem->gprs_powered) {
			modem->gprs_attached = 0;
			modem->context_active = 0;
			modem->state = MODEM_INIT;
		}
	}
}

static void connman_signal_cb(GDBusProxy *connman, gchar *sender_name,
			      gchar *signal_name, GVariant *parameters,
			      gpointer data)
{
	const char *key;
	GVariant *value;
	struct modemdata *priv = data;
	if (g_strcmp0(signal_name, "PropertyChanged") == 0) {
		g_variant_get(parameters, "(sv)", &key, &value);
		g_print("connman property changed: %s\n", key);
		check_connman_prop(priv, key, value);
		g_variant_unref(value);
	}
}

void connman_stuff(struct modemdata *data)
{
	GError *err = NULL;
	data->connman = g_dbus_proxy_new_for_bus_sync(G_BUS_TYPE_SYSTEM,
			G_DBUS_PROXY_FLAGS_NONE,
			NULL,
			OFONO_SERVICE,
			data->path, /* FIXME */
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

