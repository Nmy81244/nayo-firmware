#include "ws2812.h"

#include "at32f402_405_crm.h"
#include "at32f402_405_dma.h"
#include "at32f402_405_gpio.h"
#include "at32f402_405_tmr.h"
#include "board_pinout.h"

namespace
{
constexpr uint32_t WS2812_TIMER_HZ = 800000u;
constexpr uint32_t WS2812_PERIOD_TICKS = 270u;
constexpr uint32_t WS2812_ZERO_HIGH_TICKS = 86u;
constexpr uint32_t WS2812_ONE_HIGH_TICKS = 162u;
constexpr uint32_t GPIO_SET_MASK = static_cast<uint32_t>(RGB_LED_PIN);
constexpr uint32_t GPIO_RESET_MASK =
    static_cast<uint32_t>(RGB_LED_PIN) << 16u;

uint32_t timer_clock_hz(void)
{
    crm_clocks_freq_type clocks;
    crm_clocks_freq_get(&clocks);
    const uint32_t timer_multiplier =
        CRM->cfg_bit.apb1div == CRM_APB1_DIV_1 ? 1u : 2u;
    return clocks.apb1_freq * timer_multiplier;
}

uint32_t timer_period_ticks(void)
{
    const uint32_t clock_hz = timer_clock_hz();
    const uint32_t ticks =
        (clock_hz + (WS2812_TIMER_HZ / 2u)) / WS2812_TIMER_HZ;
    return ticks == 0u ? 1u : ticks;
}

void dma_gpio_init(dma_channel_type *channel, dmamux_channel_type *mux,
                   dmamux_requst_id_sel_type request, uint32_t *waveform,
                   uint16_t count)
{
    dma_reset(channel);

    dma_init_type dma_init_struct;
    dma_default_para_init(&dma_init_struct);
    dma_init_struct.peripheral_base_addr =
        reinterpret_cast<uint32_t>(&GPIOA->scr);
    dma_init_struct.memory_base_addr =
        reinterpret_cast<uint32_t>(waveform);
    dma_init_struct.direction = DMA_DIR_MEMORY_TO_PERIPHERAL;
    dma_init_struct.buffer_size = count;
    dma_init_struct.peripheral_inc_enable = FALSE;
    dma_init_struct.memory_inc_enable = TRUE;
    dma_init_struct.peripheral_data_width = DMA_PERIPHERAL_DATA_WIDTH_WORD;
    dma_init_struct.memory_data_width = DMA_MEMORY_DATA_WIDTH_WORD;
    dma_init_struct.loop_mode_enable = FALSE;
    dma_init_struct.priority = DMA_PRIORITY_VERY_HIGH;
    dma_init(channel, &dma_init_struct);
    dma_flexible_config(DMA1, mux, request);
}
}

WS2812 g_ws2812;

WS2812::WS2812()
    : colors{}, set_waveform{}, zero_waveform{}, one_waveform{},
      initialized(false), busy(false)
{
}

void WS2812::init(void)
{
    if(initialized)
    {
        return;
    }

    init_gpio();
    init_timer();
    init_dma();
    initialized = true;
}

bool WS2812::is_initialized(void) const
{
    return initialized;
}

bool WS2812::is_busy(void)
{
    update();
    return busy;
}

void WS2812::set_pixel(uint8_t index, uint8_t red, uint8_t green,
                       uint8_t blue)
{
    if(index < WS2812_LED_COUNT)
    {
        colors[index] = {red, green, blue};
    }
}

void WS2812::set_all(uint8_t red, uint8_t green, uint8_t blue)
{
    for(uint32_t index = 0; index < WS2812_LED_COUNT; ++index)
    {
        colors[index] = {red, green, blue};
    }
}

void WS2812::clear(void)
{
    set_all(0, 0, 0);
}

