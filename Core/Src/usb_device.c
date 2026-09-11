#include "usb_device.h"

#include "at32f402_405_conf.h"
#include "at32f402_405_crm.h"
#include "at32f402_405_gpio.h"
#include "at32f402_405_misc.h"
#include "at32f402_405_usb.h"
#include "board_pinout.h"

#define USB_EP0_MPS             64u
#define USB_KEYBOARD_EP         1u
#define USB_VENDOR_IN_EP        2u
#define USB_VENDOR_OUT_EP       3u
#define USB_KEYBOARD_REPORT_LEN 8u
#define USB_VENDOR_REPORT_LEN   32u

static const uint8_t device_descriptor[] = {
  18, 1, 0x00, 0x02, 0, 0, 0, USB_EP0_MPS, 0xC0, 0x16, 0x05, 0x40, 0x01, 0x00, 0, 0, 0, 1
};

static const uint8_t configuration_descriptor[] = {
  9, 2, 0x42, 0x00, 2, 1, 0, 0xA0, 0x32,
  9, 4, 0, 0, 1, 3, 1, 1, 0,
  9, 0x21, 0x11, 0x01, 0, 1, 0x22, 0x3F, 0x00,
  7, 5, 0x81, 3, 8, 0, 1,
  9, 4, 1, 0, 2, 3, 0, 0, 0,
  9, 0x21, 0x11, 0x01, 0, 0, 0x22, 0x1D, 0x00,
  7, 5, 0x82, 3, 0x20, 0, 1,
  7, 5, 0x03, 3, 0x20, 0, 1
};

static const uint8_t device_qualifier_descriptor[] = {
  10, 6, 0x00, 0x02, 0, 0, 0, USB_EP0_MPS, 1, 0
};

static const uint8_t keyboard_report_descriptor[] = {
  0x05, 0x01, 0x09, 0x06, 0xA1, 0x01, 0x05, 0x07,
  0x19, 0xE0, 0x29, 0xE7, 0x15, 0x00, 0x25, 0x01,
  0x75, 0x01, 0x95, 0x08, 0x81, 0x02, 0x95, 0x01,
  0x75, 0x08, 0x81, 0x01, 0x95, 0x05, 0x75, 0x01,
  0x05, 0x08, 0x19, 0x01, 0x29, 0x05, 0x91, 0x02,
  0x95, 0x01, 0x75, 0x03, 0x91, 0x01, 0x95, 0x06,
  0x75, 0x08, 0x15, 0x00, 0x25, 0x65, 0x05, 0x07,
  0x19, 0x00, 0x29, 0x65, 0x81, 0x00, 0xC0
};

static const uint8_t vendor_report_descriptor[] = {
  0x06, 0x00, 0xFF, 0x09, 0x01, 0xA1, 0x01, 0x15,
  0x00, 0x26, 0xFF, 0x00, 0x75, 0x08, 0x95, 0x20,
  0x09, 0x01, 0x81, 0x02, 0x75, 0x08, 0x95, 0x20,
  0x09, 0x02, 0x91, 0x02, 0xC0
};

static otg_global_type *const usb = OTG2_GLOBAL;
static usb_ept_info ep0_in;
static usb_ept_info ep0_out;
static usb_ept_info keyboard_in;
static usb_ept_info vendor_in;
static usb_ept_info vendor_out;

static uint8_t setup_packet[8];
static const uint8_t *control_data;
static uint16_t control_length;
static uint16_t control_offset;
static uint8_t control_zlp;
static uint8_t control_out_pending;
static uint8_t pending_address;
static uint8_t configured;
static uint8_t keyboard_busy;
static uint8_t vendor_busy;
static uint8_t keyboard_report[USB_KEYBOARD_REPORT_LEN];
static uint8_t vendor_in_report[USB_VENDOR_REPORT_LEN];
static uint8_t vendor_out_report[USB_VENDOR_REPORT_LEN];
static uint8_t vendor_out_length;

static void endpoint_info_init(usb_ept_info *ep, uint8_t number, uint8_t address,
                               uint8_t direction, uint8_t type, uint16_t max_packet)
{
  ep->eptn = number;
  ep->ept_address = address;
  ep->inout = direction;
  ep->trans_type = type;
  ep->maxpacket = max_packet;
  ep->trans_buf = 0;
  ep->total_len = 0;
  ep->trans_len = 0;
  ep->stall = 0;
}

