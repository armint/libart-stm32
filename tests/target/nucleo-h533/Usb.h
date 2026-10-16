#pragma once

// USB part of the hardware test image: a CDC-ACM echo device on CN3 with the
// libart-stm32 Stm32UsbDeviceController and UsbSerial on the H5 USB driver
// of system-stm32. hwtest.py checks it from the host.

namespace hwtest {

/**
 * @brief Starts HSI48 with the CRS, the USB pins and the CDC-ACM device
 * (pid.codes test ID 1209:0001). Prints "UID <hex>": the USB serial number.
 * @return false if the HSI48 or the CRS setup failed.
 */
bool startUsb();

/**
 * @brief Main loop work: runs the USB controller and the CDC echo. Prints the
 * state changes ("USB configured", "USB terminal open", ...). Once the device
 * is configured, it prints the check "crs-sync-usb-sof": the CRS must trim
 * HSI48 from the USB start-of-frame packets.
 */
void pollUsb();

} // namespace hwtest
