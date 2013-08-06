#include <gio/gio.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>

#include "privdata.h"
#include "contexts.h"
#include "modemd.h"
#include "ofono-props.h"
#include "debug.h"
static int used_context = 0;
struct ip_settings {
	char interface[16];
	char address[16];
	char netmask[16];
	char gateway[16];
	char resolvers[3][16];
	int nresolvers;
};
static struct ip_settings ipv4;

static void save_resolv_conf(void)
{
	int i, fd;
	fd = open("/var/run/resolv.conf", O_WRONLY|O_CREAT|O_TRUNC);
	if (fd < 0)
		return;
	for (i = 0; i < ipv4.nresolvers; i++) {
		char ns[64];
		snprintf(ns, sizeof(ns), "nameserver %s\n", ipv4.resolvers[i]);
		write(fd, ns, strlen(ns));
	}
	close(fd);
}

void do_ipv4_config(void)
{
	char cmdbuf[128];
	snprintf(cmdbuf, sizeof(cmdbuf), "/sbin/ifconfig %s %s netmask %s",
		ipv4.interface, ipv4.address, ipv4.netmask);
	g_print("IPv4 command: %s\n", cmdbuf);
	system(cmdbuf);
	if (!strcmp(ipv4.gateway, "0.0.0.0"))
		snprintf(cmdbuf, sizeof(cmdbuf), "/sbin/route add default dev %s", ipv4.interface);
	else
		snprintf(cmdbuf, sizeof(cmdbuf), "/sbin/route add default gw %s", ipv4.gateway);
	g_print("IPv4 command: %s\n", cmdbuf);
	system(cmdbuf);
	save_resolv_conf();
}

static void do_ip_down(void)
{
	char cmdbuf[128];
	if (strlen(ipv4.interface) == 0)
		return;
	else {
		snprintf(cmdbuf, sizeof(cmdbuf), "/sbin/ifconfig %s down",
			ipv4.interface);
		system(cmdbuf);
	}
}

