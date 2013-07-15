#include <stdio.h>
#include <glib.h>
#include <dbus/dbus.h>
#include <dbus/dbus-glib.h>
#include <dbus/dbus-glib-lowlevel.h>
 
void state_changed_callback(DBusGProxy *proxy, guint32 state,
        gpointer user_data)
{
    printf("state %d\n", state);
}
 
int main()
{
    GMainLoop *loop = g_main_loop_new(NULL, FALSE);
 
    g_type_init();
 
    GError *error = NULL;
    DBusGConnection *conn = dbus_g_bus_get(DBUS_BUS_SYSTEM, &error);
 
    if (error != NULL) {
        g_error("D-BUS Connection error: %s", error->message);
        g_error_free(error);
    }
 
    if (!conn) {
        g_error("D-BUS connection cannot be created");
        return 1;
    }
 
    DBusGProxy *proxy = dbus_g_proxy_new_for_name(conn,
            "org.freedesktop.NetworkManager",
            "/org/freedesktop/NetworkManager",
            "org.freedesktop.NetworkManager");
 
    if (!proxy) {
        g_error("Cannot create proxy");
        return 1;
    }
 
    dbus_g_proxy_add_signal(proxy, "StateChange", G_TYPE_UINT,
            G_TYPE_INVALID);
 
    dbus_g_proxy_connect_signal(proxy, "StateChange",
            G_CALLBACK(state_changed_callback), NULL, NULL);
 
    g_message("Waiting D-BUS proxy callback for signal");
    g_main_loop_run(loop);
 
    return 0;
}