static void control_arm_out(uint16_t length)
{
  otg_eptout_type *ep = USB_OUTEPT(usb, 0);
  ep->doeptsiz = 0;
  ep->doeptsiz_bit.pktcnt = 1;
  ep->doeptsiz_bit.xfersize = length > USB_EP0_MPS ? USB_EP0_MPS : length;
  ep->doepctl_bit.cnak = TRUE;
  ep->doepctl_bit.eptena = TRUE;
}

static void control_fill_fifo(void)
{
  otg_eptin_type *ep = USB_INEPT(usb, 0);
  uint16_t remaining = (uint16_t)(control_length - control_offset);
  uint16_t packet_length = remaining > USB_EP0_MPS ? USB_EP0_MPS : remaining;

  if(packet_length != 0)
  {
    usb_write_packet(usb, (uint8_t *)&control_data[control_offset], 0, packet_length);
    control_offset = (uint16_t)(control_offset + packet_length);
  }
  else if(control_zlp)
  {
    control_zlp = 0;
  }

  if((ep->dtxfsts & USB_OTG_DTXFSTS_INEPTFSAV) <= ((packet_length + 3u) / 4u))
  {
    return;
  }
  if(control_offset >= control_length)
  {
    OTG_DEVICE(usb)->diepempmsk &= ~(1u << 0);
  }
}

static void control_send(const uint8_t *data, uint16_t length, uint16_t requested_length)
{
  otg_eptin_type *ep = USB_INEPT(usb, 0);
  control_data = data;
  control_length = length < requested_length ? length : requested_length;
  control_offset = 0;
  control_zlp = (control_length != 0 && (control_length % USB_EP0_MPS) == 0 &&
                requested_length > control_length) ? 1 : 0;

  ep->dieptsiz = 0;
  ep->dieptsiz_bit.pktcnt = control_length == 0 ? 1 : (uint32_t)((control_length + USB_EP0_MPS - 1) / USB_EP0_MPS);
  ep->dieptsiz_bit.xfersize = control_length;
  ep->diepctl_bit.cnak = TRUE;
  ep->diepctl_bit.eptena = TRUE;
  if(control_length != 0)
  {
    OTG_DEVICE(usb)->diepempmsk |= 1u;
  }
  else
  {
    control_arm_out(0);
  }
  control_fill_fifo();
}

static void control_send_status(void)
{
  control_send(0, 0, 0);
}

static void endpoint_send(usb_ept_info *info, uint8_t *data, uint16_t length)
{
  otg_eptin_type *ep = USB_INEPT(usb, info->eptn);
  ep->dieptsiz = 0;
  ep->dieptsiz_bit.pktcnt = 1;
  ep->dieptsiz_bit.xfersize = length;
  ep->diepctl_bit.cnak = TRUE;
  ep->diepctl_bit.eptena = TRUE;
  usb_write_packet(usb, data, info->eptn, length);
}

static void endpoint_arm_out(usb_ept_info *info)
{
  otg_eptout_type *ep = USB_OUTEPT(usb, info->eptn);
  ep->doeptsiz = 0;
  ep->doeptsiz_bit.pktcnt = 1;
  ep->doeptsiz_bit.xfersize = info->maxpacket;
  ep->doepctl_bit.cnak = TRUE;
  ep->doepctl_bit.eptena = TRUE;
}

static const uint8_t *get_descriptor(uint8_t type, uint8_t index, uint16_t *length)
{
  (void)index;
  switch(type)
  {
    case 1: *length = sizeof(device_descriptor); return device_descriptor;
    case 2: *length = sizeof(configuration_descriptor); return configuration_descriptor;
    case 6: *length = sizeof(device_qualifier_descriptor); return device_qualifier_descriptor;
    case 0x22:
      if(index == 0) { *length = sizeof(keyboard_report_descriptor); return keyboard_report_descriptor; }
      if(index == 1) { *length = sizeof(vendor_report_descriptor); return vendor_report_descriptor; }
      break;
    default:
      break;
  }
  return 0;
}

