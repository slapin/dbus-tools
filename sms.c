#include <gio/gio.h>
#include "sms.h"
#include "debug.h"

#define SMS_SCRIPT	"/usr/lib/pongo/smsrun.py"
static void sms_recv(GDBusConnection *conn,
		       const gchar *sender_name,
		       const gchar *object_path,
		       const gchar *interface_name,
		       const gchar *signal_name,
		       GVariant *parameters,
		       gpointer userdata)
{
	gchar *message, *what, *sender;
	GVariantIter *iter;
	GVariant *data;
	gboolean r;
	GError *err;
	int status;
	char *outp, *errp;
	err = NULL;
	char *script_argv[] = {
		SMS_SCRIPT,
		NULL, /* Sender */
		NULL, /* Text */
		NULL,
	};
	g_variant_get (parameters, "(sa{sv})", &message, &iter);
	d_info("got SMS\n");
	d_info("message text: %s\n", message);
	sender = NULL;
	while(g_variant_iter_loop(iter, "{sv}", &what, &data)) {
		d_info("SMS: %s\n", what);
		if (!g_strcmp0(what, "Sender"))
			g_variant_get(data, "s", &sender);
	}
	g_variant_iter_free(iter);
	if (!sender)
		goto out;
	script_argv[1] = sender;
	script_argv[2] = message;
	r = g_spawn_sync(NULL, script_argv, NULL, 0, /* flags*/
			NULL, NULL, &outp, &errp,
			&status, &err);
	d_info("SMS script exec: %s (%s) status = %d\n", outp, errp, status);
	if (!r) {
		if (err)
			d_info("SMS script error %s\n", err->message);
	} else
		goto out;
out:
	g_free(outp);
	g_free(errp);
	return;
}

void sms_init(GDBusConnection *conn)
{
	g_dbus_connection_signal_subscribe(conn, NULL, "org.ofono.MessageManager", "IncomingMessage", NULL, NULL, G_DBUS_SIGNAL_FLAGS_NONE,
		sms_recv, NULL, NULL);
}

