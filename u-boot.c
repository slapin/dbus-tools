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
		"fw_printenv",
		"-n",
		s,
		NULL,
	};
	r = g_spawn_sync(NULL, argv, NULL, 0, /* flags*/
			NULL, NULL, &outp, &errp,
			&status, &err);
	d_info("u-boot env: %s = %s\n", s, outp);
	if (!r)
		return NULL;
	else
		return outp;
}

