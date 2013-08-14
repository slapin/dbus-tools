#ifndef PRIVDATA_H
#define PRIVDATA_H

enum modem_states {
	MODEM_INIT,
	MODEM_CONNMAN,
	MODEM_GPRS,
};

struct privdata {
	GDBusConnection *conn;
	GDBusProxy *mgr;
	GHashTable *modem_hash;
	GHashTable *context_hash;
};

struct modemdata {
	struct privdata *priv;
	gchar *path;
	GDBusProxy *modem;
	GDBusProxy *netreg;
	GDBusProxy *connman;
	GDBusProxy *context;
	GDBusProxy *voicecall;
	gboolean modem_online;
	gboolean modem_powered;
	int registered;
	int have_connman;
	int gprs_attached;
	int gprs_powered;
	int context_active;
	int ip_configured;

	/* Events */
	guint check_modem_id;
	guint check_connman_id;
	guint check_netreg_id;
	guint check_context_id;
	guint connman_attach_id;
	guint modem_online_id;
	guint modem_connman_id;

	/* Watchdog field */
	int failcount;

	/* State */
	enum modem_states state;
	gboolean cstate;

	/* Context */
	int active_context_counter;
	/* Fatal count */
	int fatal_count;
	/* Voice */
	int in_voicecall;
	time_t voicecall_start;
	char *call_path;
};
#endif
