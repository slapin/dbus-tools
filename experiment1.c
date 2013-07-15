#include <stdio.h>
#include <glib.h>
#include <dbus/dbus.h>
#include <dbus/dbus-glib.h>
#include <dbus/dbus-glib-lowlevel.h>
void read_network_manager_state_change(DBusMessage *msg)
{
    DBusError error;
    dbus_error_init(&error);
 
    guint32 state = 0;
 
    if (!dbus_message_get_args(msg, &error, DBUS_TYPE_UINT32, &state,
            DBUS_TYPE_INVALID)) {
        g_error("Cannot read NetworkManager state change message, cause: %s", error.message);
        dbus_error_free(&error);
        return;
    }
    printf("state = %d\n", state);
}
 
DBusHandlerResult signal_filter(DBusConnection *connection, DBusMessage *msg,
        void *user_data)
{
    if (dbus_message_is_signal(msg, "org.freedesktop.NetworkManager",
            "StateChange")) {
        read_network_manager_state_change(msg);
    }
    return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
}
 
 
int main()
{
    GMainLoop *loop = g_main_loop_new(NULL, FALSE);
    DBusError error;
 
    dbus_error_init(&error);
    DBusConnection *conn = dbus_bus_get(DBUS_BUS_SYSTEM, &error);
 
    if (dbus_error_is_set(&error)) {
        g_error("Cannot get System BUS connection: %s", error.message);
        dbus_error_free(&error);
        return 1;
    }
    dbus_connection_setup_with_g_main(conn, NULL);
 
    char *rule = "type='signal',interface='org.freedesktop.NetworkManager'";
    g_message("Signal match rule: %s", rule);
    dbus_bus_add_match(conn, rule, &error);
 
    if (dbus_error_is_set(&error)) {
        g_error("Cannot add D-BUS match rule, cause: %s", error.message);
        dbus_error_free(&error);
        return 1;
    }
 
    g_message("Listening to D-BUS signals using a connection filter");
    dbus_connection_add_filter(conn, signal_filter, NULL, NULL);
 
    g_main_loop_run(loop);
 
    return 0; 
}