void do_ipv6_config(void)
{
}
static void check_ip_settings(struct modemdata *data, const char *key, GVariant *value)
{
	g_print("IPv4: %s, value type: %s\n",
		key,
		g_variant_get_type_string(value));
	if (g_strcmp0(key, "Interface") == 0) {
		const char *iface;
		g_variant_get(value, "s", &iface);
		strncpy(ipv4.interface, iface, sizeof(ipv4.interface));
	} else if (g_strcmp0(key, "Address") == 0) {
		const char *addr;
		g_variant_get(value, "s", &addr);
		strncpy(ipv4.address, addr, sizeof(ipv4.address));
	} else if (g_strcmp0(key, "Netmask") == 0) {
		const char *addr;
		g_variant_get(value, "s", &addr);
		strncpy(ipv4.netmask, addr, sizeof(ipv4.netmask));
	} else if (g_strcmp0(key, "Gateway") == 0) {
		const char *addr;
		g_variant_get(value, "s", &addr);
		strncpy(ipv4.gateway, addr, sizeof(ipv4.gateway));
	} else if (g_strcmp0(key, "DomainNameServers") == 0) {
		GVariantIter *iter;
		const char *v;
		int i;
		g_variant_get(value, "as", &iter);
		i = 0;
		while (g_variant_iter_loop (iter, "s", &v)) {
			d_info("DNS: %s\n", v);
			memset(ipv4.resolvers[i], 0, sizeof(ipv4.resolvers[i]));
			strncpy(ipv4.resolvers[i], v, sizeof(ipv4.resolvers[i]));
			i++;
			if (i >= sizeof(ipv4.resolvers) / sizeof(ipv4.resolvers[0]))
				break;
		}
		ipv4.nresolvers = i;
	}
}
static void check_ipv6_settings(struct modemdata *data, const char *key, GVariant *value)
{
	g_print("IPv6: %s, value type: %s\n",
		key,
		g_variant_get_type_string(value));
}
static void check_context_prop(void *data, const char *key, GVariant *value)
{
	struct modemdata *modem = data;
	g_print("context prop: %s, value type: %s\n",
		key,
		g_variant_get_type_string(value));
	if (g_strcmp0(key, "Active") == 0) {
		gboolean v;
		g_variant_get(value, "b", &v);
		modem->context_active = v;
		if (!modem->context_active) {
			do_ip_down();
			modem->ip_configured = 0;
		}
	} else if (g_strcmp0(key, "Settings") == 0) {
		GVariantIter *iter;
		const char *k;
		GVariant *v;
		g_variant_get(value, "a{sv}", &iter);
		while (g_variant_iter_loop (iter, "{sv}", &k, &v)) {
			check_ip_settings(data, k, v);
		}
		if (modem->context_active && !modem->ip_configured) {
			do_ipv4_config();
			modem->state = MODEM_GPRS;
			modem->ip_configured = 1;
		}
	} else if (g_strcmp0(key, "IPv6.Settings") == 0) {
		GVariantIter *iter;
		const char *k;
		GVariant *v;
		g_variant_get(value, "a{sv}", &iter);
		while (g_variant_iter_loop (iter, "{sv}", &k, &v)) {
			check_ipv6_settings(modem, k, v);
		}
		if (modem->context_active)
			do_ipv6_config();
	}
}
static void context_signal_cb(GDBusProxy *context, gchar *sender_name,
			      gchar *signal_name, GVariant *parameters,
			      gpointer data)
{
	const char *key;
	GVariant *value;
	struct modemdata *priv = data;
	g_print("context: signal: %s\n", signal_name);
	if (g_strcmp0(signal_name, "PropertyChanged") == 0) {
		g_variant_get(parameters, "(sv)", &key, &value);
		g_print("connman property changed: %s\n", key);
		check_context_prop(priv, key, value);
		g_variant_unref(value);
	}
}

static void activate_context(struct modemdata *data, const char *objpath)
{
	GError *err = NULL;
	data->context = g_dbus_proxy_new_for_bus_sync(G_BUS_TYPE_SYSTEM,
			G_DBUS_PROXY_FLAGS_NONE,
			NULL,
			OFONO_SERVICE,
			objpath, /* FIXME */
			OFONO_CONTEXT_INTERFACE,
			NULL, &err);
	if (err) {
		g_warning("no proxy for context: %s\n", err->message);
		g_error_free(err);
		return;
	}
	strcpy(ipv4.gateway, "0.0.0.0");
	g_signal_connect(data->context, "g-signal", G_CALLBACK(context_signal_cb), data);
	get_process_props(data->context, data, check_context_prop);
	if (!data->context_active) {
		set_proxy_property(data->context, "AccessPointName", g_variant_new_string("internet"));
		set_proxy_property(data->context, "Active", g_variant_new_boolean(TRUE));
	}
}

void get_connection_contexts(struct modemdata *data)
{
	GError *err = NULL;
	GVariantIter *iter;
	const char *obj_path;
	int i;
	GVariant *contexts = g_dbus_proxy_call_sync(data->connman,
		 "GetContexts", NULL, G_DBUS_CALL_FLAGS_NONE,
		 -1, NULL, &err);
	if (err) {
		g_warning("GetContexts failed: %s\n", err->message);
		g_error_free(err);
		return;
	}
	g_print("contexts type: %s\n",
		g_variant_get_type_string(contexts));
	g_variant_get(contexts, "(a(oa{sv}))", &iter);
	i = 0;
	while (g_variant_iter_loop(iter, "oa{sv}", &obj_path, NULL)) {
		g_print("context: %s\n", obj_path);
		if (i == used_context)
			activate_context(data, obj_path);
		i++;
	}
	g_variant_iter_free(iter);
	g_variant_unref(contexts);
}

