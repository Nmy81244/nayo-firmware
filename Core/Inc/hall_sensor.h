#ifndef HALL_SENSOR_H
#define HALL_SENSOR_H

#include <cstdint>

#define HALL_SENSOR_COUNT             4u
#define HALL_SENSOR_SAMPLE_RATE_HZ   8000u
#define HALL_SENSOR_ADC_MAX_VALUE    4095u
#define HALL_SENSOR_VREF_VOLTS       3.3f

struct HallSensorCalibration
{
    uint16_t raw_min;
    uint16_t raw_max;
    bool valid;
};

class HallSensorManager
{
public:
    HallSensorManager();

    void init(void);

    bool is_initialized(void) const;
    bool is_ready(void) const;
    bool snapshot(uint16_t values[HALL_SENSOR_COUNT]) const;

    uint16_t get_raw_value(uint8_t index) const;
    float get_voltage(uint8_t index) const;
    float get_normalized_value(uint8_t index) const;

    bool set_calibration(uint8_t index, uint16_t raw_min, uint16_t raw_max);
    void clear_calibration(uint8_t index);
    bool get_calibration(uint8_t index, HallSensorCalibration *calibration) const;

private:
    alignas(4) volatile uint16_t dma_buffer[HALL_SENSOR_COUNT];
    HallSensorCalibration calibration[HALL_SENSOR_COUNT];
    bool initialized;
    mutable bool frame_ready;

    void init_gpios(void);
    void init_dma(void);
    void init_adc(void);
    void init_timer(void);
};

extern HallSensorManager g_hall_sensors;

#endif
