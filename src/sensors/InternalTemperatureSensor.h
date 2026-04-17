#pragma once

#include <cstdint>
#include <sensors/TemperatureSensor.h>

/**
 * A class to read the temperature sensor built into STM32 microcontrollers.
 * Requires Adc::TemperatureSensorCalibration to be available (from system-stm32).
 */
class InternalTemperatureSensor : public TemperatureSensor {

   volatile uint16_t &adcValue;
   volatile uint16_t &vrefintAdcValue;
   const uint32_t adcBits;
   uint32_t (*const getReferenceVoltageInmV)(uint32_t vrefintReg);

   public:
   InternalTemperatureSensor(volatile uint16_t &adcValue, volatile uint16_t &vrefintAdcValue, uint32_t adcBits, uint32_t (*getReferenceVoltageInmV)(uint32_t));

   Temperature_t getTemperature() override;

   Temperature_t getMaxTemperature() override;
};
