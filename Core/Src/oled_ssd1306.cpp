#include "oled_ssd1306.h"

#include "at32f402_405_crm.h"
#include "at32f402_405_gpio.h"
#include "at32f402_405_i2c.h"
#include "board_pinout.h"

namespace
{
constexpr uint32_t I2C_TIMEOUT_LOOPS = 100000u;
constexpr uint32_t I2C_SPEED_HZ = 100000u;
constexpr uint8_t OLED_CONTROL_COMMAND = 0x00u;
constexpr uint8_t OLED_CONTROL_DATA = 0x40u;

const uint8_t FONT_DIGITS[10][5] = {
    {0x3E, 0x51, 0x49, 0x45, 0x3E}, {0x00, 0x42, 0x7F, 0x40, 0x00},
    {0x42, 0x61, 0x51, 0x49, 0x46}, {0x21, 0x41, 0x45, 0x4B, 0x31},
    {0x18, 0x14, 0x12, 0x7F, 0x10}, {0x27, 0x45, 0x45, 0x45, 0x39},
    {0x3C, 0x4A, 0x49, 0x49, 0x30}, {0x01, 0x71, 0x09, 0x05, 0x03},
    {0x36, 0x49, 0x49, 0x49, 0x36}, {0x06, 0x49, 0x49, 0x29, 0x1E}};

const uint8_t FONT_UPPER[26][5] = {
    {0x7E, 0x11, 0x11, 0x11, 0x7E}, {0x7F, 0x49, 0x49, 0x49, 0x36},
    {0x3E, 0x41, 0x41, 0x41, 0x22}, {0x7F, 0x41, 0x41, 0x22, 0x1C},
    {0x7F, 0x49, 0x49, 0x49, 0x41}, {0x7F, 0x09, 0x09, 0x09, 0x01},
    {0x3E, 0x41, 0x49, 0x49, 0x7A}, {0x7F, 0x08, 0x08, 0x08, 0x7F},
    {0x00, 0x41, 0x7F, 0x41, 0x00}, {0x20, 0x40, 0x41, 0x3F, 0x01},
    {0x7F, 0x08, 0x14, 0x22, 0x41}, {0x7F, 0x40, 0x40, 0x40, 0x40},
    {0x7F, 0x02, 0x0C, 0x02, 0x7F}, {0x7F, 0x04, 0x08, 0x10, 0x7F},
    {0x3E, 0x41, 0x41, 0x41, 0x3E}, {0x7F, 0x09, 0x09, 0x09, 0x06},
    {0x3E, 0x41, 0x51, 0x21, 0x5E}, {0x7F, 0x09, 0x19, 0x29, 0x46},
    {0x46, 0x49, 0x49, 0x49, 0x31}, {0x01, 0x01, 0x7F, 0x01, 0x01},
    {0x3F, 0x40, 0x40, 0x40, 0x3F}, {0x1F, 0x20, 0x40, 0x20, 0x1F},
    {0x7F, 0x20, 0x18, 0x20, 0x7F}, {0x63, 0x14, 0x08, 0x14, 0x63},
    {0x07, 0x08, 0x70, 0x08, 0x07}, {0x61, 0x51, 0x49, 0x45, 0x43}};

const uint8_t *glyph_for(char character)
{
    if(character >= 'a' && character <= 'z')
    {
        character = static_cast<char>(character - ('a' - 'A'));
    }

    if(character >= 'A' && character <= 'Z')
    {
        return FONT_UPPER[static_cast<uint8_t>(character - 'A')];
    }
    if(character >= '0' && character <= '9')
    {
        return FONT_DIGITS[static_cast<uint8_t>(character - '0')];
    }

    static const uint8_t space[5] = {0, 0, 0, 0, 0};
    static const uint8_t punctuation[5] = {0x00, 0x00, 0x5F, 0x00, 0x00};
    static const uint8_t question[5] = {0x02, 0x01, 0x51, 0x09, 0x06};
    static const uint8_t dash[5] = {0x08, 0x08, 0x08, 0x08, 0x08};
    static const uint8_t colon[5] = {0x00, 0x36, 0x36, 0x00, 0x00};
    static const uint8_t dot[5] = {0x00, 0x60, 0x60, 0x00, 0x00};
    static const uint8_t slash[5] = {0x20, 0x10, 0x08, 0x04, 0x02};
    static const uint8_t plus[5] = {0x08, 0x08, 0x3E, 0x08, 0x08};
    static const uint8_t equals[5] = {0x14, 0x14, 0x14, 0x14, 0x14};
    static const uint8_t open_paren[5] = {0x00, 0x1C, 0x22, 0x41, 0x00};
    static const uint8_t close_paren[5] = {0x00, 0x41, 0x22, 0x1C, 0x00};

    switch(character)
    {
    case ' ':
        return space;
    case '!':
        return punctuation;
    case '?':
        return question;
    case '-':
    case '_':
        return dash;
    case ':':
        return colon;
    case '.':
        return dot;
    case '/':
        return slash;
    case '+':
        return plus;
    case '=':
        return equals;
    case '(':
        return open_paren;
    case ')':
        return close_paren;
    default:
        return question;
    }
}

uint32_t i2c_timing_100khz(void)
{
    crm_clocks_freq_type clocks;
    crm_clocks_freq_get(&clocks);

    uint32_t divider = 0u;
    uint32_t phase_ticks = clocks.apb1_freq / (I2C_SPEED_HZ * 2u);
    while(phase_ticks > 256u && divider < 15u)
    {
        ++divider;
        phase_ticks = clocks.apb1_freq /
                      (I2C_SPEED_HZ * 2u * (divider + 1u));
    }

    if(phase_ticks == 0u)
    {
        phase_ticks = 1u;
    }
    if(phase_ticks > 256u)
    {
        phase_ticks = 256u;
    }
    --phase_ticks;

    return phase_ticks | (phase_ticks << 8u) | (2u << 16u) |
           (2u << 20u) | (divider << 24u) | (divider << 28u);
}

bool i2c_error(void)
{
    return i2c_flag_get(OLED_I2C, I2C_ACKFAIL_FLAG) == SET ||
           i2c_flag_get(OLED_I2C, I2C_BUSERR_FLAG) == SET ||
           i2c_flag_get(OLED_I2C, I2C_ARLOST_FLAG) == SET ||
           i2c_flag_get(OLED_I2C, I2C_TMOUT_FLAG) == SET;
}

void clear_i2c_errors(void)
{
    i2c_flag_clear(OLED_I2C, I2C_ACKFAIL_FLAG | I2C_BUSERR_FLAG |
                              I2C_ARLOST_FLAG | I2C_TMOUT_FLAG |
                              I2C_STOPF_FLAG);
}

bool wait_for_i2c_flag(uint32_t flag)
{
    for(uint32_t timeout = 0u; timeout < I2C_TIMEOUT_LOOPS; ++timeout)
    {
        if(i2c_error())
        {
            return false;
        }
        if(i2c_flag_get(OLED_I2C, flag) == SET)
        {
            return true;
        }
    }
    return false;
}
}