static void handle_setup(void)
{
  uint8_t request_type = setup_packet[0];
  uint8_t request = setup_packet[1];
  uint16_t value = (uint16_t)setup_packet[2] | ((uint16_t)setup_packet[3] << 8);
  uint16_t index = (uint16_t)setup_packet[4] | ((uint16_t)setup_packet[5] << 8);
  uint16_t length = (uint16_t)setup_packet[6] | ((uint16_t)setup_packet[7] << 8);
  const uint8_t *data;
  uint16_t data_length;

  if((request_type & 0x60) == 0)
  {
    switch(request)
    {
      case 0x00:
        if(length == 2) { static const uint8_t zero_status[2] = {0, 0}; control_send(zero_status, 2, length); }
        else { control_send_status(); }
        return;
      case 0x05:
        pending_address = (uint8_t)value;
        control_send_status();
        return;
      case 0x06:
        if((value >> 8) == 0x21)
        {
          static const uint8_t keyboard_hid[] = {9, 0x21, 0x11, 1, 0, 1, 0x22, sizeof(keyboard_report_descriptor), 0};
          static const uint8_t vendor_hid[] = {9, 0x21, 0x11, 1, 0, 0, 0x22, sizeof(vendor_report_descriptor), 0};
          control_send(index == 0 ? keyboard_hid : vendor_hid, 9, length);
          return;
        }
        data = get_descriptor((uint8_t)(value >> 8), (uint8_t)value, &data_length);
        if(data != 0) { control_send(data, data_length, length); return; }
        break;
      case 0x08:
        { static uint8_t configuration = 0; configuration = configured; control_send(&configuration, 1, length); return; }
      case 0x09:
        if(configured == 1 && value == 0)
        {
          usb_ept_close(usb, &keyboard_in);
          usb_ept_close(usb, &vendor_in);
          usb_ept_close(usb, &vendor_out);
        }
        configured = (uint8_t)value;
        if(configured == 1)
        {
          usb_ept_open(usb, &keyboard_in);
          usb_ept_open(usb, &vendor_in);
          usb_ept_open(usb, &vendor_out);
          endpoint_arm_out(&vendor_out);
        }
        control_send_status();
        return;
      case 0x0A:
        { static const uint8_t alt = 0; control_send(&alt, 1, length); return; }
      case 0x0B:
        control_send_status();
        return;
      case 0x01:
      case 0x03:
        if((request_type & 0x1F) == 2) { control_send_status(); return; }
        break;
      default:
        break;
    }
  }
  else if((request_type & 0x60) == 0x20)
  {
    if(request == 0x0A || request == 0x0B)
    {
      control_send_status();
      return;
    }
    if(request == 0x01 && (request_type & 0x80) != 0)
    {
      static const uint8_t empty_report[USB_VENDOR_REPORT_LEN] = {0};
      control_send(empty_report, sizeof(empty_report), length);
      return;
    }
    if(request == 0x09 && (request_type & 0x80) == 0)
    {
      control_out_pending = 1;
      control_arm_out(length);
      return;
    }
  }
  control_send_status();
}

static void handle_rx_fifo(void)
{
  uint32_t status;
  uint8_t endpoint;
  uint16_t count;
  uint8_t packet_status;

  while((usb->gintsts & USB_OTG_RXFLVL_FLAG) != 0)
  {
    status = usb->grxstsp;
    endpoint = (uint8_t)(status & USB_OTG_GRXSTSP_EPTNUM);
    count = (uint16_t)((status & USB_OTG_GRXSTSP_BCNT) >> 4);
    packet_status = (uint8_t)((status & USB_OTG_GRXSTSP_PKTSTS) >> 17);

    if(packet_status == USB_SETUP_STS_DATA && endpoint == 0)
    {
      usb_read_packet(usb, setup_packet, 0, count > sizeof(setup_packet) ? sizeof(setup_packet) : count);
      handle_setup();
    }
    else if(packet_status == USB_OUT_STS_DATA)
    {
      if(endpoint == 0 && control_out_pending != 0)
      {
        uint16_t received = count > USB_VENDOR_REPORT_LEN ? USB_VENDOR_REPORT_LEN : count;
        usb_read_packet(usb, vendor_out_report, 0, received);
        vendor_out_length = (uint8_t)received;
        control_out_pending = 0;
        control_send_status();
      }
      else if(endpoint == USB_VENDOR_OUT_EP)
      {
        uint16_t received = count > USB_VENDOR_REPORT_LEN ? USB_VENDOR_REPORT_LEN : count;
        usb_read_packet(usb, vendor_out_report, endpoint, received);
        vendor_out_length = (uint8_t)received;
        endpoint_arm_out(&vendor_out);
      }
      else if(count != 0)
      {
        uint8_t discard[USB_VENDOR_REPORT_LEN];
        usb_read_packet(usb, discard, endpoint, count > sizeof(discard) ? sizeof(discard) : count);
      }
    }
  }
}

