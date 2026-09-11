#ifndef BOARD_PINOUT_H
#define BOARD_PINOUT_H

#include "at32f402_405.h"

// Hall Linear Sensors (MT9105)
#define HALL_1_PORT         GPIOA
#define HALL_1_PIN          GPIO_PINS_0
#define HALL_1_ADC_CHANNEL  ADC_CHANNEL_0

#define HALL_2_PORT         GPIOA
#define HALL_2_PIN          GPIO_PINS_1
#define HALL_2_ADC_CHANNEL  ADC_CHANNEL_1

#define HALL_3_PORT         GPIOA
#define HALL_3_PIN          GPIO_PINS_2
#define HALL_3_ADC_CHANNEL  ADC_CHANNEL_2

#define HALL_4_PORT         GPIOA
#define HALL_4_PIN          GPIO_PINS_3
#define HALL_4_ADC_CHANNEL  ADC_CHANNEL_3

// EC11 Encoder
#define ENCODER_A_PORT      GPIOB
#define ENCODER_A_PIN       GPIO_PINS_0
#define ENCODER_B_PORT      GPIOB
#define ENCODER_B_PIN       GPIO_PINS_1
#define ENCODER_SW_PORT     GPIOB
#define ENCODER_SW_PIN      GPIO_PINS_2

// Buttons
#define BUTTON_LED_PORT      GPIOA
#define BUTTON_LED_PIN       GPIO_PINS_8
#define BUTTON_SET_PORT      GPIOA
#define BUTTON_SET_PIN       GPIO_PINS_9
#define BUTTON_LAY_PORT      GPIOA
#define BUTTON_LAY_PIN       GPIO_PINS_10

// OLED SSD1306 (I2C1)
#define OLED_I2C            I2C1
#define OLED_I2C_PORT       GPIOB
#define OLED_I2C_ADDRESS    0x3C
#define OLED_SCL_PIN        GPIO_PINS_6
#define OLED_SDA_PIN        GPIO_PINS_7

// WS2812B LEDs
#define RGB_LED_PORT        GPIOA
#define RGB_LED_PIN         GPIO_PINS_15

// USB 2.0 HS
#define USB_DM_PORT         GPIOB
#define USB_DM_PIN          GPIO_PINS_14
#define USB_DP_PORT         GPIOB
#define USB_DP_PIN          GPIO_PINS_15

#endif