OLEDSSD1306 g_oled;

OLEDSSD1306::OLEDSSD1306()
    : framebuffer{}, initialized(false), dirty(false)
{
}

bool OLEDSSD1306::init(void)
{
    if(initialized)
    {
        return true;
    }

    crm_periph_clock_enable(CRM_GPIOB_PERIPH_CLOCK, TRUE);
    gpio_pin_mux_config(OLED_I2C_PORT, GPIO_PINS_SOURCE6, GPIO_MUX_4);
    gpio_pin_mux_config(OLED_I2C_PORT, GPIO_PINS_SOURCE7, GPIO_MUX_4);

    gpio_init_type gpio_init_struct;
    gpio_default_para_init(&gpio_init_struct);
    gpio_init_struct.gpio_pins = OLED_SCL_PIN | OLED_SDA_PIN;
    gpio_init_struct.gpio_mode = GPIO_MODE_MUX;
    gpio_init_struct.gpio_out_type = GPIO_OUTPUT_OPEN_DRAIN;
    gpio_init_struct.gpio_pull = GPIO_PULL_UP;
    gpio_init_struct.gpio_drive_strength = GPIO_DRIVE_STRENGTH_STRONGER;
    gpio_init(OLED_I2C_PORT, &gpio_init_struct);

    crm_periph_clock_enable(CRM_I2C1_PERIPH_CLOCK, TRUE);
    i2c_reset(OLED_I2C);
    i2c_init(OLED_I2C, 0u, i2c_timing_100khz());
    i2c_clock_stretch_enable(OLED_I2C, TRUE);
    i2c_ack_enable(OLED_I2C, TRUE);
    i2c_timeout_set(OLED_I2C, 0x0FFFu);
    i2c_timeout_detcet_set(OLED_I2C, I2C_TIMEOUT_DETCET_LOW);
    i2c_timeout_enable(OLED_I2C, TRUE);
    i2c_enable(OLED_I2C, TRUE);
    clear_i2c_errors();

    static const uint8_t commands[] = {
        0xAE,             // display off
        0xD5, 0x80,       // clock divide
        0xA8, 0x1F,       // 128x32 multiplex
        0xD3, 0x00,       // no display offset
        0x40,             // display start line
        0x8D, 0x14,       // internal charge pump
        0x20, 0x00,       // horizontal addressing
        0xA1,             // segment remap
        0xC8,             // COM scan direction
        0xDA, 0x02,       // 128x32 COM pins
        0x81, 0x8F,       // contrast
        0xD9, 0xF1,       // pre-charge
        0xDB, 0x40,       // VCOMH
        0xA4,             // resume RAM display
        0xA6,             // normal display
        0xAF              // display on
    };

    initialized = write_commands(commands, sizeof(commands));
    dirty = initialized;
    return initialized;
}