static void handle_in_endpoints(void)
{
  uint32_t pending = usb_get_all_in_interrupt(usb);
  while(pending != 0)
  {
    uint8_t endpoint = 0;
    while((pending & 1u) == 0) { pending >>= 1; ++endpoint; }
    uint32_t flags = usb_ept_in_interrupt(usb, endpoint);
    if((flags & USB_OTG_DIEPINT_TXFEMP_FLAG) != 0 && endpoint == 0)
    {
      control_fill_fifo();
    }
    if((flags & USB_OTG_DIEPINT_XFERC_FLAG) != 0)
    {
      usb_ept_in_clear(usb, endpoint, USB_OTG_DIEPINT_XFERC_FLAG);
      if(endpoint == 0)
      {
        OTG_DEVICE(usb)->diepempmsk &= ~1u;
        if(control_offset >= control_length && control_zlp == 0)
        {
          if(pending_address != 0)
          {
            usb_set_address(usb, pending_address);
            pending_address = 0;
          }
          control_arm_out(0);
        }
      }
      else if(endpoint == USB_KEYBOARD_EP) { keyboard_busy = 0; }
      else if(endpoint == USB_VENDOR_IN_EP) { vendor_busy = 0; }
    }
    if((flags & USB_OTG_DIEPINT_TXFEMP_FLAG) != 0 && endpoint != 0)
    {
      usb_ept_in_clear(usb, endpoint, USB_OTG_DIEPINT_TXFEMP_FLAG);
    }
    pending >>= 1;
  }
}

static void handle_out_endpoints(void)
{
  uint32_t pending = usb_get_all_out_interrupt(usb);
  while(pending != 0)
  {
    uint8_t endpoint = 0;
    while((pending & 1u) == 0) { pending >>= 1; ++endpoint; }
    uint32_t flags = usb_ept_out_interrupt(usb, endpoint);
    if((flags & USB_OTG_DOEPINT_XFERC_FLAG) != 0)
    {
      usb_ept_out_clear(usb, endpoint, USB_OTG_DOEPINT_XFERC_FLAG);
      if(endpoint == 0)
      {
        control_arm_out(0);
      }
    }
    pending >>= 1;
  }
}

void usb_device_irq_handler(void)
{
  uint32_t interrupts = usb_global_get_all_interrupt(usb);

  if((interrupts & USB_OTG_USBRST_FLAG) != 0)
  {
    usb_global_clear_interrupt(usb, USB_OTG_USBRST_FLAG);
    configured = 0;
    pending_address = 0;
    OTG_DEVICE(usb)->daintmsk = 0x00010001u;
    OTG_DEVICE(usb)->doepmsk_bit.xfercmsk = TRUE;
    OTG_DEVICE(usb)->doepmsk_bit.setupmsk = TRUE;
    OTG_DEVICE(usb)->diepmsk_bit.xfercmsk = TRUE;
    usb_flush_tx_fifo(usb, 0x10);
    usb_ept0_start(usb);
    control_arm_out(0);
  }
  if((interrupts & USB_OTG_ENUMDONE_FLAG) != 0)
  {
    usb_global_clear_interrupt(usb, USB_OTG_ENUMDONE_FLAG);
    usb_ept_close(usb, &ep0_in);
    usb_ept_close(usb, &ep0_out);
    usb_ept_open(usb, &ep0_in);
    usb_ept_open(usb, &ep0_out);
    usb_ept0_setup(usb);
  }
  if((interrupts & USB_OTG_RXFLVL_FLAG) != 0)
  {
    usb->gintmsk &= ~USB_OTG_RXFLVL_INT;
    handle_rx_fifo();
    usb->gintmsk |= USB_OTG_RXFLVL_INT;
  }
  if((interrupts & USB_OTG_IEPT_FLAG) != 0) { handle_in_endpoints(); }
  if((interrupts & USB_OTG_OEPT_FLAG) != 0) { handle_out_endpoints(); }
  if((interrupts & USB_OTG_USBSUSP_FLAG) != 0) { usb_global_clear_interrupt(usb, USB_OTG_USBSUSP_FLAG); }
  if((interrupts & USB_OTG_WKUP_FLAG) != 0) { usb_global_clear_interrupt(usb, USB_OTG_WKUP_FLAG); }
}

void OTGHS_IRQHandler(void)
{
  usb_device_irq_handler();
}

