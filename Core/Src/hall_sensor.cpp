#include "hall_sensor.h"

#include "at32f402_405_adc.h"
#include "at32f402_405_crm.h"
#include "at32f402_405_dma.h"
#include "at32f402_405_gpio.h"
#include "at32f402_405_tmr.h"
#include "board_pinout.h"

namespace
{
constexpr adc_sampletime_select_type HALL_ADC_SAMPLE_TIME = ADC_SAMPLETIME_28_5;

uint32_t timer_clock_hz(void)
{
    crm_clocks_freq_type clocks;
    crm_clocks_freq_get(&clocks);

    // AT32 timers run from twice the APB clock when the APB prescaler is not 1.
    const uint32_t timer_multiplier =
        CRM->cfg_bit.apb1div == CRM_APB1_DIV_1 ? 1u : 2u;
    return clocks.apb1_freq * timer_multiplier;
}

void timer_period_calculate(uint32_t clock_hz, uint32_t *prescaler,
                            uint32_t *period)
{
    uint32_t divider = 1u;
    const uint32_t ticks_per_period = clock_hz / HALL_SENSOR_SAMPLE_RATE_HZ;

    if(ticks_per_period > 0x10000u)
    {
        divider = (ticks_per_period + 0xFFFFu) / 0x10000u;
    }

    uint32_t timer_ticks = clock_hz / (divider * HALL_SENSOR_SAMPLE_RATE_HZ);
    if(timer_ticks == 0u)
    {
        timer_ticks = 1u;
    }

    *prescaler = divider - 1u;
    *period = timer_ticks - 1u;
}
}

HallSensorManager::HallSensorManager()
    : initialized(false), frame_ready(false)
{
    for(uint32_t index = 0; index < HALL_SENSOR_COUNT; ++index)
    {
        dma_buffer[index] = 0;
        calibration[index] = {0, HALL_SENSOR_ADC_MAX_VALUE, false};
    }
}

void HallSensorManager::init(void)
{
    if(initialized)
    {
        return;
    }

    init_gpios();
    init_dma();
    init_adc();
    init_timer();
    initialized = true;
}

bool HallSensorManager::is_initialized(void) const
{
    return initialized;
}

bool HallSensorManager::is_ready(void) const
{
    if(!initialized)
    {
        return false;
    }

    if(!frame_ready && dma_flag_get(DMA1_FDT1_FLAG) == SET)
    {
        dma_flag_clear(DMA1_FDT1_FLAG);
        frame_ready = true;
    }

    return frame_ready;
}

bool HallSensorManager::snapshot(uint16_t values[HALL_SENSOR_COUNT]) const
{
    if(values == nullptr || !is_ready())
    {
        return false;
    }

    // A complete scan is four half-word DMA writes. Retry if the circular
    // buffer advances while it is being copied.
    for(uint32_t attempt = 0; attempt < 3u; ++attempt)
    {
        const uint16_t count_before = dma_data_number_get(DMA1_CHANNEL1);
        for(uint32_t index = 0; index < HALL_SENSOR_COUNT; ++index)
        {
            values[index] = dma_buffer[index];
        }
        const uint16_t count_after = dma_data_number_get(DMA1_CHANNEL1);

        if(count_before == count_after)
        {
            return true;
        }
    }

    return false;
}

uint16_t HallSensorManager::get_raw_value(uint8_t index) const
{
    if(index >= HALL_SENSOR_COUNT)
    {
        return 0;
    }
    return dma_buffer[index];
}

float HallSensorManager::get_voltage(uint8_t index) const
{
    return (static_cast<float>(get_raw_value(index)) /
            static_cast<float>(HALL_SENSOR_ADC_MAX_VALUE)) *
           HALL_SENSOR_VREF_VOLTS;
}

float HallSensorManager::get_normalized_value(uint8_t index) const
{
    if(index >= HALL_SENSOR_COUNT)
    {
        return 0.0f;
    }

    const uint16_t raw = get_raw_value(index);
    const HallSensorCalibration &entry = calibration[index];
    if(!entry.valid || entry.raw_max <= entry.raw_min)
    {
        return static_cast<float>(raw) /
               static_cast<float>(HALL_SENSOR_ADC_MAX_VALUE);
    }

    if(raw <= entry.raw_min)
    {
        return 0.0f;
    }
    if(raw >= entry.raw_max)
    {
        return 1.0f;
    }

    return static_cast<float>(raw - entry.raw_min) /
           static_cast<float>(entry.raw_max - entry.raw_min);
}

bool HallSensorManager::set_calibration(uint8_t index, uint16_t raw_min,
                                         uint16_t raw_max)
{
    if(index >= HALL_SENSOR_COUNT || raw_max <= raw_min)
    {
        return false;
    }

    calibration[index] = {raw_min, raw_max, true};
    return true;
}

void HallSensorManager::clear_calibration(uint8_t index)
{
    if(index < HALL_SENSOR_COUNT)
    {
        calibration[index] = {0, HALL_SENSOR_ADC_MAX_VALUE, false};
    }
}

bool HallSensorManager::get_calibration(
    uint8_t index, HallSensorCalibration *entry) const
{
    if(index >= HALL_SENSOR_COUNT || entry == nullptr)
    {
        return false;
    }

    *entry = calibration[index];
    return true;
}

