#include "DeviceGps.h"
#if !CPADV_DEMO
#include <Arduino.h>
#include <CpadvLog.h>
#include <target.h>

void Serial1Port::open(uint32_t baud) {
  Serial1.end();
  Serial1.setRxBufferSize(1024);   // a full NMEA second is ~500 bytes
  // MeshCore naming: PIN_GPS_TX is the GPS's TX line (= our RX)
  Serial1.begin(baud, SERIAL_8N1, PIN_GPS_TX, PIN_GPS_RX);
  CPLOG_D("GPS", "Serial1 %lu baud, ESP RX %d <- GPS TX, ESP TX %d -> GPS RX", (unsigned long)baud, PIN_GPS_TX,
          PIN_GPS_RX);
}
int Serial1Port::available() { return Serial1.available(); }
int Serial1Port::read() { return Serial1.read(); }
size_t Serial1Port::write(const uint8_t* data, size_t len) { return Serial1.write(data, len); }

uint32_t MeshClock::now() { return rtc_clock.getCurrentTime(); }
void MeshClock::set(uint32_t unix) { rtc_clock.setCurrentTime(unix); }

void DeviceGps::begin() {
  CPLOG_I("GPS", "UART: ESP RX %d <- GPS TX, ESP TX %d -> GPS RX", PIN_GPS_TX, PIN_GPS_RX);
  _link.begin(millis());
}

bool DeviceGps::present() { return _link.present(millis()); }

void DeviceGps::loop() { _link.loop(millis()); }

#endif  // !CPADV_DEMO
