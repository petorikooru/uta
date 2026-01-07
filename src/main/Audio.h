#include "Display.h"

#pragma once

#include "AudioTools.h"
#include "AudioTools/Disk/AudioSourceSDFAT.h"
#include "AudioTools/AudioCodecs/CodecFLACFoxen.h"

#include "SDCard.h"

FsFile* fileToStream(const char* path, FsFile& oldFile);

I2SStream i2s;
FLACDecoderFoxen decoder;
AudioSourceVector<FsFile> source(fileToStream);
AudioPlayer player(source, i2s, decoder);

struct Metadata {
  String title;
  String artist;
} currentTrack;

FsFile audioFile;
FsFile metaFile;

void printMetaData(MetaDataType type, const char* str, int len){
  Serial.print("==> ");
  Serial.print(toStr(type));
  Serial.print(": ");
  Serial.println(str);
}

bool boot_i2s(){
  auto config = i2s.defaultConfig(TX_MODE);
  config.pin_bck = 8;
  config.pin_ws = 17;
  config.pin_data = 18;

  if (!i2s.begin(config)) {
    Serial.println("Failed to start I2S");
    return false;
  }

  Serial.println("[INFO] : PCM5102 (i2s) is successfully initialized!");
  return true;
}


void getVorbisData(FsFile& file, uint32_t size) {
  uint32_t vendorLen;
  file.read(&vendorLen, 4);

  file.seek(file.position() + vendorLen);

  uint32_t commentCount;
  file.read(&commentCount, 4);

  for (uint32_t i = 0; i < commentCount; i++) {
    uint32_t len;
    file.read(&len, 4);

    char buf[len + 1];
    file.read(buf, len);
    buf[len] = 0;

    // convert to uppercase for easier comparison
    String entry = String(buf);

    // Check for TITLE=
    if (entry.startsWith("TITLE=")) {
      currentTrack.title = entry.substring(6);
      Serial.print("[METADATA] Title: ");
      Serial.println(currentTrack.title);
    }

    // Check for ARTIST=
    if (entry.startsWith("ARTIST=")) {
      currentTrack.artist = entry.substring(7);
      Serial.print("[METADATA] Artist: ");
      Serial.println(currentTrack.artist);
    }
  }
}

bool getFlacMetadata(FsFile& file) {
  file.seek(0);

  // Check FLAC signature
  char sig[4];
  file.read(sig, 4);
  if (strncmp(sig, "fLaC", 4) != 0) {
    Serial.println("Not a FLAC file!!!!");
    return false;
  }

  bool lastBlock = false;

  while (!lastBlock) {
    uint8_t header[4];
    file.read(header, 4);

    lastBlock = header[0] & 0x80;
    uint8_t blockType = header[0] & 0x7F;
    uint32_t blockSize = (header[1] << 16) | (header[2] << 8) | header[3];

    if (blockType == 4) {
      getVorbisData(file, blockSize);
      return true;
    }

    file.seek(file.position() + blockSize);
  }

  return false;
}

// Callback to convert file path to stream for AudioSourceVector
FsFile* fileToStream(const char* path, FsFile& oldFile) {
  oldFile.close();
  audioFile.open(path);

  Serial.print("Playing: ");
  Serial.println(path);

  if (metaFile.open(path)) {
    getFlacMetadata(metaFile);
    metaFile.close();
  }

  display_text(currentTrack.title.c_str(), 0, 0);
  display_text(currentTrack.artist.c_str(), 0, 30);

  if (!audioFile) {
    LOGE("Failed to open: %s", path);
    return nullptr;
  }

  return &audioFile;
}
