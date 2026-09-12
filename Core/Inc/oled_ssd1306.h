#ifndef OLED_SSD1306_H
#define OLED_SSD1306_H

#include <cstdint>

constexpr uint8_t OLED_WIDTH = 128;
constexpr uint8_t OLED_HEIGHT = 32;
constexpr uint16_t OLED_FRAMEBUFFER_SIZE =
    (static_cast<uint16_t>(OLED_WIDTH) * OLED_HEIGHT) / 8u;

class OLEDSSD1306
{
public:
    OLEDSSD1306();

    bool init(void);
    bool update(void);
    void clear(void);
    void pixel(uint8_t x, uint8_t y, bool on = true);
    void draw_char(uint8_t x, uint8_t y, char character);
    void draw_text(uint8_t x, uint8_t y, const char *text);

    bool is_initialized(void) const;

private:
    bool write_commands(const uint8_t *commands, uint8_t count);
    bool write_data(const uint8_t *data, uint16_t count);
    bool write_transfer(const uint8_t *data, uint8_t count);

    uint8_t framebuffer[OLED_FRAMEBUFFER_SIZE];
    bool initialized;
    bool dirty;
};

extern OLEDSSD1306 g_oled;

#endif
