#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

#define BLINK_PERIOD_MS 500

static const struct gpio_dt_spec led = GPIO_DT_SPEC_GET(DT_ALIAS(led0), gpios);

int main(void)
{
	bool on = true;

	printk("Hello from %s\n", CONFIG_BOARD_TARGET);

	if (!gpio_is_ready_dt(&led)) {
		printk("LED device not ready\n");
		return 0;
	}

	if (gpio_pin_configure_dt(&led, GPIO_OUTPUT_ACTIVE) < 0) {
		printk("Failed to configure LED\n");
		return 0;
	}

	while (true) {
		gpio_pin_set_dt(&led, on);
		printk("LED %s\n", on ? "on" : "off");
		on = !on;
		k_msleep(BLINK_PERIOD_MS);
	}

	return 0;
}
