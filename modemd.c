#include <gio/gio.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#define OFONO_SERVICE "org.ofono"
#define OFONO_MANAGER_PATH "/"
#define OFONO_MANAGER_INTERFACE OFONO_SERVICE ".Manager"
#define OFONO_MODEM_INTERFACE OFONO_SERVICE ".Modem"
#define OFONO_SIM_INTERFACE OFONO_SERVICE ".SimManager"
#define OFONO_NETREG_INTERFACE OFONO_SERVICE ".NetworkRegistration"
#define OFONO_CONNMAN_INTERFACE OFONO_SERVICE ".ConnectionManager"
#define OFONO_CONTEXT_INTERFACE OFONO_SERVICE ".ConnectionContext"
#define OFONO_VOICECALL_INTERFACE OFONO_SERVICE ".VoiceCallManager"

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
struct privdata modemdata;

static GMainLoop *loop = NULL;
static void export_gpio(int gpio)
{
	char num[16];
	int fd = open("/sys/class/gpio/export", O_WRONLY);
	if (fd >0) {
		snprintf(num, sizeof(num) - 1, "%d", gpio);
		write(fd, num, strlen(num));
		close(fd);
	} else
		g_warning("Can't export gpio %d\n", gpio);
}

static void gpio_set_input(int gpio)
{
	char filepath[64];
	int fd;
	snprintf(filepath, sizeof(filepath) - 1,
		"/sys/class/gpio/gpio%d/direction",
		gpio);
	fd = open(filepath, O_WRONLY);
	write(fd, "in", 2);
	close(fd);
}

static void gpio_set_output(int gpio)
{
	char filepath[64];
	int fd;
	snprintf(filepath, sizeof(filepath) - 1,
		"/sys/class/gpio/gpio%d/direction",
		gpio);
	fd = open(filepath, O_WRONLY);
	write(fd, "out", 2);
	close(fd);
}

static int gpio_get_value(int gpio)
{
	char filepath[64];
	char rdbuff[64];
	int fd, ret;
	snprintf(filepath, sizeof(filepath) - 1,
		"/sys/class/gpio/gpio%d/value",
		gpio);
	fd = open(filepath, O_RDONLY);
	read(fd, rdbuff, sizeof(rdbuff));
	close(fd);
	return atoi(rdbuff);
}

static void gpio_set_value(int gpio, int value)
{
	char filepath[64];
	int fd, ret;
	snprintf(filepath, sizeof(filepath) - 1,
		"/sys/class/gpio/gpio%d/value",
		gpio);
	fd = open(filepath, O_WRONLY);
	if (value)
		write(fd, "1", 1);
	else
		write(fd, "0", 1);
	close(fd);
}

static void gpio_init(void)
{
	export_gpio(138);
	export_gpio(384);
	export_gpio(390);
	gpio_set_input(138);
	gpio_set_output(384);
	gpio_set_output(390);
}

static int modem_check_power(void)
{
	return gpio_get_value(138);
}

static void power_write(int value)
{
	const char *path = "/sys/devices/platform/reg-userspace-consumer.3/state";
	int fd = open(path, O_WRONLY);
	if (value)
		write(fd, "enabled", 7);
	else
		write(fd, "disabled", 8);
	close(fd);
}

static void modem_power_on(void)
{
	if (!modem_check_power()) {
		gpio_set_value(390, 0);
		power_write(1);
		g_usleep(1000000);
		gpio_set_value(384, 0);
		g_usleep(1000000);
		gpio_set_value(384, 1);
		g_usleep(2000000);
		while(1) {
			int timeout = 10;
			while(!modem_check_power() && timeout) {
				g_usleep(500000);
				timeout--;
			}
			if (timeout) {
				g_usleep(1000000);
				break;
			}
			gpio_set_value(384, 0);
			g_usleep(1000000);
			gpio_set_value(384, 1);
			g_usleep(2000000);
		}
	}
}
static void modem_power_off(void)
{
	power_write(0);
}

