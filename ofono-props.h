#ifndef OFONO_PROPS_H
#define OFONO_PROPS_H
void get_process_props(GDBusProxy *proxy, struct privdata *data,
			     void (*func)(struct privdata *data,
					  const char *k, GVariant *val));
int set_proxy_property(GDBusProxy *proxy,
				const char *prop,
				GVariant *val);
#endif
