#pragma once


#include "AudioTools.h"
#include "AudioTools/Disk/AudioSourceSDFAT.h"
#include "AudioTools/AudioCodecs/CodecFLACFoxen.h"
#include "AudioTools/AudioCodecs/CodecMP3Helix.h"
#include "AudioTools/AudioCodecs/CodecAACHelix.h"
#include "AudioTools/AudioCodecs/CodecWAV.h"
#include "AudioTools/Concurrency/RTOS.h"
#include "uta_SDCard.h"

extern DisplayManager display;

const char* getFileStem(const char* path) {
  if (!path || path[0] == '\0') return "";

  const char* filename = strrchr(path, '/');
  if (filename) filename++;
  else filename = path;

  const char* lastDot = nullptr;
  for (const char* p = filename; *p; ++p) {
    if (*p == '.') lastDot = p;
  }

  static char stem[128];
  size_t len;

  if (lastDot && lastDot != filename) {
    len = lastDot - filename;
  } else {
    len = strlen(filename);
  }

  if (len >= sizeof(stem)) len = sizeof(stem) - 1;
  memcpy(stem, filename, len);
  stem[len] = '\0';

  return stem;
}

void formatDuration(float seconds, char* buffer, size_t bufSize = 12) {
  if (seconds < 0) seconds = 0;
  if (bufSize < 9) return;

  uint32_t total = (uint32_t)seconds;
  uint16_t hrs = total / 3600;
  uint8_t mins = (total % 3600) / 60;
  uint8_t secs = total % 60;

  if (hrs > 0) {
    snprintf(buffer, bufSize, "%u:%02u:%02u", hrs, mins, secs);
  } else {
    snprintf(buffer, bufSize, "%u:%02u", mins, secs);
  }
}

class AudioManager {
public:

protected:

  class UtaI2S : public I2SStream {
  public:
    uint64_t bytes_written = 0;

    size_t write(const uint8_t* buffer, size_t size) override {
      size_t res      = I2SStream::write(buffer, size);
      bytes_written  += res;

      return res;
    }

    float getAudioCurrentTime() {
      auto info = audioInfo();
      if (info.sample_rate == 0) 
        return 0.0f;
      int byte_rate = info.sample_rate * info.channels * (info.bits_per_sample / 8);

      return byte_rate > 0 ? (float)bytes_written / byte_rate : 0.0f;
    }

    void resetBytesWritten() {
      bytes_written = 0;
    }
  };

  static FsFile audio_file;
  static FsFile meta_file;
  static bool played;

  static UtaI2S i2s;

  MultiDecoder      decoder;
  FLACDecoderFoxen  flac_decoder;
  MP3DecoderHelix   mp3_decoder;
  AACDecoderHelix   aac_decoder;
  WAVDecoder        wav_decoder;