const char *led_path = "/sys/class/leds/crux:yellow";
const char *led_trigger = "trigger";
const char *led_brightness = "brightness";
const char *led_hb = "heartbeat";
const char *led_tm = "timer";
const char *led_tm_on = "delay_on";
const char *led_tm_off = "delay_off";

static void set_led_trigger_heartbeat(void)
{
	char led[128];
	int fd;
	snprintf(led, sizeof(led), "%s/%s", led_path, led_trigger);
	fd = open(led, O_WRONLY);
	write(fd, led_hb, strlen(led_hb));
	close(fd);
}

static void set_led_trigger_timer(int delay_on, int delay_off)
{
	char led[128];
	int fd;
	snprintf(led, sizeof(led), "%s/%s", led_path, led_trigger);
	fd = open(led, O_WRONLY);
	write(fd, led_tm, strlen(led_tm));
	close(fd);
	snprintf(led, sizeof(led), "%s/%s", led_path, led_tm_on);
	fd = open(led, O_WRONLY);
	snprintf(led, sizeof(led), "%d", delay_on);
	write(fd, led, strlen(led));
	close(fd);
	snprintf(led, sizeof(led), "%s/%s", led_path, led_tm_off);
	fd = open(led, O_WRONLY);
	snprintf(led, sizeof(led), "%d", delay_off);
	write(fd, led, strlen(led));
	close(fd);
}

