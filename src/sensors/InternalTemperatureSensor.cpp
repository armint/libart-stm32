#include "InternalTemperatureSensor.h"
#include "Adc.h"

InternalTemperatureSensor::InternalTemperatureSensor(volatile uint16_t &adcValue,
                                                     volatile uint16_t &vrefintAdcValue,
                                                     uint32_t adcBits,
                                                     uint32_t (*getReferenceVoltageInmV)(uint32_t))
   : adcValue(adcValue)
   , vrefintAdcValue(vrefintAdcValue)
   , adcBits(adcBits)
   , getReferenceVoltageInmV(getReferenceVoltageInmV) {
}

Temperature_t InternalTemperatureSensor::getTemperature() {
   using Cal = Adc::TemperatureSensorCalibration;
   Temperature_t temperature;
   const Temperature_t temp1Cal = static_cast<Temperature_t>(static_cast<uint32_t>(Cal::cal1()) << (adcBits - 12));
   const Temperature_t temp2Cal = static_cast<Temperature_t>(static_cast<uint32_t>(Cal::cal2()) << (adcBits - 12));

   temperature = (static_cast<Temperature_t>(adcValue * getReferenceVoltageInmV(vrefintAdcValue)) / Cal::vrefAnalog) - temp1Cal;
   temperature = temperature * static_cast<Temperature_t>(Cal::temp2 - Cal::temp1);
   temperature = temperature / (temp2Cal - temp1Cal);
   temperature = temperature + Cal::temp1;
   return temperature;
}

Temperature_t InternalTemperatureSensor::getMaxTemperature() {
   return 125;
}