  static FsFile* file_to_stream(const char* path, FsFile& old_file) {

    if (old_file.isOpen()) {
      old_file.close();
    }

    i2s.resetBytesWritten();
    reset_current_time();
    current_duration = 0.0f;

    current_track.artist.clear();
    current_track.title.clear();
    current_track.album.clear();

    String fullPath = String(path);
    String filename = fullPath.substring(fullPath.lastIndexOf('/') + 1);
    filename.toLowerCase();

    const char* supported[] = { ".mp3", ".flac", ".wav" };
    bool isSupported = false;
    for (const char* ext : supported) {
      if (filename.endsWith(ext)) {
        isSupported = true;
        break;
      }
    }

    if (!isSupported) {
      Serial.printf(" Skipping non-audio: %s\n", filename.c_str());
      audio_file.open("");
      return &audio_file;
    }

    if (played) {
      Serial.println();
      Serial.println(F("══════════════════════════════════════════════════════════════"));
      Serial.println(F("                         NEXT TRACK                            "));
      Serial.println(F("══════════════════════════════════════════════════════════════"));
    } else {
      Serial.println();
      Serial.println(F("══════════════════════════════════════════════════════════════"));
      Serial.println(F("                       STARTING PLAYBACK                       "));
      Serial.println(F("══════════════════════════════════════════════════════════════"));
      played = true;
    }

    Serial.printf(" File: %s\n", filename.c_str());

    FsFile meta_file;
    bool metadata_ok = false;

    if (meta_file.open(path)) {
      extract_metadata(meta_file, path);
      meta_file.close();
    } else {
      Serial.println(" Failed to open file for metadata");
    }

    // Fallback: use filename if no title
    if (current_track.title.isEmpty())  current_track.title   = getFileStem(path);
    if (current_track.artist.isEmpty()) current_track.artist  = "Unknown Artist";
    if (current_track.album.isEmpty())  current_track.album   = "Unknown Album";

    Serial.println(F("──────────────────────────────────────────────────────────────"));
    Serial.printf(" Title  : %s\n", current_track.title.c_str());
    Serial.printf(" Artist : %s\n", current_track.artist.c_str());
    Serial.printf(" Album  : %s\n", current_track.album.c_str());
    Serial.println(F("──────────────────────────────────────────────────────────────"));

    display.display_text(current_track.title.c_str(), 0, TITLE_Y);
    display.display_text(current_track.artist.c_str(), 0, ARTIST_Y);

    if (!audio_file.open(path)) {
      Serial.println();
      Serial.println(F(" ERROR: Cannot open audio file!"));
      Serial.println(F("        File may be unsupported."));
      Serial.println(F("══════════════════════════════════════════════════════════════\n"));

      // Trick the player to automatically skip stuff
      audio_file.open("");
      return &audio_file;
    }


    if      (filename.endsWith(".flac"))  Serial.println(" Format: FLAC (Lossless)");
    else if (filename.endsWith(".mp3"))   Serial.println(" Format: MP3");
    else if (filename.endsWith(".wav"))   Serial.println(" Format: WAV (Uncompressed)");

    Serial.println(F("══════════════════════════════════════════════════════════════\n"));

    return &audio_file;
  }

  static void get_vorbis_data(FsFile& file, uint32_t size) {
    uint32_t vendor_len;
    if (file.read(&vendor_len, 4) != 4) return;

    file.seek(file.position() + vendor_len);

    uint32_t comment_count;
    if (file.read(&comment_count, 4) != 4) return;

    const size_t max_buffer_size = 256;
    for (uint32_t i = 0; i < comment_count; i++) {
      uint32_t len;
      if (file.read(&len, 4) != 4) break;

      size_t read_len = min(len, max_buffer_size);
      char buf[read_len + 1];
      if (file.read(buf, read_len) != read_len) break;
      buf[read_len] = '\0';

      String entry = String(buf);
      if      (entry.startsWith("TITLE=")) current_track.title = entry.substring(6);
      else if (entry.startsWith("title=")) current_track.title = entry.substring(6);

      if      (entry.startsWith("ARTIST=")) current_track.artist = entry.substring(7);
      else if (entry.startsWith("artist=")) current_track.artist = entry.substring(7);

      if      (entry.startsWith("ALBUM=")) current_track.album = entry.substring(6);
      else if (entry.startsWith("album=")) current_track.album = entry.substring(6);
    }
  }

