#ifndef WS2812_H
#define WS2812_H

#include <cstdint>

#define WS2812_LED_COUNT 10u

struct Ws2812Color
{
    uint8_t r;
    uint8_t g;
    uint8_t b;
};

class WS2812
{
public:
    WS2812();

    void init(void);
    bool is_initialized(void) const;
    bool is_busy(void);

    void set_pixel(uint8_t index, uint8_t red, uint8_t green, uint8_t blue);
    void set_all(uint8_t red, uint8_t green, uint8_t blue);
    void clear(void);

    // Starts a non-blocking transfer.  Call update() from the main loop.
    bool show(void);
    void update(void);

private:
    static constexpr uint32_t DATA_BITS = WS2812_LED_COUNT * 24u;
    static constexpr uint32_t RESET_SLOTS = 64u;
    static constexpr uint32_t DMA_SLOTS = DATA_BITS + RESET_SLOTS;

    Ws2812Color colors[WS2812_LED_COUNT];
    alignas(4) uint32_t set_waveform[DMA_SLOTS];
    alignas(4) uint32_t zero_waveform[DMA_SLOTS];
    alignas(4) uint32_t one_waveform[DMA_SLOTS];
    bool initialized;
    volatile bool busy;

    void init_gpio(void);
    void init_timer(void);
    void init_dma(void);
    void finish_transfer(void);
    void encode_waveform(void);
};

extern WS2812 g_ws2812;

#endif
