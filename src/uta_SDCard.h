#pragma once

#include "sdios.h"
#include "SdFat.h"

#include "uta_Display.h"

#define SPI_MISO 40
#define SPI_MOSI 42
#define SPI_SCK 41
#define SPI_CS 2

#define SD_FAT_TYPE 2
#define SPI_CLOCK SD_SCK_MHZ(50)

SdFs sd;
extern DisplayManager display;

/// Taken from https://github.com/greiman/SdFat/issues/450
class ExFatSPI : public SdSpiBaseClass {
public:
  void activate() { SPI.beginTransaction(m_spiSettings); }
  void begin(SdSpiConfig config) {
    (void)config;
    SPI.begin(SPI_SCK, SPI_MISO, SPI_MOSI, -1);
  }
  void deactivate() { SPI.endTransaction(); }
  uint8_t receive() { return SPI.transfer(0XFF); }
  uint8_t receive(uint8_t* buf, size_t count) {
    for (size_t i = 0; i < count; i++) buf[i] = SPI.transfer(0XFF);
    return 0;
  }
  void send(uint8_t data) { SPI.transfer(data); }
  void send(const uint8_t* buf, size_t count) {
    for (size_t i = 0; i < count; i++) SPI.transfer(buf[i]);
  }
  void setSckSpeed(uint32_t maxSck) {
    m_spiSettings = SPISettings(maxSck, MSBFIRST, SPI_MODE0);
  }
private:
  SPISettings m_spiSettings;
} exfat_spi;

#define SD_CONFIG SdSpiConfig(SPI_CS, DEDICATED_SPI, SPI_CLOCK, &exfat_spi)

bool sdcard_begin() {
  if (!sd.begin(SD_CONFIG)) {
    return false;
  }
  return true;
}