#include <gio/gio.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "privdata.h"
#include "leds.h"

const char *led_path = "/sys/class/leds/crux:yellow";
const char *led_trigger = "trigger";
const char *led_brightness = "brightness";
const char *led_hb = "heartbeat";
const char *led_tm = "timer";
const char *led_tm_on = "delay_on";
const char *led_tm_off = "delay_off";

static void set_led_trigger_heartbeat(void)
{
	char led[128];
	int fd;
	snprintf(led, sizeof(led), "%s/%s", led_path, led_trigger);
	fd = open(led, O_WRONLY);
	write(fd, led_hb, strlen(led_hb));
	close(fd);
}

static void set_led_trigger_timer(int delay_on, int delay_off)
{
	char led[128];
	int fd;
	snprintf(led, sizeof(led), "%s/%s", led_path, led_trigger);
	fd = open(led, O_WRONLY);
	write(fd, led_tm, strlen(led_tm));
	close(fd);
	snprintf(led, sizeof(led), "%s/%s", led_path, led_tm_on);
	fd = open(led, O_WRONLY);
	snprintf(led, sizeof(led), "%d", delay_on);
	write(fd, led, strlen(led));
	close(fd);
	snprintf(led, sizeof(led), "%s/%s", led_path, led_tm_off);
	fd = open(led, O_WRONLY);
	snprintf(led, sizeof(led), "%d", delay_off);
	write(fd, led, strlen(led));
	close(fd);
}

void set_led_state(struct privdata *data)
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

