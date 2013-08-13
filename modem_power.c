#include <gio/gio.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static void export_gpio(int gpio)
{
	char num[16];
	int fd = open("/sys/class/gpio/export", O_WRONLY);
	if (fd >0) {
		snprintf(num, sizeof(num) - 1, "%d", gpio);
		write(fd, num, strlen(num));
		close(fd);
	} else
		g_warning("Can't export gpio %d\n", gpio);
}

static void gpio_set_input(int gpio)
{
	char filepath[64];
	int fd;
	snprintf(filepath, sizeof(filepath) - 1,
		"/sys/class/gpio/gpio%d/direction",
		gpio);
	fd = open(filepath, O_WRONLY);
	write(fd, "in", 2);
	close(fd);
}

static void gpio_set_output(int gpio)
{
	char filepath[64];
	int fd;
	snprintf(filepath, sizeof(filepath) - 1,
		"/sys/class/gpio/gpio%d/direction",
		gpio);
	fd = open(filepath, O_WRONLY);
	write(fd, "out", 2);
	close(fd);
}

static int gpio_get_value(int gpio)
{
	char filepath[64];
	char rdbuff[64];
	int fd, ret;
	snprintf(filepath, sizeof(filepath) - 1,
		"/sys/class/gpio/gpio%d/value",
		gpio);
	fd = open(filepath, O_RDONLY);
	read(fd, rdbuff, sizeof(rdbuff));
	close(fd);
	return atoi(rdbuff);
}

static void gpio_set_value(int gpio, int value)
{
	char filepath[64];
	int fd, ret;
	snprintf(filepath, sizeof(filepath) - 1,
		"/sys/class/gpio/gpio%d/value",
		gpio);
	fd = open(filepath, O_WRONLY);
	if (value)
		write(fd, "1", 1);
	else
		write(fd, "0", 1);
	close(fd);
}

void gpio_init(void)
{
	export_gpio(138);
	export_gpio(384);
	export_gpio(390);
	gpio_set_input(138);
	gpio_set_output(384);
	gpio_set_output(390);
}

int modem_check_power(void)
{
	return gpio_get_value(138);
}

static void power_write(int value)
{
	const char *path = "/sys/devices/platform/reg-userspace-consumer.3/state";
	int fd = open(path, O_WRONLY);
	if (value)
		write(fd, "enabled", 7);
	else
		write(fd, "disabled", 8);
	close(fd);
}

void modem_power_on(void)
{
	if (!modem_check_power()) {
		gpio_set_value(390, 0);
		power_write(1);
		g_usleep(1000000);
		gpio_set_value(384, 0);
		g_usleep(1000000);
		gpio_set_value(384, 1);
		g_usleep(2000000);
		while(1) {
			int timeout = 10;
			while(!modem_check_power() && timeout) {
				g_usleep(500000);
				timeout--;
			}
			if (timeout) {
				g_usleep(1000000);
				break;
			}
			gpio_set_value(384, 0);
			g_usleep(1000000);
			gpio_set_value(384, 1);
			g_usleep(2000000);
		}
	}
}
void modem_power_off(void)
{
	power_write(0);
}

void prepare_modem_reflash(void)
{
	modem_power_off();
	gpio_set_output(138);
	gpio_set_value(138, 1);
	gpio_set_value(384, 0);
	power_write(1);
}
void finish_modem_reflash(void)
{
	gpio_set_input(138);
	modem_power_off();
}