  static bool get_flac_metadata(FsFile& file) {
    file.seek(0);
    char sig[4];
    if (file.read(sig, 4) != 4 || strncmp(sig, "fLaC", 4) != 0) {
      Serial.println("[WARN] Not a FLAC file");
      return false;
    }

    bool last_block = false;
    while (!last_block) {
      uint8_t header[4];
      if (file.read(header, 4) != 4) break;

      last_block = header[0] & 0x80;
      uint8_t block_type = header[0] & 0x7F;
      uint32_t block_size = (header[1] << 16) | (header[2] << 8) | header[3];

      if (block_type == 0) {
        uint8_t buf[34];

        if (file.read(buf, 34) != 34) return false;
        uint32_t sample_rate = ((uint32_t)buf[10] << 12) | (buf[11] << 4) | ((buf[12] >> 4) & 0x0F);
        uint8_t channels = ((buf[12] & 0x0E) >> 1) + 1;
        uint8_t bps = (((buf[12] & 0x01) << 4) | ((buf[13] >> 4) & 0x0F)) + 1;
        uint64_t total_samples = ((uint64_t)(buf[13] & 0x0F) << 32) | ((uint64_t)buf[14] << 24) | ((uint64_t)buf[15] << 16) | ((uint64_t)buf[16] << 8) | buf[17];

        current_duration = (float)total_samples / sample_rate;

        static char duration_str[12] = { 0 };
        formatDuration(current_duration, duration_str, 12);
        Serial.printf("\n Duration: [%s]\n", duration_str);

      } else if (block_type == 4) {
        get_vorbis_data(file, block_size);
        return true;
      } else {
        file.seek(file.position() + block_size);
      }
    }
    return false;
  }

  static bool get_mp3_id3v1_fallback(FsFile& file) {
    if (file.size() < 128) return false;
    file.seek(file.size() - 128);
    char tag[3];
    if (file.read(tag, 3) != 3 || strncmp(tag, "TAG", 3)) return false;

    char buf[125] = { 0 };
    file.read((uint8_t*)buf, 125);

    char title[31] = { 0 }, artist[31] = { 0 }, album[31] = { 0 };
    strncpy(title, buf + 3, 30);
    strncpy(artist, buf + 33, 30);
    strncpy(album, buf + 63, 30);

    if (title[0])   current_track.title = String(title);
    if (artist[0])  current_track.artist = String(artist);
    if (album[0])   current_track.album = String(album);

    Serial.printf("- [ID3v1] %s - %s (%s)\n", current_track.artist.c_str(), current_track.title.c_str(), current_track.album.c_str());
    return true;
  }

  static void estimate_mp3_duration(FsFile& file) {
    file.seek(0);
    uint8_t buf[1024];
    size_t read = file.read(buf, sizeof(buf));
    int sample_rate = 44100;
    int bitrate = 128000;

    for (int i = 0; i < read - 4; i++) {
      if (buf[i] == 0xFF && (buf[i + 1] & 0xE0) == 0xE0) {
        int version = (buf[i + 1] & 0x18) >> 3;
        int sr_idx = (buf[i + 2] & 0x0C) >> 2;
        int br_idx = (buf[i + 2] & 0xF0) >> 4;

        static const int sr_table[3][3] = { 
          { 44100, 48000, 32000 }, 
          { 22050, 24000, 16000 }, 
          { 11025, 12000, 8000 } 
        };

        static const int br_table[2][15] = {
          { 0, 32, 64, 96, 128, 160, 192, 224, 256, 288, 320, 352, 384, 416, 448 },
          { 0, 32, 48, 56, 64, 80, 96, 112, 128, 144, 160, 176, 192, 224, 256 }
        };

        sample_rate = sr_table[version == 3 ? 0 : (version == 2 ? 1 : 2)][sr_idx];
        bitrate = br_table[version == 3 ? 0 : 1][br_idx] * 1000;
        break;
      }
    }

    if (bitrate > 0) {
      current_duration = (float)file.size() * 8 / bitrate;
      Serial.printf("- Estimated Duration: %.2f s\n", current_duration);
    }
  }

