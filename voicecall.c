#include <gio/gio.h>
#include <string.h>

#include "modemd.h"
#include "privdata.h"
#include "voicecall.h"
#include "debug.h"
#include "u-boot.h"

static void add_call(GDBusConnection *connection,
		       const gchar *sender_name,
		       const gchar *object_path,
		       const gchar *interface_name,
		       const gchar *signal_name,
		       GVariant *parameters,
		       gpointer userdata)
{
	d_notice("AddCall: %s\n", g_variant_get_type_string(parameters));

	GVariant *v;
	GVariantIter *iter;
	char* objstr;
	char* str;
	char* incoming_number;

	char* number = fw_getenv("pongo_masternum");
	d_notice("pongo_masternum: %s\n", number);

	g_variant_get(parameters, "(oa{sv})", &objstr, &iter);

	while(g_variant_iter_loop(iter, "{sv}", &str, &v)) {
		if (!strcmp(str, "LineIdentification"))
			g_variant_get(v, "s", &incoming_number);
			if(strcmp(number, incoming_number) == 0 )
			{
				d_info("Answer on incoming call\n");
			}
	}
}

static void remove_call(GDBusConnection *connection,
		       const gchar *sender_name,
		       const gchar *object_path,
		       const gchar *interface_name,
		       const gchar *signal_name,
		       GVariant *parameters,
		       gpointer userdata)
{
	d_notice("RemoveCall: %s\n", g_variant_get_type_string(parameters));
}

void voicecall_init(GDBusConnection *conn, struct privdata *priv)
{
	g_dbus_connection_signal_subscribe(conn, NULL, OFONO_VOICECALL_INTERFACE, "CallAdded", NULL, NULL, G_DBUS_SIGNAL_FLAGS_NONE,
		add_call, priv, NULL);
	g_dbus_connection_signal_subscribe(conn, NULL, OFONO_VOICECALL_INTERFACE, "CallRemoved", NULL, NULL, G_DBUS_SIGNAL_FLAGS_NONE,
		remove_call, priv, NULL);
	
}

