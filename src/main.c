#include <stdio.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <zephyr/device.h>
#include <zephyr/drivers/pwm.h>
#include <zephyr/drivers/gpio.h>

static const struct pwm_dt_spec pwm_motor = PWM_DT_SPEC_GET(DT_ALIAS(pwm_motor));
static const struct gpio_dt_spec led0 = GPIO_DT_SPEC_GET(DT_ALIAS(led0), gpios);
static const struct gpio_dt_spec motor_in3 = GPIO_DT_SPEC_GET(DT_ALIAS(motor_in3), gpios);
static const struct gpio_dt_spec motor_in4 = GPIO_DT_SPEC_GET(DT_ALIAS(motor_in4), gpios);
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
static void button_pressed(const struct device *dev, struct gpio_callback *cb, uint32_t pins)
{
	struct button_data_t *data = k_malloc(sizeof(struct button_data_t));
	data->cb = cb;
	k_fifo_put(&button_fifo, data);
}

static int board_gpio_init(const struct gpio_dt_spec *spec)
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

static int init_buttom_x(const struct gpio_dt_spec *button, struct gpio_callback *button_cb_data, void *button_pressed)
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

static void toggle_reversed(int reversed)
{
	if (reversed)
	{
		gpio_pin_set_dt(&motor_in3, 0);
		gpio_pin_set_dt(&motor_in4, 1);
	}
	else
	{
		gpio_pin_set_dt(&motor_in3, 1);
		gpio_pin_set_dt(&motor_in4, 0);
	}
}

int main(void)
{
	if (!pwm_is_ready_dt(&pwm_motor))
	{
		printf("Error: PWM device %s is not ready\n",
			   pwm_motor.dev->name);
		return 0;
	}
	board_gpio_init(&led0);
	board_gpio_init(&motor_in3);
	board_gpio_init(&motor_in4);

	init_buttom_x(&button0, &button0_cb_data, button_pressed);
	init_buttom_x(&button1, &button1_cb_data, button_pressed);
	init_buttom_x(&button2, &button2_cb_data, button_pressed);

	printf("board: %s\n", CONFIG_BOARD_TARGET);

	int speed = 1;		  // 速度状态
	int reversed = 0;	  // 反转状态
	int motor_enable = 0; // 电机开关状态
	int64_t last_press_time = 0;
#if 0 
	// 调试时改为1
	gpio_pin_set_dt(&motor_in3, 0);
	gpio_pin_set_dt(&motor_in4, 1);
	pwm_set_pulse_dt(&pwm_motor, pwm_motor.period / 2);
#endif
	while (1)
	{
		struct button_data_t *rx_data = k_fifo_get(&button_fifo, K_FOREVER); // 读取按钮消息
		int64_t now = k_uptime_get();
		if (now - last_press_time < 500) // 抖动消除, 触发间隔小于500ms则忽略
		{
			k_free(rx_data);
			continue;
		}
		else
		{
			last_press_time = now;
		}

		// printf("cb %p\n", rx_data->cb); 调试
		/*
			由于按钮和回调函数绑定,
			所以只要在按钮按下时记录对应回调(rx_data->cb)
			并在这里进行比较就能分辨是哪个按钮按下
		*/
		if (rx_data->cb == &button0_cb_data)

		{
			// 启停
			// printf("button0_cb_data %p\n", &button0_cb_data);
			// gpio_pin_toggle_dt(&led0);
			gpio_pin_set_dt(&led0, !motor_enable);
			motor_enable = gpio_pin_get_dt(&led0); // 如果电平没有修改成功, 那么下次也能向反方向修改电平

			printf("motor_enable %d", motor_enable);
			if (motor_enable)
			{
				toggle_reversed(motor_enable);
			}
			else
			{
				gpio_pin_set_dt(&motor_in3, 0);
				gpio_pin_set_dt(&motor_in4, 0);
			}
		}
		else if (rx_data->cb == &button1_cb_data)
		{
			// 换挡
			// printf("button1_cb_data %p\n", &button1_cb_data);
			printf("speed %d\n", speed);
			uint32_t pulse = pwm_motor.period / speed;
			/*
				高电平时长 = 周期 / 挡位.
				周期已经预设为20ms, 可用代码调整
			*/
			pwm_set_pulse_dt(&pwm_motor, pulse);
			speed++;
			if (speed > 5)
			{
				speed = 1; // 循环调整
			}
		}
		else if (rx_data->cb == &button2_cb_data)
		{
			// 换向
			// printf("button2_cb_data %p\n", &button2_cb_data);
			if (motor_enable) // 电机启动后再调试
			{
				reversed = !reversed;
				printf("reversed %d\n", reversed);
				toggle_reversed(motor_enable);
			}
		}

		k_free(rx_data);
	}
	return 0;
}
