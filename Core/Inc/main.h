#ifndef MAIN_H
#define MAIN_H

#ifdef __cplusplus
extern "C" {
#endif

// System and AT32 BSP
#include "at32f402_405.h"
#include "at32f402_405_gpio.h"
#include "at32f402_405_adc.h"
#include "at32f402_405_dma.h"
#include "at32f402_405_i2c.h"
#include "at32f402_405_tmr.h"
#include "at32f402_405_crm.h"

#ifdef __cplusplus
}
#endif

// C++ STD
#include <cstdint>
#include <cmath>
#include <algorithm>
#include <array>

// Nayo Modules
#include "board_pinout.h"
#include "hall_sensor.h"
#include "rapid_trigger.h"
#include "encoder.h"
#include "oled_ssd1306.h"
#include "ws2812.h"
#include "usb_device.h"

#endif