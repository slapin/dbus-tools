#include <syslog.h>
#include <stdarg.h>
#include <gio/gio.h>

static void g_log_handler(const char *domain,
		GLogLevelFlags log_level,
		const gchar *message,
		gpointer data)
{
	int logprio;
	switch (log_level) {
		case G_LOG_FLAG_FATAL:
			logprio = LOG_ALERT;
			break;
		case G_LOG_LEVEL_ERROR:
			logprio = LOG_ERR;
			break;
		case G_LOG_LEVEL_CRITICAL:
			logprio = LOG_CRIT;
			break;
		case G_LOG_LEVEL_WARNING:
			logprio = LOG_WARNING;
			break;
		case G_LOG_LEVEL_MESSAGE:
			logprio = LOG_NOTICE;
			break;
		case G_LOG_LEVEL_INFO:
			logprio = LOG_INFO;
			break;
		case G_LOG_LEVEL_DEBUG:
			logprio = LOG_DEBUG;
			break;
		default:
			logprio = LOG_INFO;
			break;
	}
	syslog(logprio, "%s:%s", domain, message);
	if (log_level < G_LOG_LEVEL_INFO)
		g_print("%s:%s\n", domain, message);
}

void dprint(int prio, const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	g_logv("modemd", prio, fmt, ap);
	va_end(ap);
}

void d_info(const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	g_logv("modemd", G_LOG_LEVEL_INFO, fmt, ap);
	va_end(ap);
}

void d_debug(const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	g_logv("modemd", G_LOG_LEVEL_DEBUG, fmt, ap);
	va_end(ap);
}

void d_error(const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	g_logv("modemd", G_LOG_LEVEL_ERROR, fmt, ap);
	va_end(ap);
}

void d_notice(const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	g_logv("modemd", G_LOG_LEVEL_MESSAGE, fmt, ap);
	va_end(ap);
}

void debug_init(void)
{
	openlog("modemd", LOG_PID, LOG_DAEMON);
	g_log_set_default_handler(g_log_handler, NULL);
}

