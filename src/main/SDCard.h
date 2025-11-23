#include "common/FsApiConstants.h"

#pragma once

#include "sdios.h"
#include "SdFat.h"

#include "Display.h"
#include <PNGdec.h>

#define SPI_MISO 37
#define SPI_MOSI 35
#define SPI_SCK 36
#define SD_CS_PIN 39

#define SD_FAT_TYPE 3
#define SPI_CLOCK SD_SCK_MHZ(70)

SdFs sd;

/// Taken from https://github.com/greiman/SdFat/issues/450
class ExFatSPI : public SdSpiBaseClass {
public:
  void activate() {
    SPI.beginTransaction(m_spiSettings);
  }

  void begin(SdSpiConfig config) {
    (void)config;
    SPI.begin(SPI_SCK, SPI_MISO, SPI_MOSI, -1);
  }

  void deactivate() {
    SPI.endTransaction();
  }

  uint8_t receive() {
    return SPI.transfer(0XFF);
  }

  uint8_t receive(uint8_t* buf, size_t count) {
    for (size_t i = 0; i < count; i++) {
      buf[i] = SPI.transfer(0XFF);
    }
    return 0;
  }

  void send(uint8_t data) {
    SPI.transfer(data);
  }

  void send(const uint8_t* buf, size_t count) {
    for (size_t i = 0; i < count; i++) {
      SPI.transfer(buf[i]);
    }
  }

  void setSckSpeed(uint32_t maxSck) {
    m_spiSettings = SPISettings(maxSck, MSBFIRST, SPI_MODE0);
  }

private:
  SPISettings m_spiSettings;

} exfat_spi;

#define SD_CONFIG SdSpiConfig(SD_CS_PIN, DEDICATED_SPI, SD_SCK_MHZ(70), &exfat_spi)

bool boot_sdcard() {
  if (!sd.begin(SD_CONFIG)) {
    Serial.println("SD card initialization failed!");
    return false;
  }
  if (!sd.volumeBegin()) {
    Serial.println("SD card volume initialization failed!");
    return false;
  }

  Serial.println("[INFO] : SD Card is successfully initialized!");
  return true;
}