bool OLEDSSD1306::update(void)
{
    if(!initialized || !dirty)
    {
        return initialized;
    }

    static const uint8_t address_commands[] =
        {0x21, 0x00, OLED_WIDTH - 1u, 0x22, 0x00, (OLED_HEIGHT / 8u) - 1u};
    if(!write_commands(address_commands, sizeof(address_commands)) ||
       !write_data(framebuffer, OLED_FRAMEBUFFER_SIZE))
    {
        return false;
    }

    dirty = false;
    return true;
}

void OLEDSSD1306::clear(void)
{
    for(uint16_t index = 0u; index < OLED_FRAMEBUFFER_SIZE; ++index)
    {
        framebuffer[index] = 0u;
    }
    dirty = true;
}

void OLEDSSD1306::pixel(uint8_t x, uint8_t y, bool on)
{
    if(x >= OLED_WIDTH || y >= OLED_HEIGHT)
    {
        return;
    }

    const uint16_t index =
        static_cast<uint16_t>(x) +
        (static_cast<uint16_t>(y) / 8u) * OLED_WIDTH;
    const uint8_t mask = static_cast<uint8_t>(1u << (y & 7u));
    const uint8_t old_value = framebuffer[index];
    framebuffer[index] = on ? static_cast<uint8_t>(old_value | mask)
                            : static_cast<uint8_t>(old_value & ~mask);
    dirty = dirty || old_value != framebuffer[index];
}

void OLEDSSD1306::draw_char(uint8_t x, uint8_t y, char character)
{
    const uint8_t *glyph = glyph_for(character);
    for(uint8_t column = 0u; column < 5u; ++column)
    {
        if(static_cast<uint16_t>(x) + column >= OLED_WIDTH)
        {
            continue;
        }
        const uint8_t bits = glyph[column];
        for(uint8_t row = 0u; row < 7u; ++row)
        {
            if(static_cast<uint16_t>(y) + row < OLED_HEIGHT)
            {
                pixel(static_cast<uint8_t>(x + column),
                      static_cast<uint8_t>(y + row),
                      (bits & (1u << row)) != 0u);
            }
        }
    }
    if(static_cast<uint16_t>(x) + 5u < OLED_WIDTH)
    {
        for(uint8_t row = 0u; row < 7u; ++row)
        {
            if(static_cast<uint16_t>(y) + row < OLED_HEIGHT)
            {
                pixel(static_cast<uint8_t>(x + 5u),
                      static_cast<uint8_t>(y + row), false);
            }
        }
    }
}

void OLEDSSD1306::draw_text(uint8_t x, uint8_t y, const char *text)
{
    if(text == nullptr)
    {
        return;
    }

    uint16_t cursor = x;
    while(*text != '\0' && cursor < OLED_WIDTH)
    {
        draw_char(static_cast<uint8_t>(cursor), y, *text++);
        cursor += 6u;
    }
}

bool OLEDSSD1306::is_initialized(void) const
{
    return initialized;
}

bool OLEDSSD1306::write_commands(const uint8_t *commands, uint8_t count)
{
    if(commands == nullptr || count > 254u)
    {
        return false;
    }

    uint8_t transfer[255];
    transfer[0] = OLED_CONTROL_COMMAND;
    for(uint8_t index = 0u; index < count; ++index)
    {
        transfer[index + 1u] = commands[index];
    }
    return write_transfer(transfer, static_cast<uint8_t>(count + 1u));
}

bool OLEDSSD1306::write_data(const uint8_t *data, uint16_t count)
{
    if(data == nullptr)
    {
        return false;
    }

    while(count != 0u)
    {
        const uint8_t payload =
            count > 254u ? 254u : static_cast<uint8_t>(count);
        uint8_t transfer[255];
        transfer[0] = OLED_CONTROL_DATA;
        for(uint8_t index = 0u; index < payload; ++index)
        {
            transfer[index + 1u] = data[index];
        }
        if(!write_transfer(transfer, static_cast<uint8_t>(payload + 1u)))
        {
            return false;
        }
        data += payload;
        count -= payload;
    }
    return true;
}

bool OLEDSSD1306::write_transfer(const uint8_t *data, uint8_t count)
{
    if(data == nullptr || count == 0u)
    {
        return false;
    }

    clear_i2c_errors();
    if(i2c_flag_get(OLED_I2C, I2C_BUSYF_FLAG) == SET)
    {
        return false;
    }

    i2c_transmit_set(OLED_I2C, static_cast<uint16_t>(OLED_I2C_ADDRESS << 1u),
                     count, I2C_AUTO_STOP_MODE, I2C_GEN_START_WRITE);
    for(uint8_t index = 0u; index < count; ++index)
    {
        if(!wait_for_i2c_flag(I2C_TDBE_FLAG))
        {
            i2c_stop_generate(OLED_I2C);
            clear_i2c_errors();
            return false;
        }
        i2c_data_send(OLED_I2C, data[index]);
    }

    if(!wait_for_i2c_flag(I2C_TDC_FLAG) ||
       !wait_for_i2c_flag(I2C_STOPF_FLAG))
    {
        i2c_stop_generate(OLED_I2C);
        clear_i2c_errors();
        return false;
    }

    clear_i2c_errors();
    return true;
}
