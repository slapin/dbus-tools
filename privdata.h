#ifndef PRIVDATA_H
#define PRIVDATA_H

enum modem_states {
	MODEM_INIT,
	MODEM_CONNMAN,
	MODEM_GPRS,
};

struct privdata {
	GDBusProxy *mgr;
	GDBusProxy *modem;
	GDBusProxy *netreg;
	GDBusProxy *connman;
	GDBusProxy *context;
	gboolean modem_online;
	gboolean modem_powered;
	int registered;
	int have_connman;
	int gprs_attached;
	int gprs_powered;
	int context_active;
	int ip_configured;

	/* Watchdog field */
	int failcount;

	/* State */
	enum modem_states state;
};
#endif