bool WS2812::show(void)
{
    if(!initialized || busy)
    {
        return false;
    }

    encode_waveform();
    dma_data_number_set(DMA1_CHANNEL2, DMA_SLOTS);
    dma_data_number_set(DMA1_CHANNEL3, DMA_SLOTS);
    dma_data_number_set(DMA1_CHANNEL4, DMA_SLOTS);
    dma_flag_clear(DMA1_GL2_FLAG | DMA1_GL3_FLAG | DMA1_GL4_FLAG);

    GPIOA->scr = GPIO_RESET_MASK;
    tmr_counter_value_set(TMR3, 0);
    tmr_flag_clear(TMR3, TMR_OVF_FLAG | TMR_C1_FLAG | TMR_C2_FLAG);

    busy = true;
    tmr_dma_request_enable(TMR3, TMR_OVERFLOW_DMA_REQUEST, TRUE);
    tmr_dma_request_enable(TMR3, TMR_C1_DMA_REQUEST, TRUE);
    tmr_dma_request_enable(TMR3, TMR_C2_DMA_REQUEST, TRUE);
    dma_channel_enable(DMA1_CHANNEL2, TRUE);
    dma_channel_enable(DMA1_CHANNEL3, TRUE);
    dma_channel_enable(DMA1_CHANNEL4, TRUE);
    tmr_counter_enable(TMR3, TRUE);
    return true;
}

void WS2812::update(void)
{
    if(!busy)
    {
        return;
    }

    if(dma_flag_get(DMA1_FDT2_FLAG) == SET &&
       dma_flag_get(DMA1_FDT3_FLAG) == SET &&
       dma_flag_get(DMA1_FDT4_FLAG) == SET)
    {
        finish_transfer();
    }
}

void WS2812::init_gpio(void)
{
    crm_periph_clock_enable(CRM_GPIOA_PERIPH_CLOCK, TRUE);

    // PA15 is deliberately used as a normal GPIO.  The timer only supplies
    // DMA request timing, avoiding an unverified PA15 timer alternate-function
    // mapping in this BSP.
    gpio_pin_mux_config(GPIOA, GPIO_PINS_SOURCE15, GPIO_MUX_0);

    gpio_init_type gpio_init_struct;
    gpio_default_para_init(&gpio_init_struct);
    gpio_init_struct.gpio_pins = RGB_LED_PIN;
    gpio_init_struct.gpio_mode = GPIO_MODE_OUTPUT;
    gpio_init_struct.gpio_out_type = GPIO_OUTPUT_PUSH_PULL;
    gpio_init_struct.gpio_pull = GPIO_PULL_NONE;
    gpio_init_struct.gpio_drive_strength = GPIO_DRIVE_STRENGTH_STRONGER;
    gpio_init(GPIOA, &gpio_init_struct);
    GPIOA->scr = GPIO_RESET_MASK;
}

void WS2812::init_timer(void)
{
    crm_periph_clock_enable(CRM_TMR3_PERIPH_CLOCK, TRUE);
    tmr_reset(TMR3);
    tmr_internal_clock_set(TMR3);
    tmr_cnt_dir_set(TMR3, TMR_COUNT_UP);
    tmr_clock_source_div_set(TMR3, TMR_CLOCK_DIV1);
    tmr_primary_mode_select(TMR3, TMR_PRIMARY_SEL_OVERFLOW);

    const uint32_t period_ticks = timer_period_ticks();
    tmr_base_init(TMR3, period_ticks - 1u, 0);
    tmr_counter_value_set(TMR3, 0);
    tmr_channel_value_set(TMR3, TMR_SELECT_CHANNEL_1,
                          (period_ticks * WS2812_ZERO_HIGH_TICKS) /
                              WS2812_PERIOD_TICKS);
    tmr_channel_value_set(TMR3, TMR_SELECT_CHANNEL_2,
                          (period_ticks * WS2812_ONE_HIGH_TICKS) /
                              WS2812_PERIOD_TICKS);

    tmr_output_config_type output;
    tmr_output_default_para_init(&output);
    output.oc_mode = TMR_OUTPUT_CONTROL_PWM_MODE_A;
    output.oc_output_state = TRUE;
    tmr_output_channel_config(TMR3, TMR_SELECT_CHANNEL_1, &output);
    tmr_output_channel_config(TMR3, TMR_SELECT_CHANNEL_2, &output);
    tmr_channel_dma_select(TMR3, TMR_DMA_REQUEST_BY_CHANNEL);
    tmr_counter_enable(TMR3, FALSE);
}

