#include "main.h"

HallSensorManager g_hall_sensors;

static void board_gpio_init(void)
{
    crm_periph_clock_enable(CRM_GPIOB_PERIPH_CLOCK, TRUE);

    gpio_pin_mux_config(GPIOB, GPIO_PINS_SOURCE3, GPIO_MUX_0);
    gpio_pin_mux_config(GPIOB, GPIO_PINS_SOURCE4, GPIO_MUX_0);

    gpio_init_type gpio_init_struct;
    gpio_default_para_init(&gpio_init_struct);
    gpio_init_struct.gpio_pins = GPIO_PB3_PIN | GPIO_PB4_PIN;
    gpio_init_struct.gpio_mode = GPIO_MODE_INPUT;
    gpio_init_struct.gpio_pull = GPIO_PULL_NONE;
    gpio_init(GPIOB, &gpio_init_struct);
}

int main(void) {
    board_gpio_init();
    usb_device_init();
    g_hall_sensors.init();
    g_ws2812.init();
    g_ws2812.clear();
    g_ws2812.show();

    while(1) {
        g_ws2812.update();
    }
}