void usb_device_init(void)
{
  gpio_init_type gpio;

  crm_periph_clock_enable(CRM_GPIOB_PERIPH_CLOCK, TRUE);
  crm_periph_clock_enable(CRM_OTGHS_PERIPH_CLOCK, TRUE);
  crm_periph_clock_enable(CRM_OTGHSPHY_PERIPH_CLOCK, TRUE);
  crm_usb_clock_source_select(CRM_USB_CLOCK_SOURCE_HICK);
  crm_usb_phy12_clock_select(CRM_USB_PHY12_CLOCK_HEXT_DIV_1);

  gpio_default_para_init(&gpio);
  gpio.gpio_pins = USB_DM_PIN | USB_DP_PIN;
  gpio.gpio_mode = GPIO_MODE_MUX;
  gpio.gpio_pull = GPIO_PULL_NONE;
  gpio.gpio_out_type = GPIO_OUTPUT_PUSH_PULL;
  gpio.gpio_drive_strength = GPIO_DRIVE_STRENGTH_STRONGER;
  gpio_init(GPIOB, &gpio);

  usb_global_init(usb);
  usb_global_set_mode(usb, OTG_DEVICE_MODE);
  usb->gusbcfg_bit.usbtrdtim = USB_TRDTIM_8;
  usb->gccfg_bit.vbusig = TRUE;
  usb->gahbcfg_bit.dmaen = FALSE;
  usb_set_rx_fifo(usb, 128);
  usb_set_tx_fifo(usb, 0, 64);
  usb_set_tx_fifo(usb, 1, 32);
  usb_set_tx_fifo(usb, 2, 32);
  usb_set_tx_fifo(usb, 3, 32);
  OTG_DEVICE(usb)->dcfg_bit.devspd = 0;

  endpoint_info_init(&ep0_in, 0, 0x80, EPT_DIR_IN, EPT_CONTROL_TYPE, USB_EP0_MPS);
  endpoint_info_init(&ep0_out, 0, 0x00, EPT_DIR_OUT, EPT_CONTROL_TYPE, USB_EP0_MPS);
  endpoint_info_init(&keyboard_in, USB_KEYBOARD_EP, 0x81, EPT_DIR_IN, EPT_INT_TYPE, 8);
  endpoint_info_init(&vendor_in, USB_VENDOR_IN_EP, 0x82, EPT_DIR_IN, EPT_INT_TYPE, USB_VENDOR_REPORT_LEN);
  endpoint_info_init(&vendor_out, USB_VENDOR_OUT_EP, 0x03, EPT_DIR_OUT, EPT_INT_TYPE, USB_VENDOR_REPORT_LEN);
  usb_ept_open(usb, &ep0_in);
  usb_ept_open(usb, &ep0_out);
  usb_ept0_start(usb);
  control_arm_out(0);

  usb->gintmsk = USB_OTG_USBRST_INT | USB_OTG_ENUMDONE_INT | USB_OTG_RXFLVL_INT |
                 USB_OTG_IEPT_INT | USB_OTG_OEPT_INT | USB_OTG_USBSUSP_INT | USB_OTG_WKUP_INT;
  nvic_irq_enable(OTGHS_IRQn, 1, 0);
  usb_interrupt_enable(usb);
  usb_connect(usb);
}

void usb_hid_keyboard_set_keys(uint8_t key_mask)
{
  static const uint8_t usages[] = {0x07, 0x09, 0x0D, 0x0E};
  uint8_t index;
  uint8_t count = 0;

  keyboard_report[0] = 0;
  keyboard_report[1] = 0;
  for(index = 0; index < 4 && count < 6; ++index)
  {
    if((key_mask & (1u << index)) != 0) { keyboard_report[2 + count++] = usages[index]; }
  }
  while(count < 6) { keyboard_report[2 + count++] = 0; }
  if(configured != 0 && keyboard_busy == 0)
  {
    keyboard_busy = 1;
    endpoint_send(&keyboard_in, keyboard_report, sizeof(keyboard_report));
  }
}

uint8_t usb_vendor_send(const uint8_t *data, uint8_t length)
{
  if(configured == 0 || vendor_busy != 0 || data == 0 || length > USB_VENDOR_REPORT_LEN) { return 0; }
  for(uint8_t index = 0; index < length; ++index) { vendor_in_report[index] = data[index]; }
  vendor_busy = 1;
  endpoint_send(&vendor_in, vendor_in_report, length);
  return 1;
}

uint8_t usb_vendor_read(uint8_t *data, uint8_t capacity)
{
  uint8_t length = vendor_out_length;
  if(data == 0 || capacity < length) { return 0; }
  for(uint8_t index = 0; index < length; ++index) { data[index] = vendor_out_report[index]; }
  vendor_out_length = 0;
  return length;
}