  // Idk what does it do exactly, but basically just extract metadata
  static bool get_mp3_metadata(FsFile& file) {
    file.seek(0);
    char header[10];
    if (file.read(header, 10) != 10 || strncmp(header, "ID3", 3)) {
      estimate_mp3_duration(file);
      return false;
    }

    uint32_t tagsize = ((header[6] & 0x7F) << 21) | ((header[7] & 0x7F) << 14) | ((header[8] & 0x7F) << 7) | (header[9] & 0x7F);

    size_t pos = 10;
    while (pos < tagsize + 10 && pos + 10 < file.size()) {
      char frame[4];
      if (file.read((uint8_t*)frame, 4) != 4 || frame[0] == 0) break;

      uint32_t fsize;
      file.read(&fsize, 4);
      fsize = __builtin_bswap32(fsize);
      file.seek(file.position() + 2);  // skip flags

      if (fsize > 512) {
        pos += 10 + fsize;
        file.seek(pos);
        continue;
      }

      char* data = new char[fsize + 1];
      file.read((char*)data, fsize);
      data[fsize] = 0;

      uint8_t enc_byte = (uint8_t)data[0];
      int enc = enc_byte;
      char* text_start = data + 1;
      size_t avail_bytes = fsize - 1;
      bool little_endian = false;
      if (enc == 1 && avail_bytes >= 2) {
        uint8_t bom1 = (uint8_t)text_start[0];
        uint8_t bom2 = (uint8_t)text_start[1];
        if ((bom1 == 0xFF && bom2 == 0xFE) || (bom1 == 0xFE && bom2 == 0xFF)) {
          little_endian = (bom2 == 0xFE);
          text_start += 2;
          avail_bytes -= 2;
        }
      }

      size_t text_bytes;
      if (enc == 3) {
        text_bytes = avail_bytes;
      } else {
        int unit_size = (enc == 0) ? 1 : 2;
        size_t num_units = avail_bytes / unit_size;
        size_t units_to_term = 0;
        const uint8_t* p = (const uint8_t*)text_start;
        if (unit_size == 1) {
          while (units_to_term < num_units && p[units_to_term] != 0) ++units_to_term;
        } else {
          while (units_to_term < num_units && (p[2 * units_to_term] != 0 || p[2 * units_to_term + 1] != 0)) ++units_to_term;
        }
        text_bytes = units_to_term * unit_size;
      }

      String value;
      if (enc == 3) {
        value = String(text_start, text_bytes);
      } else if (enc == 0) {
        value = "";
        const uint8_t* p = (const uint8_t*)text_start;
        for (size_t i = 0; i < text_bytes; ++i) {
          uint32_t ch = p[i];
          if (ch == 0) break;
          if (ch < 128) {
            value += (char)ch;
          } else {
            value += (char)(0xC0 | (ch >> 6));
            value += (char)(0x80 | (ch & 0x3F));
          }
        }
      } else {  // UTF-16 (enc 1 or 2)
        value = "";
        const uint8_t* p = (const uint8_t*)text_start;
        for (size_t i = 0; i < text_bytes / 2; ++i) {
          uint8_t b1 = p[2 * i];
          uint8_t b2 = p[2 * i + 1];
          uint16_t ch = little_endian ? (b2 << 8 | b1) : (b1 << 8 | b2);
          if (ch == 0) break;
          if (ch < 0x80) {
            value += (char)ch;
          } else if (ch < 0x800) {
            value += (char)(0xC0 | (ch >> 6));
            value += (char)(0x80 | (ch & 0x3F));
          } else {
            value += (char)(0xE0 | (ch >> 12));
            value += (char)(0x80 | ((ch >> 6) & 0x3F));
            value += (char)(0x80 | (ch & 0x3F));
          }
        }
      }

      if      (strncmp(frame, "TIT2", 4) == 0) current_track.title  = value;
      else if (strncmp(frame, "TPE1", 4) == 0) current_track.artist = value;
      else if (strncmp(frame, "TALB", 4) == 0) current_track.album  = value;

      delete[] data;
      pos += 10 + fsize;
      file.seek(pos);
    }

    estimate_mp3_duration(file);
    return true;
  }

