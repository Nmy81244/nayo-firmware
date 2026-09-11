#include "main.h"

HallSensorManager g_hall_sensors;

int main(void) {
    usb_device_init();
    g_hall_sensors.init();

    while(1) {

    }
}