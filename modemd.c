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

struct privdata {
	GDBusProxy *mgr;
	GDBusProxy *modem;
	GDBusProxy *netreg;
	GDBusProxy *connman;
	gboolean modem_online;
	gboolean modem_powered;
	int registered;
	int have_connman;
	int gprs_attached;
	int gprs_powered;
};
struct privdata modemdata;

static GMainLoop *loop = NULL;
static void gprs_attach_context(struct privdata *data)
{
	g_print("Aieeeee!!!!\n");
}

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
	fd = open(filepath, O_RDONLY);
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

static void check_connman_prop(struct privdata *data, const char *key, GVariant *value)
{
	g_print("value type: %s\n",
		g_variant_get_type_string(value));
	if (g_strcmp0(key, "Attached") == 0) {
		gboolean val;
		g_variant_get(value, "b", &val);
		data->gprs_attached = val;
	} else if (g_strcmp0(key, "Powered") == 0) {
		gboolean val;
		g_variant_get(value, "b", &val);
		data->gprs_powered = val;
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

static void get_process_props(GDBusProxy *proxy, struct privdata *data,
			     void (*func)(struct privdata *data,
					  const char *k, GVariant *val))
{
	GVariant *props = get_proxy_props(proxy);
	if (props)
		process_proplist(data, props, func);
	g_variant_unref(props);
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

static void check_gdbus_error(char *what, GError *err)
{
	if (err) {
		g_warning("%s failed: %s\n",  what, err->message);
		g_error_free(err);
	}
}

static void power_modem(GDBusProxy *proxy, struct privdata *data)
{
	GError *err = NULL;
	GVariant *retv = g_dbus_proxy_call_sync(proxy, "SetProperty",
		 g_variant_new("(sv)", "Powered", g_variant_new_boolean(TRUE)),
		 G_DBUS_CALL_FLAGS_NONE, 120000000, NULL, &err);
	if (err) {
		check_gdbus_error("SetProperty Powered", err);
		return;
	}
	if (retv)
		g_print("value type: %s\n",
			g_variant_get_type_string(retv));
}
static void online_modem(GDBusProxy *proxy, struct privdata *data)
{
	GError *err = NULL;
	GVariant *retv = g_dbus_proxy_call_sync(proxy, "SetProperty",
		 g_variant_new("(sv)", "Online", g_variant_new_boolean(TRUE)),
		 G_DBUS_CALL_FLAGS_NONE, 120000000, NULL, &err);
	if (err) {
		check_gdbus_error("SetProperty Online", err);
		return;
	}
	if (retv)
		g_print("value type: %s\n",
			g_variant_get_type_string(retv));
}

static void check_interfaces(struct privdata *data, const char *iface)
{
	int have_connman = 0;
	if (g_strcmp0(iface, OFONO_NETREG_INTERFACE) == 0)
		netreg_stuff(data);
	else if (g_strcmp0(iface, OFONO_CONNMAN_INTERFACE) == 0)
		have_connman = 1;
	data->have_connman = have_connman;
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
static gboolean check_modem_attach(gpointer userdata)
{
	struct privdata *priv = userdata;
	GError *err;
	if (priv->have_connman)
		connman_stuff(priv);
	return TRUE;
}
int main()
{
	GError *err = NULL;
	struct privdata *priv = &modemdata;
	GVariant *modems;
	char *obj_path;
	GVariantIter *iter;
	g_type_init();
	gpio_init();
	loop = g_main_loop_new(NULL, FALSE);
	priv->mgr = g_dbus_proxy_new_for_bus_sync(G_BUS_TYPE_SYSTEM,
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
	g_timeout_add_seconds(1, check_modem_attach, priv);
	
	g_main_loop_run(loop);
	return 0;
}

