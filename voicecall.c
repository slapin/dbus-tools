#include <gio/gio.h>
#include <string.h>
#include <fcntl.h>

#include "modemd.h"
#include "privdata.h"
#include "voicecall.h"
#include "debug.h"
#include "u-boot.h"

extern const char *amppath;
static void write_amp(const char *d)
{
	int fd = open(amppath, O_WRONLY);
	if (fd > 0) {
		write(fd, d, strlen(d));
		close(fd);
	}
}
static void enable_amp(void)
{
	write_amp("enabled");
}

static void disable_amp(void)
{
	write_amp("disabled");
}

static void add_call(GDBusConnection *connection,
		       const gchar *sender_name,
		       const gchar *object_path,
		       const gchar *interface_name,
		       const gchar *signal_name,
		       GVariant *parameters,
		       gpointer userdata)
{
	GError *err;
	GVariant *v;
	GVariantIter *iter;
	char* objstr;
	char* str;
	char* incoming_number;

	char* number = fw_getenv("pongo_masternum");
	d_notice("AddCall: %s\n", g_variant_get_type_string(parameters));
	d_notice("pongo_masternum: %s\n", number);

	g_variant_get(parameters, "(oa{sv})", &objstr, &iter);

	while(g_variant_iter_loop(iter, "{sv}", &str, &v)) {
		d_info("call field: %s\n", str);
		if (!strcmp(str, "LineIdentification")) {
			g_variant_get(v, "s", &incoming_number);
			d_info("call: %s, incoming number: %s\n", objstr, incoming_number);
			if(strstr(number, incoming_number) || (strlen(number) < 2)) {
				GVariant *ret;
				d_info("Answer on incoming call\n");
				enable_amp();
				d_info("calling Answer method");
				err = NULL;
				g_dbus_connection_call_sync(connection, OFONO_SERVICE,
					objstr, OFONO_CALL_INTERFACE, "Answer", NULL, NULL,
					G_DBUS_CALL_FLAGS_NONE, 1500, NULL, &err);
				/* TODO: check error */
				if (err)
					d_notice("error: Answer: %s\n", err->message);
			} else {
				err = NULL;
				g_dbus_connection_call_sync(connection, OFONO_SERVICE,
					objstr, OFONO_CALL_INTERFACE, "Hangup", NULL, NULL,
					G_DBUS_CALL_FLAGS_NONE, 1500, NULL, &err);
				/* TODO: check error */
				if (err)
					d_notice("error: Hangup: %s\n", err->message);
			}
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
	disable_amp();
}

void voicecall_init(GDBusConnection *conn, struct modemdata *priv)
{
	extern int mic_vol, spk_vol;
	GError *err = NULL;
	g_dbus_connection_call_sync(conn, OFONO_SERVICE,
		priv->path, OFONO_VOLUME_INTERFACE, "SetProperty",
		g_variant_new("(sv)",
			"SpeakerVolume",
			g_variant_new_int32(spk_vol)),
		NULL,
		G_DBUS_CALL_FLAGS_NONE, 1500, NULL, &err);
	g_dbus_connection_call_sync(conn, OFONO_SERVICE,
		priv->path, OFONO_VOLUME_INTERFACE, "SetProperty",
		g_variant_new("(sv)",
			"MicrophoneVolume",
			g_variant_new_int32(mic_vol)),
		NULL,
		G_DBUS_CALL_FLAGS_NONE, 1500, NULL, &err);
	/* TODO: check error */
	g_dbus_connection_signal_subscribe(conn, NULL, OFONO_VOICECALL_INTERFACE, "CallAdded", NULL, NULL, G_DBUS_SIGNAL_FLAGS_NONE,
		add_call, priv, NULL);
	g_dbus_connection_signal_subscribe(conn, NULL, OFONO_VOICECALL_INTERFACE, "CallRemoved", NULL, NULL, G_DBUS_SIGNAL_FLAGS_NONE,
		remove_call, priv, NULL);
	
}

