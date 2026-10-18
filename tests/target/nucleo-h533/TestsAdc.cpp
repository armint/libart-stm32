#include "Board.h"
#include "Report.h"
#include "Tests.h"

#include <Adc.h>
#include <delaybusywait.h>
#include <peripherals/Dma.h>
#include <peripherals/Rcc.h>
#include <sensors/InternalTemperatureSensor.h>

namespace hwtest {
namespace {

const Adc adc(ADC1);

uint32_t vddaFromVrefint(uint32_t raw) {
   return Adc::supplyFromVrefint(static_cast<uint16_t>(raw), Adc::Calibration::vrefint());
}

bool near(uint16_t value, uint16_t reference, uint16_t tolerance) {
   return value + tolerance >= reference && value <= reference + tolerance;
}

alignas(4) uint16_t sequence[2];

// Vrefint and Vsense as a regular sequence of two ranks, read by GPDMA.
bool convertSequence() {
   Dma::Config config;
   config.direction = Dma::Direction::PeripheralToMemory;
   config.request = Dma::Request::adc1;
   config.sourceSize = Dma::DataSize::Size16Bits;
   config.destinationSize = Dma::DataSize::Size16Bits;
   config.destinationIncrement = true;
   const Dma::Channel channel(GPDMA1, 5);
   Rcc::enableGPDMA1Clock();

   adc.setRegularRank(1, Adc::Vrefint);
   adc.setRegularRank(2, Adc::Vsense);
   adc.setRegularLength(2);
   adc.setConversionMode(Adc::ConversionMode::Single);
   adc.setDmaMode(Adc::DmaMode::OneShot);
   bool ok = channel.start(config, address(ADC1->DR), sequence, sizeof(sequence));
   adc.start();
   ok = ok && waitChannel(channel);
   adc.setDmaMode(Adc::DmaMode::Off);
   adc.setRegularLength(1);
   return ok;
}

} // namespace

void testAdc() {
   Adc::enableClock();
   const uint32_t clockHz = Adc::configureClock();
   (void)vcp.printf("  adc clock %lu Hz\r\n", static_cast<unsigned long>(clockHz));
   if(!check("adc1-power-up-calibrate-enable", clockHz != 0 && adc.powerUp() && adc.calibrate() && adc.enable())) {
      return;
   }

   uint32_t vddaMv = 0;
   const bool vddaOk = adc.readSupplyMillivolts(vddaMv);
   (void)vcp.printf("  VDDA %lu mV\r\n", static_cast<unsigned long>(vddaMv));
   // The NUCLEO-H533RE supplies VDDA with 3.3 V.
   check("adc1-vdda-from-vrefint", vddaOk && vddaMv >= 3000 && vddaMv <= 3600);

   // Vsense needs at least 9 us of sampling: 640.5 cycles at up to 62.5 MHz.
   Adc::enableTemperatureSensor();
   adc.setSamplingTime(Adc::Vsense, Adc::SamplingCycles::Cycles640_5);
   delayuS(30);
   uint16_t vrefintRaw = 0;
   uint16_t vsenseRaw = 0;
   const bool converted = adc.convert(Adc::Vrefint, vrefintRaw) && adc.convert(Adc::Vsense, vsenseRaw);

   // The libart-stm32 sensor reads the two raw values through references.
   volatile uint16_t vsense = vsenseRaw;
   volatile uint16_t vrefint = vrefintRaw;
   InternalTemperatureSensor sensor(vsense, vrefint, 12, &vddaFromVrefint);
   const auto celsius = sensor.getTemperature();
   const int tenths = static_cast<int>(celsius * 10);
   (void)vcp.printf("  vrefint %u, vsense %u raw: %d.%d C\r\n", vrefintRaw, vsenseRaw, tenths / 10,
                    tenths < 0 ? -(tenths % 10) : tenths % 10);
   check("temperature-sensor-libart", converted && celsius > 10 && celsius < 70);

   const bool sequenceOk = convertSequence();
   (void)vcp.printf("  sequence: vrefint %u, vsense %u raw\r\n", sequence[0], sequence[1]);
   check("adc1-sequence-gpdma", sequenceOk && near(sequence[0], vrefintRaw, 40) && near(sequence[1], vsenseRaw, 40));

   // 16 samples, shifted right by 4: again a 12-bit value.
   adc.setOversampling(Adc::OversamplingRatio::X16, 4, true, false);
   uint16_t oversampled = 0;
   const bool oversamplingOk = adc.convert(Adc::Vrefint, oversampled);
   adc.disableOversampling();
   (void)vcp.printf("  vrefint oversampled x16: %u raw\r\n", oversampled);
   check("adc1-oversampling-x16", oversamplingOk && near(oversampled, vrefintRaw, 20));
}

} // namespace hwtest
