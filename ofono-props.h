#ifndef OFONO_PROPS_H
#define OFONO_PROPS_H
void get_process_props(GDBusProxy *proxy, void *data,
			     void (*func)(void *data,
					  const char *k, GVariant *val));
int set_proxy_property(GDBusProxy *proxy,
				const char *prop,
				GVariant *val);
#endif
