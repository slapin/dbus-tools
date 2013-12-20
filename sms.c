#include <gio/gio.h>
#include "sms.h"
#include "debug.h"

static void sms_recv(GDBusConnection *conn,
		       const gchar *sender_name,
		       const gchar *object_path,
		       const gchar *interface_name,
		       const gchar *signal_name,
		       GVariant *parameters,
		       gpointer userdata)
{
	gchar *message, *what;
	GVariantIter *iter;
	GVariant *data;
	g_variant_get (parameters, "(sa{sv})", &message, &iter);
	d_info("got SMS\n");
	d_info("message text: %s\n", message);
	while(g_variant_iter_loop(iter, "{sv}", &what, &data))
		d_info("SMS: %s\n", what);
	g_variant_iter_free(iter);
}

void sms_init(GDBusConnection *conn)
{
	g_dbus_connection_signal_subscribe(conn, NULL, "org.ofono.MessageManager", "IncomingMessage", NULL, NULL, G_DBUS_SIGNAL_FLAGS_NONE,
		sms_recv, NULL, NULL);
}