void HallSensorManager::init_gpios(void)
{
    crm_periph_clock_enable(CRM_GPIOA_PERIPH_CLOCK, TRUE);

    gpio_init_type gpio_init_struct;
    gpio_default_para_init(&gpio_init_struct);
    gpio_init_struct.gpio_pins =
        HALL_1_PIN | HALL_2_PIN | HALL_3_PIN | HALL_4_PIN;
    gpio_init_struct.gpio_mode = GPIO_MODE_ANALOG;
    gpio_init(GPIOA, &gpio_init_struct);
}

void HallSensorManager::init_dma(void)
{
    crm_periph_clock_enable(CRM_DMA1_PERIPH_CLOCK, TRUE);

    dma_reset(DMA1_CHANNEL1);
    dma_flag_clear(DMA1_GL1_FLAG);
    dma_flexible_config(DMA1, DMA1MUX_CHANNEL1, DMAMUX_DMAREQ_ID_ADC1);
    dmamux_enable(DMA1, TRUE);

    dma_init_type dma_init_struct;
    dma_default_para_init(&dma_init_struct);
    dma_init_struct.peripheral_base_addr =
        reinterpret_cast<uint32_t>(&ADC1->odt);
    dma_init_struct.memory_base_addr =
        reinterpret_cast<uint32_t>(dma_buffer);
    dma_init_struct.direction = DMA_DIR_PERIPHERAL_TO_MEMORY;
    dma_init_struct.buffer_size = HALL_SENSOR_COUNT;
    dma_init_struct.peripheral_inc_enable = FALSE;
    dma_init_struct.memory_inc_enable = TRUE;
    dma_init_struct.peripheral_data_width =
        DMA_PERIPHERAL_DATA_WIDTH_HALFWORD;
    dma_init_struct.memory_data_width = DMA_MEMORY_DATA_WIDTH_HALFWORD;
    dma_init_struct.loop_mode_enable = TRUE;
    dma_init_struct.priority = DMA_PRIORITY_HIGH;
    dma_init(DMA1_CHANNEL1, &dma_init_struct);
    dma_channel_enable(DMA1_CHANNEL1, TRUE);
}

void HallSensorManager::init_adc(void)
{
    crm_periph_clock_enable(CRM_ADC1_PERIPH_CLOCK, TRUE);
    adc_reset(ADC1);

    // ADC_DIV_8 keeps the ADC clock within the device range at 216 MHz.
    adc_clock_div_set(ADC_DIV_8);

    adc_base_config_type adc_base_config_struct;
    adc_base_default_para_init(&adc_base_config_struct);
    adc_base_config_struct.sequence_mode = TRUE;
    // One timer trigger must start exactly one four-channel scan.
    adc_base_config_struct.repeat_mode = FALSE;
    adc_base_config_struct.data_align = ADC_RIGHT_ALIGNMENT;
    adc_base_config_struct.ordinary_channel_length = HALL_SENSOR_COUNT;
    adc_base_config(ADC1, &adc_base_config_struct);

    adc_ordinary_channel_set(ADC1, HALL_1_ADC_CHANNEL, 1,
                             HALL_ADC_SAMPLE_TIME);
    adc_ordinary_channel_set(ADC1, HALL_2_ADC_CHANNEL, 2,
                             HALL_ADC_SAMPLE_TIME);
    adc_ordinary_channel_set(ADC1, HALL_3_ADC_CHANNEL, 3,
                             HALL_ADC_SAMPLE_TIME);
    adc_ordinary_channel_set(ADC1, HALL_4_ADC_CHANNEL, 4,
                             HALL_ADC_SAMPLE_TIME);

    // Enable the trigger only after TMR2 has been configured.  tmr_base_init
    // generates a software update event while loading the timer registers.
    adc_ordinary_conversion_trigger_set(
        ADC1, ADC12_ORDINARY_TRIG_TMR2TRGOUT, FALSE);
    adc_dma_mode_enable(ADC1, TRUE);

    adc_calibration_init(ADC1);
    while(adc_calibration_init_status_get(ADC1) == SET)
    {
    }
    adc_calibration_start(ADC1);
    while(adc_calibration_status_get(ADC1) == SET)
    {
    }

    // DMA is enabled before the ADC, so no conversion result can be lost.
    adc_enable(ADC1, TRUE);
}

void HallSensorManager::init_timer(void)
{
    crm_periph_clock_enable(CRM_TMR2_PERIPH_CLOCK, TRUE);
    tmr_reset(TMR2);
    tmr_internal_clock_set(TMR2);
    tmr_cnt_dir_set(TMR2, TMR_COUNT_UP);
    tmr_clock_source_div_set(TMR2, TMR_CLOCK_DIV1);
    tmr_primary_mode_select(TMR2, TMR_PRIMARY_SEL_OVERFLOW);

    uint32_t prescaler;
    uint32_t period;
    timer_period_calculate(timer_clock_hz(), &prescaler, &period);
    tmr_base_init(TMR2, period, prescaler);
    tmr_counter_value_set(TMR2, 0);

    // Starting the timer is the final step: its overflow is the ADC trigger.
    adc_ordinary_conversion_trigger_set(
        ADC1, ADC12_ORDINARY_TRIG_TMR2TRGOUT, TRUE);
    tmr_counter_enable(TMR2, TRUE);
}