static void set_led_state(struct privdata *data)
{
	static int oldstate = -1;
	if (oldstate == data->state)
		return;
	switch(data->state) {
		case MODEM_INIT:
			set_led_trigger_heartbeat();
			break;
		case MODEM_CONNMAN:
			set_led_trigger_timer(600, 400);
			break;
		case MODEM_GPRS:
			set_led_trigger_timer(200, 100);
			break;
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

static void process_proplist(struct privdata *data, GVariant *props,
			     void (*func)(struct privdata *data,
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


static void get_process_props(GDBusProxy *proxy, struct privdata *data,
			     void (*func)(struct privdata *data,
					  const char *k, GVariant *val))
{
	GVariant *props = get_proxy_props(proxy);
	if (props)
		process_proplist(data, props, func);
	g_variant_unref(props);
}

static void check_gdbus_error(char *what, GError *err)
{
	if (err) {
		g_warning("%s failed: %s\n",  what, err->message);
		g_error_free(err);
	}
}

static void set_proxy_property(GDBusProxy *proxy,
				const char *prop,
				GVariant *val)
{
	GError *err = NULL;
	GVariant *retv;
	retv = g_dbus_proxy_call_sync(proxy, "SetProperty",
		 g_variant_new("(sv)", prop, val),
		 G_DBUS_CALL_FLAGS_NONE, 120000000, NULL, &err);
	if (err) {
		g_warning("Can't set prop %s\n", prop);
		check_gdbus_error("SetProperty", err);
		return;
	}
	if (retv)
		g_print("value type: %s\n",
			g_variant_get_type_string(retv));
}
struct ip_settings {
	char interface[16];
	char address[16];
	char netmask[16];
	char gateway[16];
	char resolvers[3][16];
};
static struct ip_settings ipv4;
static void do_ipv4_config(void)
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

static void do_ipv6_config(void)
{
}
static void check_ip_settings(struct privdata *data, const char *key, GVariant *value)
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
	}
}
static void check_ipv6_settings(struct privdata *data, const char *key, GVariant *value)
{
	g_print("IPv6: %s, value type: %s\n",
		key,
		g_variant_get_type_string(value));
}
static void check_context_prop(struct privdata *data, const char *key, GVariant *value)
{
	g_print("context prop: %s, value type: %s\n",
		key,
		g_variant_get_type_string(value));
	if (g_strcmp0(key, "Active") == 0) {
		gboolean v;
		g_variant_get(value, "b", &v);
		data->context_active = v;
		if (!data->context_active) {
			do_ip_down();
			data->ip_configured = 0;
		}
	} else if (g_strcmp0(key, "Settings") == 0) {
		GVariantIter *iter;
		const char *k;
		GVariant *v;
		g_variant_get(value, "a{sv}", &iter);
		while (g_variant_iter_loop (iter, "{sv}", &k, &v)) {
			check_ip_settings(data, k, v);
		}
		if (data->context_active && !data->ip_configured) {
			do_ipv4_config();
			data->state = MODEM_GPRS;
			data->ip_configured = 1;
		}
	} else if (g_strcmp0(key, "IPv6.Settings") == 0) {
		GVariantIter *iter;
		const char *k;
		GVariant *v;
		g_variant_get(value, "a{sv}", &iter);
		while (g_variant_iter_loop (iter, "{sv}", &k, &v)) {
			check_ipv6_settings(data, k, v);
		}
		if (data->context_active)
			do_ipv6_config();
	}
}
static void context_signal_cb(GDBusProxy *context, gchar *sender_name,
			      gchar *signal_name, GVariant *parameters,
			      gpointer data)
{
	const char *key;
	GVariant *value;
	struct privdata *priv = data;
	g_print("context: signal: %s\n", signal_name);
	if (g_strcmp0(signal_name, "PropertyChanged") == 0) {
		g_variant_get(parameters, "(sv)", &key, &value);
		g_print("connman property changed: %s\n", key);
		check_context_prop(priv, key, value);
		g_variant_unref(value);
	}
}

static int used_context = 0;
static void activate_context(struct privdata *data, const char *objpath)
{
	GError *err = NULL;
	GVariant *vact;
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
static void get_connection_contexts(struct privdata *data)
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

static void check_connman_prop(struct privdata *data, const char *key, GVariant *value)
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

static void connman_stuff(struct privdata *data)
{
	GError *err = NULL;
	GVariant *props, *value;
	GVariantIter *iter;
	const char *key;
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


static void check_netreg_status(struct privdata *data, const char *status)
{
	if (!g_strcmp0(status, "registered"))
		data->registered = 1;
	else
		data->registered = 0;
}

static void check_netreg_property(struct privdata *data, const char *key, GVariant *value)
{
	const char *val;
	g_print("value type: %s\n",
		g_variant_get_type_string(value));
	if (g_strcmp0(key, "Status") == 0) {
		g_variant_get(value, "s", &val);
		g_print("value: %s\n", val);
		check_netreg_status(data, val);
	}
}

static void netreg_signal_cb(GDBusProxy *netreg, gchar *sender_name,
			      gchar *signal_name, GVariant *parameters,
			      gpointer data)
{
	const char *key;
	GVariant *value;
	struct privdata *priv = data;
	if (g_strcmp0(signal_name, "PropertyChanged") == 0) {
		g_variant_get(parameters, "(sv)", &key, &value);
		g_print("netreg property changed: %s\n", key);
		check_netreg_property(priv, key, value);
		g_variant_unref(value);
	}
}

static void netreg_stuff(struct privdata *data)
{
	GError *err = NULL;
	data->netreg = g_dbus_proxy_new_for_bus_sync(G_BUS_TYPE_SYSTEM,
			G_DBUS_PROXY_FLAGS_NONE,
			NULL,
			OFONO_SERVICE,
			"/sim900_0", /* FIXME */
			OFONO_NETREG_INTERFACE,
			NULL, &err);
	if (err) {
		g_warning("No netreg interface proxy: %s\n", err->message);
		g_error_free(err);
		return;
	}
	g_signal_connect(data->netreg, "g-signal", G_CALLBACK(netreg_signal_cb), data);
	get_process_props(data->netreg, data, check_netreg_property);
}

static void power_modem(GDBusProxy *proxy, struct privdata *data)
{
	g_print("modem power on\n");
	set_proxy_property(proxy, "Powered", g_variant_new_boolean(TRUE));
	g_print("done\n");
}
static void online_modem(GDBusProxy *proxy, struct privdata *data)
{
	g_print("modem power on\n");
	set_proxy_property(proxy, "Online", g_variant_new_boolean(TRUE));
	g_print("done\n");
}

static void check_interfaces(struct privdata *data, const char *iface)
{
	int have_connman = data->have_connman;
	if (g_strcmp0(iface, OFONO_NETREG_INTERFACE) == 0)
		netreg_stuff(data);
	else if (g_strcmp0(iface, OFONO_CONNMAN_INTERFACE) == 0)
		have_connman = 1;
	data->have_connman = have_connman;
	if (have_connman)
		connman_stuff(data);
}

static void check_modem_property(struct privdata *data, const char *key, GVariant *value)
{
	g_print("value type: %s\n",
		g_variant_get_type_string(value));

	if (g_strcmp0(key, "Powered") == 0) {
		g_variant_get(value, "b", &data->modem_powered);
		g_print("value data: %d\n", data->modem_powered);
	} else if (g_strcmp0(key, "Online") == 0) {
		g_variant_get(value, "b", &data->modem_online);
		g_print("value data: %d\n", data->modem_online);
	} else if (g_strcmp0(key, "Interfaces") == 0) {
		GVariantIter *iter;
		char *v;
		g_variant_get(value, "as", &iter);
		while (g_variant_iter_loop (iter, "s", &v)) {
			g_print("Interface: %s\n", v);
			if (g_strcmp0(v, OFONO_CONNMAN_INTERFACE) == 0)
				g_print("CONNMAN\n");
			check_interfaces(data, v);
		}
		g_variant_iter_free(iter);
	}
}
static void set_properties(GDBusProxy *proxy, struct privdata *data)
{
	g_print("setting props\n");
	if (!data->modem_powered)
		power_modem(proxy, data);
	if (!data->modem_online && data->modem_powered)
		online_modem(proxy, data);
	g_print("all done\n");
}

static void modem_obj_cb(GDBusProxy *proxy, gchar *sender_name,
			 gchar *signal_name, GVariant *parameters,
			 gpointer data)
{
	GVariant *value;
	const char *key;
	struct privdata *priv = data;
	if (g_strcmp0(signal_name, "PropertyChanged") == 0) {
		g_variant_get(parameters, "(sv)", &key, &value);
		g_print("modem property changed: %s\n", key);
		check_modem_property(priv, key, value);
		g_variant_unref(value);
	}
	set_properties(proxy, priv);
}

static void enable_modem(struct privdata *priv, const char *path)
{
	GError *err = NULL;
	modem_power_on();
	priv->modem = g_dbus_proxy_new_for_bus_sync(G_BUS_TYPE_SYSTEM, G_DBUS_PROXY_FLAGS_NONE,
				  NULL, OFONO_SERVICE, path, OFONO_MODEM_INTERFACE,
				  NULL, &err);
	if (err) {
		g_warning("No Modem proxy: %s", err->message);
		g_error_free(err);
		return;
	}
	g_signal_connect(priv->modem, "g-signal", G_CALLBACK (modem_obj_cb), priv);
	get_process_props(priv->modem, priv, check_modem_property);
	set_properties(priv->modem, priv);
}

static void manager_signal_cb(GDBusProxy *mgr, gchar *sender_name,
			      gchar *signal_name, GVariant *parameters,
			      gpointer data)
{
	struct privdata *priv = data;
	const char *obj_path;
	if (g_strcmp0 (signal_name, "ModemAdded") == 0) {
		g_variant_get (parameters, "(oa{sv})", &obj_path, NULL);
		g_print("Modem added: %s\n", obj_path);
		enable_modem(data, obj_path);
	} else if (g_strcmp0 (signal_name, "ModemRemoved") == 0) {
		g_variant_get (parameters, "(o)", &obj_path);
		g_print("Modem removed: %s\n", obj_path);
		modem_power_off();
	}
}

static gboolean check_modem_state(gpointer data)
{
	struct privdata *priv = (struct privdata *)data;
#if 0
	get_process_props(priv->modem, priv, check_modem_property);
#endif
	set_led_state(priv);
	g_print("registered: %d\n", priv->registered);
	g_print("have_connman: %d\n", priv->have_connman);
	g_print("failcount: %d\n", priv->failcount);
	g_print("gprs_attached: %d\n", priv->gprs_attached);
	return TRUE;
}
static gboolean check_connman_powered(gpointer data)
{
	struct privdata *priv = (struct privdata *)data;
	int power_on = 0;
	if (!priv->have_connman)
		priv->ip_configured = 0;

	if (priv->have_connman && (!priv->gprs_attached || !priv->context_active)) {
		priv->ip_configured = 0;
		get_process_props(priv->connman, data, check_connman_prop);
	}
	if (priv->gprs_attached && !priv->context_active) {
		priv->ip_configured = 0;
		get_connection_contexts(priv);
	}
	if (!priv->gprs_powered) {
		priv->ip_configured = 0;
		set_proxy_property(priv->connman, "Powered", g_variant_new_boolean(TRUE));
	}
	if (priv->context_active && !priv->ip_configured) {
		do_ipv4_config();
		do_ipv6_config();
		priv->state = MODEM_GPRS;
		priv->ip_configured = 1;
	}
out:
	return TRUE;
}

static void add_call(GDBusConnection *connection,
		       const gchar *sender_name,
		       const gchar *object_path,
		       const gchar *interface_name,
		       const gchar *signal_name,
		       GVariant *parameters,
		       gpointer userdata)
{
	g_print("AddCall: %s\n", g_variant_get_type_string(parameters));
}

static void remove_call(GDBusConnection *connection,
		       const gchar *sender_name,
		       const gchar *object_path,
		       const gchar *interface_name,
		       const gchar *signal_name,
		       GVariant *parameters,
		       gpointer userdata)
{
	g_print("RemoveCall: %s\n", g_variant_get_type_string(parameters));
}


int main(int argc, char *argv[])
{
	GError *err = NULL;
	struct privdata *priv = &modemdata;
	GVariant *modems;
	char *obj_path;
	GVariantIter *iter;
	GDBusConnection *conn;
	g_type_init();
	gpio_init();
	priv->state = MODEM_INIT;
	loop = g_main_loop_new(NULL, FALSE);
	conn = g_bus_get_sync(G_BUS_TYPE_SYSTEM, NULL, &err);
	if (!conn) {
		g_printerr("Error connecting to D-Bus: %s\n", err->message);
		g_error_free(err);
		return 1;
	}
	err = NULL;
	priv->mgr = g_dbus_proxy_new_sync(conn,
			G_DBUS_PROXY_FLAGS_NONE,
			NULL,
			OFONO_SERVICE,
			OFONO_MANAGER_PATH,
			OFONO_MANAGER_INTERFACE,
			NULL,
			&err);
	if (err)
		g_error("No ofono proxy: %s\n", err->message);
	g_signal_connect(priv->mgr, "g-signal", G_CALLBACK(manager_signal_cb), priv);
	err = NULL;
	modems = g_dbus_proxy_call_sync(priv->mgr, "GetModems", NULL, G_DBUS_CALL_FLAGS_NONE,
			-1, NULL, &err);
	if (err)
		g_error("can't get list of modems %s\n", err->message);
	g_variant_get (modems, "(a(oa{sv}))", &iter);
	while (g_variant_iter_loop (iter, "(oa{sv})", &obj_path, NULL)) {
		g_print("modem: %s\n", obj_path);
		if (!g_strcmp0(obj_path, "/sim900_0")) {
			enable_modem(priv, obj_path);
		}
	}
	g_variant_iter_free(iter);
	g_variant_unref(modems);
	g_timeout_add_seconds(3, check_modem_state, priv);
	g_timeout_add_seconds(6, check_connman_powered, priv);

	g_dbus_connection_signal_subscribe(conn, NULL, OFONO_VOICECALL_INTERFACE, "CallAdded", NULL, NULL, G_DBUS_SIGNAL_FLAGS_NONE,
		add_call, priv, NULL);
	g_dbus_connection_signal_subscribe(conn, NULL, OFONO_VOICECALL_INTERFACE, "CallRemoved", NULL, NULL, G_DBUS_SIGNAL_FLAGS_NONE,
		remove_call, priv, NULL);
	
	g_main_loop_run(loop);
	return 0;
}