void WS2812::init_dma(void)
{
    crm_periph_clock_enable(CRM_DMA1_PERIPH_CLOCK, TRUE);
    dmamux_enable(DMA1, TRUE);

    dma_gpio_init(DMA1_CHANNEL2, DMA1MUX_CHANNEL2,
                  DMAMUX_DMAREQ_ID_TMR3_OVERFLOW, set_waveform,
                  static_cast<uint16_t>(DMA_SLOTS));
    dma_gpio_init(DMA1_CHANNEL3, DMA1MUX_CHANNEL3,
                  DMAMUX_DMAREQ_ID_TMR3_CH1, zero_waveform,
                  static_cast<uint16_t>(DMA_SLOTS));
    dma_gpio_init(DMA1_CHANNEL4, DMA1MUX_CHANNEL4,
                  DMAMUX_DMAREQ_ID_TMR3_CH2, one_waveform,
                  static_cast<uint16_t>(DMA_SLOTS));

    // DMA1 channel 1 is reserved by Hall ADC DMA.  Channels 2-4 and TMR3
    // are unused by the rest of this firmware.
    tmr_dma_request_enable(TMR3, TMR_OVERFLOW_DMA_REQUEST, FALSE);
    tmr_dma_request_enable(TMR3, TMR_C1_DMA_REQUEST, FALSE);
    tmr_dma_request_enable(TMR3, TMR_C2_DMA_REQUEST, FALSE);
}

void WS2812::finish_transfer(void)
{
    tmr_counter_enable(TMR3, FALSE);
    tmr_dma_request_enable(TMR3, TMR_OVERFLOW_DMA_REQUEST, FALSE);
    tmr_dma_request_enable(TMR3, TMR_C1_DMA_REQUEST, FALSE);
    tmr_dma_request_enable(TMR3, TMR_C2_DMA_REQUEST, FALSE);
    dma_channel_enable(DMA1_CHANNEL2, FALSE);
    dma_channel_enable(DMA1_CHANNEL3, FALSE);
    dma_channel_enable(DMA1_CHANNEL4, FALSE);
    dma_flag_clear(DMA1_GL2_FLAG | DMA1_GL3_FLAG | DMA1_GL4_FLAG);
    GPIOA->scr = GPIO_RESET_MASK;
    busy = false;
}

void WS2812::encode_waveform(void)
{
    for(uint32_t slot = 0; slot < DMA_SLOTS; ++slot)
    {
        set_waveform[slot] = 0;
        zero_waveform[slot] = 0;
        one_waveform[slot] = 0;
    }

    uint32_t slot = 0;
    for(uint32_t led = 0; led < WS2812_LED_COUNT; ++led)
    {
        // WS2812B protocol order is GRB, MSB first.  The public API remains
        // RGB so callers do not need to know the wire order.
        const uint8_t wire_order[3] = {
            colors[led].g, colors[led].r, colors[led].b};
        for(uint32_t component = 0; component < 3u; ++component)
        {
            for(uint32_t bit = 0; bit < 8u; ++bit, ++slot)
            {
                const bool one =
                    (wire_order[component] & (0x80u >> bit)) != 0u;
                set_waveform[slot] = GPIO_SET_MASK;
                zero_waveform[slot] = one ? 0u : GPIO_RESET_MASK;
                one_waveform[slot] = one ? GPIO_RESET_MASK : 0u;
            }
        }
    }
}
