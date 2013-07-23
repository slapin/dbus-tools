#ifndef CONNMAN_H
#define CONNMAN_H
void connman_stuff(struct privdata *data);
void check_connman_prop(struct privdata *data, const char *key, GVariant *value);
#endif