  static bool get_wav_metadata(FsFile& file) {
    file.seek(0);
    char riff[4];
    if (file.read(riff, 4) != 4 || strncmp(riff, "RIFF", 4)) return false;

    file.seek(20);
    uint16_t format;
    file.read(&format, 2);
    if (format != 1) return false;  // PCM only

    uint16_t channels;
    file.read(&channels, 2);
    uint32_t sample_rate;
    file.read(&sample_rate, 4);
    uint32_t byte_rate;
    file.read(&byte_rate, 4);
    file.seek(file.position() + 6);
    uint16_t bits;
    file.read(&bits, 2);

    // Find data chunk
    while (true) {
      char chunk[4];
      if (file.read(chunk, 4) != 4) break;
      uint32_t size;
      file.read(&size, 4);

      if (strncmp(chunk, "data", 4) == 0) {
        current_duration = size / (float)byte_rate;
        Serial.printf("- Duration: %.2f s\n", current_duration);
        break;
      } else if (strncmp(chunk, "LIST", 4) == 0) {
        char type[4];
        file.read(type, 4);
        if (strncmp(type, "INFO", 4) == 0) {
          size_t end = file.position() + size - 4;
          while (file.position() + 8 < end) {
            char id[4];
            file.read(id, 4);
            uint32_t len;
            file.read(&len, 4);
            char* buf = new char[len + 1];
            file.read((uint8_t*)buf, len);
            buf[len && buf[len - 1] == 0 ? len - 1 : len] = 0;
            String val = buf;
            delete[] buf;

            if (strncmp(id, "INAM", 4) == 0) current_track.title = val;
            else if (strncmp(id, "IART", 4) == 0) current_track.artist = val;
            else if (strncmp(id, "IPRD", 4) == 0) current_track.album = val;

            if (len % 2) file.seek(file.position() + 1);
          }
        } else file.seek(file.position() + size - 4);
      } else {
        file.seek(file.position() + size);
      }
    }
    return true;
  }


  static void extract_metadata(FsFile& file, const char* path) {
    String ext = String(path);
    ext.toLowerCase();
    ext = ext.substring(ext.lastIndexOf('.') + 1);

    if (ext == "flac") {
      get_flac_metadata(file);
    } else if (ext == "mp3") {
      get_mp3_metadata(file);
    } else if (ext == "wav") {
      get_wav_metadata(file);
    }
  }

  static inline size_t min(size_t x, size_t y) {
    return (x < y) ? x : y;
  }
  static inline size_t max(size_t x, size_t y) {
    return (x > y) ? x : y;
  }

public:
  static struct Metadata {
    String title;
    String artist;
    String album;
  } current_track;

  static float current_duration;

  AudioSourceVector<FsFile> source;
  AudioPlayer               player;

  AudioManager()
    : source(&AudioManager::file_to_stream)
    , player(source, i2s, decoder) {}

  bool begin() {
    AudioLogger::instance().begin(Serial, AudioLogger::Warning);

    auto config = i2s.defaultConfig(TX_MODE);
    config.pin_bck          = 8;
    config.pin_ws           = 17;
    config.pin_data         = 18;

    decoder.addDecoder(mp3_decoder, "audio/mpeg");
    decoder.addDecoder(aac_decoder, "audio/aac");
    decoder.addDecoder(wav_decoder, "audio/wav");
    decoder.addDecoder(flac_decoder, "audio/flac");

    if (!i2s.begin(config)) {
      return false;
    }

    return true;
  }

  static void reset_current_time() {
    i2s.resetBytesWritten();
  }
  float get_current_time() {
    return i2s.getAudioCurrentTime();
  }
  float get_audio_duration() {
    return current_duration;
  }

  uint8_t get_file_index(AudioManager& audioManager) {
    return audioManager.source.index();
  }
};

AudioManager::Metadata    AudioManager::current_track;
AudioManager::UtaI2S      AudioManager::i2s;
FsFile                    AudioManager::audio_file;
FsFile                    AudioManager::meta_file;
bool                      AudioManager::played = false;
float                     AudioManager::current_duration = 0.0f;