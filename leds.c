#include <gio/gio.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "privdata.h"
#include "leds.h"

extern const char *gsmledfile;
extern const char *clientledfile;
const char *led_trigger = "trigger";
const char *led_brightness = "brightness";
const char *led_hb = "heartbeat";
const char *led_tm = "timer";
const char *led_tm_on = "delay_on";
const char *led_tm_off = "delay_off";
const char *led_none = "none";

static void set_led_trigger_heartbeat(void)
{
	char led[128];
	int fd;
	snprintf(led, sizeof(led), "%s/%s", gsmledfile, led_trigger);
	fd = open(led, O_WRONLY);
	write(fd, led_hb, strlen(led_hb));
	close(fd);
}

static void set_led_trigger_timer(int delay_on, int delay_off)
{
	char led[128];
	int fd;
	snprintf(led, sizeof(led), "%s/%s", gsmledfile, led_trigger);
	fd = open(led, O_WRONLY);
	write(fd, led_tm, strlen(led_tm));
	close(fd);
	snprintf(led, sizeof(led), "%s/%s", gsmledfile, led_tm_on);
	fd = open(led, O_WRONLY);
	snprintf(led, sizeof(led), "%d", delay_on);
	write(fd, led, strlen(led));
	close(fd);
	snprintf(led, sizeof(led), "%s/%s", gsmledfile, led_tm_off);
	fd = open(led, O_WRONLY);
	snprintf(led, sizeof(led), "%d", delay_off);
	write(fd, led, strlen(led));
	close(fd);
}
static void set_cled_trigger_none(void)
{
	char led[128];
	int fd;
	snprintf(led, sizeof(led), "%s/%s", clientledfile, led_trigger);
	fd = open(led, O_WRONLY);
	write(fd, led_none, strlen(led_none));
	close(fd);
}
static void set_cled(int br)
{
	char led[128];
	int fd;
	char bright[32];
	snprintf(led, sizeof(led), "%s/%s", clientledfile, led_brightness);
	fd = open(led, O_WRONLY);
	snprintf(bright, sizeof(bright), "%d", br);
	write(fd,  bright, strlen(bright));
	close(fd);
}

static void set_cled_on(void)
{
	set_cled(1);
}

static void set_cled_off(void)
{
	set_cled(0);
}


void set_led_state(struct modemdata *data)
{
	static int oldstate = -1;
	if (oldstate == data->state)
		return;
	switch(data->state) {
		case MODEM_INIT:
			set_led_trigger_heartbeat();
			break;
		case MODEM_CONNMAN:
			set_led_trigger_timer(600, 400);
			break;
		case MODEM_GPRS:
			set_led_trigger_timer(200, 100);
			break;
	}
}

int cstate = -1;
void set_cled_state(struct modemdata *data)
{
	if (cstate == -1)
		set_cled_trigger_none();
	if (data->cstate != cstate) {
		if (data->cstate)
			set_cled_on();
		else
			set_cled_off();
		cstate = data->cstate;
	}
}


