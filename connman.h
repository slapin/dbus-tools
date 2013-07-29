#ifndef CONNMAN_H
#define CONNMAN_H
void connman_stuff(struct modemdata *data);
void check_connman_prop(void *data, const char *key, GVariant *value);
#endif

