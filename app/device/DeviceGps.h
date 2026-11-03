#pragma once
// GPS of the Cap LoRa-1262 (AT6668, NMEA over UART: ESP RX 15 <- GPS TX, ESP TX 13 -> GPS RX).
// Thin Arduino wrapper (Serial1 + MeshCore clock) around the portable driver core/drivers/GpsLink,
// which is tested on PC against a fake GPS (sim/fakes/FakeGpsUart).
#include "../core/Services.h"
#include "../core/drivers/GpsLink.h"

class Serial1Port : public UartPort {
public:
  void open(uint32_t baud) override;
  int available() override;
  int read() override;
  size_t write(const uint8_t* data, size_t len) override;
};

class MeshClock : public WallClock {
public:
  uint32_t now() override;
  void set(uint32_t unix) override;
};

class DeviceGps : public GpsService {
public:
  DeviceGps() : _link(_port, &_clock) {}
  void begin();
  void loop();   // call often: drains the UART (bounded work per call)

  const GpsState& state() override { return _link.state(); }
  bool present() override;
  uint32_t baud() override { return _link.baud(); }
  void coldRestart() override { _link.coldRestart(); }

  void setRawEcho(bool on) { _link.setRawEcho(on); }
  uint32_t bytesReceived() const { return _link.bytesReceived(); }
  bool searching() const { return _link.searching(); }

private:
  Serial1Port _port;
  MeshClock _clock;
  GpsLink _link;
};
