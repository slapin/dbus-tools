#ifndef MODEM_POWER_H
#define MODEM_POWER_H
void modem_power_off(void);
void modem_power_on(void);
void gpio_init(void);
int modem_check_power(void);
void prepare_modem_reflash(void);
void finish_modem_reflash(void);
#endif
