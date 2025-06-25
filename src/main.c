#include <stdio.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <zephyr/device.h>
#include <zephyr/drivers/pwm.h>
#include <zephyr/drivers/gpio.h>

static const struct pwm_dt_spec pwm_moto = PWM_DT_SPEC_GET(DT_ALIAS(pwm_moto));
static const struct gpio_dt_spec led0 = GPIO_DT_SPEC_GET(DT_ALIAS(led0), gpios);
static const struct gpio_dt_spec moto_in3 = GPIO_DT_SPEC_GET(DT_ALIAS(moto_in3), gpios);
static const struct gpio_dt_spec moto_in4 = GPIO_DT_SPEC_GET(DT_ALIAS(moto_in4), gpios);
static const struct gpio_dt_spec button0 = GPIO_DT_SPEC_GET_OR(DT_ALIAS(sw0), gpios, {0});
static const struct gpio_dt_spec button1 = GPIO_DT_SPEC_GET_OR(DT_ALIAS(sw1), gpios, {0});
static const struct gpio_dt_spec button2 = GPIO_DT_SPEC_GET_OR(DT_ALIAS(sw2), gpios, {0});
static struct gpio_callback button0_cb_data;
static struct gpio_callback button1_cb_data;
static struct gpio_callback button2_cb_data;

struct button_data_t
{
	struct gpio_callback *cb;
};

K_FIFO_DEFINE(button_fifo);
void button_pressed(const struct device *dev, struct gpio_callback *cb, uint32_t pins)
{
	struct button_data_t *data = k_malloc(sizeof(struct button_data_t));
	data->cb = cb;
	k_fifo_put(&button_fifo, data);
}

int board_gpio_init(const struct gpio_dt_spec *spec)
{
	int ret;
	ret = gpio_is_ready_dt(spec);
	if (ret < 0)
	{
		printf("Error: led0 is not ready\n");
		return ret;
	}

	ret = gpio_pin_configure_dt(spec, GPIO_OUTPUT_ACTIVE);
	if (ret < 0)
	{
		return ret;
	}
	return 0;
}

int init_buttom_x(const struct gpio_dt_spec *button, struct gpio_callback *button_cb_data, void *button_pressed)
{
	int ret;

	if (!gpio_is_ready_dt(button))
	{
		printf("Error: button device %s is not ready\n",
			   button->port->name);
		return 0;
	}

	ret = gpio_pin_configure_dt(button, GPIO_INPUT);
	if (ret != 0)
	{
		printf("Error %d: failed to configure %s pin %d\n",
			   ret, button->port->name, button->pin);
		return 0;
	}

	ret = gpio_pin_interrupt_configure_dt(button,
										  GPIO_INT_EDGE_TO_ACTIVE);

	if (ret != 0)
	{
		printf("Error %d: failed to configure interrupt on %s pin %d\n",
			   ret, button->port->name, button->pin);
		return 0;
	}

	gpio_init_callback(button_cb_data, button_pressed, BIT(button->pin));
	gpio_add_callback(button->port, button_cb_data);
	printf("Set up button at %s pin %d\n", button->port->name, button->pin);
	return 0;
}
int main(void)
{
	if (!pwm_is_ready_dt(&pwm_moto))
	{
		printf("Error: PWM device %s is not ready\n",
			   pwm_moto.dev->name);
		return 0;
	}
	board_gpio_init(&led0);
	board_gpio_init(&moto_in3);
	board_gpio_init(&moto_in4);

	init_buttom_x(&button0, &button0_cb_data, button_pressed);
	init_buttom_x(&button1, &button1_cb_data, button_pressed);
	init_buttom_x(&button2, &button2_cb_data, button_pressed);

	printf("board: %s\n", CONFIG_BOARD_TARGET);

	int speed = 1;
	int reversed = 0;
	int moto_enable = 0;
	while (1)
	{
		struct button_data_t *rx_data = k_fifo_get(&button_fifo, K_FOREVER);
		printf("cb %p\n", rx_data->cb);

		if (rx_data->cb == &button0_cb_data)
		{
			// 启停
			// printf("button0_cb_data %p\n", &button0_cb_data);
			gpio_pin_toggle_dt(&led0);
			moto_enable = gpio_pin_get_dt(&led0);

			if (moto_enable)
			{
				if (reversed)
				{
					gpio_pin_set_dt(&moto_in3, 0);
					gpio_pin_set_dt(&moto_in4, 1);
				}
				else
				{
					gpio_pin_set_dt(&moto_in3, 1);
					gpio_pin_set_dt(&moto_in4, 0);
				}
			}
			else
			{
				gpio_pin_set_dt(&moto_in3, 0);
				gpio_pin_set_dt(&moto_in4, 0);
			}
		}
		else if (rx_data->cb == &button1_cb_data)
		{
			// 换挡
			// printf("button1_cb_data %p\n", &button1_cb_data);
			printf("speed %d\n", speed);
			uint32_t pulse = pwm_moto.period / speed;
			pwm_set_pulse_dt(&pwm_moto, pulse);
			speed++;
			if (speed > 5)
			{
				speed = 1;
			}
		}
		else if (rx_data->cb == &button2_cb_data)
		{
			// 换向
			// printf("button2_cb_data %p\n", &button2_cb_data);
			if (moto_enable)
			{
				reversed = !reversed;
				if (reversed)
				{
					gpio_pin_set_dt(&moto_in3, 0);
					gpio_pin_set_dt(&moto_in4, 1);
				}
				else
				{
					gpio_pin_set_dt(&moto_in3, 1);
					gpio_pin_set_dt(&moto_in4, 0);
				}
			}
		}

		k_free(rx_data);
	}
	return 0;
}
