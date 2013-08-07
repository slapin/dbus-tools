#include <gio/gio.h>
#include "u-boot.h"
#include "debug.h"

/* U-boot environmwnt work */
char *fw_getenv(char *s)
{
	gboolean r;
	char *outp, *errp;
	int status;
	GError *err;
	char *argv[] = {
		"/sbin/fw_printenv",
		"-n",
		s,
		NULL,
	};
	err = NULL;
	r = g_spawn_sync(NULL, argv, NULL, 0, /* flags*/
			NULL, NULL, &outp, &errp,
			&status, &err);
       d_info("u-boot env: %s = %s (%s) status = %d\n", s, outp, errp, status);
       if (!r) {
               if (err)
                       d_info("u-bootenv : error: %s\n", err->message);
		return NULL;
	}else
		return g_strchomp(outp);
}

