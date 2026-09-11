#ifndef USB_DEVICE_H
#define USB_DEVICE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void usb_device_init(void);
void usb_device_irq_handler(void);

void usb_hid_keyboard_set_keys(uint8_t key_mask);
uint8_t usb_vendor_send(const uint8_t *data, uint8_t length);
uint8_t usb_vendor_read(uint8_t *data, uint8_t capacity);

#ifdef __cplusplus
}
#endif

#endif
