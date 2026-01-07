#pragma once

#include <SPI.h>
#include <TFT_eSPI.h>
#include <PNGdec.h>
#include <FT6236.h>

#include "Koruri-Regular24.h"
#include "BootBg.h"

#define MAX_IMAGE_WIDTH 320
#define FONT Koruri_Regular24
#define SMOOTH_FONT

// Text positions
#define TITLE_Y    0
#define ARTIST_Y   30
#define ALBUM_Y    100
#define PROGRESS_Y 450

// Volume popup
#define VOLUME_X (MAX_IMAGE_WIDTH - 80)
#define VOLUME_Y (PROGRESS_Y + 15)
#define VOLUME_W 70
#define VOLUME_H 30

#define MAX_TEXT_LEN 128

#define TFT_BL_PIN 3

class DisplayManager {
public:
  DisplayManager() : tft(TFT_eSPI()) {}

  bool begin() {
    tft.init();
    tft.setRotation(0);
    tft.setAttribute(UTF8_SWITCH, true);
    tft.setAttribute(PSRAM_ENABLE, true);

    tft_mutex = xSemaphoreCreateMutex();
    if (!tft_mutex) {
      Serial.println("[ERROR] TFT mutex failed");
      return false;
    }

    cmd_queue = xQueueCreate(32, sizeof(DisplayCommand));
    if (!cmd_queue) {
      Serial.println("[ERROR] Display queue failed");
      return false;
    }

    xTaskCreatePinnedToCore(display_worker_task, "DispWorker", 8192, this,
                            1, &worker_task_handle, 1);

    Serial.println("Loading boot image...");
    display_png_blocking(BootBg, sizeof(BootBg));
    display_text("おかえり~~~ :3", 0, 0);
    vTaskDelay(pdMS_TO_TICKS(1000));

    Serial.println("Display ready!!!!");
    return true;
  }

  void set_brightness(uint8_t brightness_percent) {
      if (brightness_percent > 100) {
          Serial.println("[WARNING] Backlight value exceeds 100; clamping to 100");
          brightness_percent = 100;
      }
      analogWrite(TFT_BL_PIN, static_cast<uint16_t>(brightness_percent) * 255 / 100);
  }

  bool display_png(const uint8_t image[], size_t size) {
    struct Params { const uint8_t* img; size_t sz; DisplayManager* dm; };
    auto* p = new Params{image, size, this};
    xTaskCreatePinnedToCore([](void* arg) {
      auto* pp = (Params*)arg;
      pp->dm->decode_png_yielding(pp->img, pp->sz);
      delete pp;
      vTaskDelete(nullptr);
    }, "PNGDecode", 12000, p, 1, nullptr, 1);
    return true;
  }

  void display_text(const char* text, uint8_t x = 0, uint8_t y = 0) {
    DisplayCommand cmd{};
    cmd.type = CMD_TEXT;
    strncpy(cmd.text, text, MAX_TEXT_LEN - 1);
    cmd.text[MAX_TEXT_LEN - 1] = '\0';
    cmd.x = x; cmd.y = y;
    xQueueSend(cmd_queue, &cmd, 0);
  }

  void update_progress(float current, float duration) {
    DisplayCommand cmd{CMD_PROGRESS, {}, 0, 0, current, duration};
    xQueueSend(cmd_queue, &cmd, 0);
  }

  void show_volume(float vol) {
    DisplayCommand cmd{CMD_VOLUME, {}, 0, 0, 0, 0, vol};
    xQueueSend(cmd_queue, &cmd, 0);
  }

  ~DisplayManager() {
    stop_worker = true;
    for (auto& f : scroll_fields) f.active = false;

    vTaskDelay(pdMS_TO_TICKS(50));
    if (smooth_scroll_task_handle) vTaskDelete(smooth_scroll_task_handle);
    if (worker_task_handle) vTaskDelete(worker_task_handle);
    if (cmd_queue) vQueueDelete(cmd_queue);
    if (tft_mutex) vSemaphoreDelete(tft_mutex);
  }

private:
  TFT_eSPI tft;
  PNG png;

  SemaphoreHandle_t tft_mutex = nullptr;
  TaskHandle_t worker_task_handle = nullptr;
  TaskHandle_t smooth_scroll_task_handle = nullptr;
  QueueHandle_t cmd_queue = nullptr;
  volatile bool stop_worker = false;

  bool progress_bar_initialized = false;
  int last_bar_width = 0;
  char last_time_str[20] = {0};
  float last_volume = -1;

  struct ScrollText {
    char text[MAX_TEXT_LEN] = {0};
    uint8_t x = 0, y = 0;
    int16_t offset = 0;
    int16_t width = 0;
    bool active = false;
    TickType_t pause_until = 0;
  };
  ScrollText scroll_fields[3];  // title, artist, album
  volatile bool scroll_task_running = false;

  enum CommandType { CMD_TEXT, CMD_PROGRESS, CMD_VOLUME };
  struct DisplayCommand {
    CommandType type = CMD_TEXT;
    char text[MAX_TEXT_LEN] = {0};
    uint8_t x = 0, y = 0;
    float current_time = 0;
    float duration = 0;
    float volume = 0;
  };

  void decode_png_yielding(const uint8_t* image, size_t size) {
    xSemaphoreTake(tft_mutex, portMAX_DELAY);
    tft.startWrite();
    int rc = png.openFLASH((uint8_t*)image, size, png_draw_callback);
    if (rc == PNG_SUCCESS) png.decode(this, 0);
    png.close();
    tft.endWrite();
    xSemaphoreGive(tft_mutex);
  }

  static int png_draw_callback(PNGDRAW* p_draw) {
    uint16_t line[MAX_IMAGE_WIDTH];
    auto* dm = (DisplayManager*)p_draw->pUser;
    dm->png.getLineAsRGB565(p_draw, line, PNG_RGB565_BIG_ENDIAN, 0xffffffff);
    dm->tft.pushImage(0, p_draw->y, p_draw->iWidth, 1, line);

    if (p_draw->y % 16 == 15) {
      dm->tft.endWrite();
      vTaskDelay(1);
      dm->tft.startWrite();
    }
    return 1;
  }

  bool display_png_blocking(const uint8_t image[], size_t size) {
    xSemaphoreTake(tft_mutex, portMAX_DELAY);
    tft.startWrite();
    bool ok = (png.openFLASH((uint8_t*)image, size, png_draw_callback) == PNG_SUCCESS);
    if (ok) {
      tft.fillScreen(TFT_BLACK);
      png.decode(this, 0);
    }
    png.close();
    tft.endWrite();
    xSemaphoreGive(tft_mutex);
    return ok;
  }

  static void display_worker_task(void* pv) {
    auto* dm = (DisplayManager*)pv;
    TFT_eSprite spr(&dm->tft);
    spr.setColorDepth(16);
    spr.loadFont(FONT);
    spr.createSprite(MAX_IMAGE_WIDTH + 120, 40);

    DisplayCommand cmd;
    while (!dm->stop_worker) {
      if (xQueueReceive(dm->cmd_queue, &cmd, pdMS_TO_TICKS(100)) == pdPASS) {
        switch (cmd.type) {
          case CMD_TEXT:     dm->handle_text(&spr, cmd.text, cmd.x, cmd.y); break;
          case CMD_PROGRESS: dm->handle_progress(cmd.current_time, cmd.duration); break;
          case CMD_VOLUME:   dm->handle_volume(cmd.volume); break;
        }
      }
      vTaskDelay(1);
    }

    spr.deleteSprite();
    spr.unloadFont();
    vTaskDelete(nullptr);
  }

void handle_text(TFT_eSprite* spr, const char* text, uint8_t x, uint8_t y) {
    int16_t tw = spr->textWidth(text);
    int field_idx = (y == TITLE_Y ? 0 : y == ARTIST_Y ? 1 : y == ALBUM_Y ? 2 : -1);

    xSemaphoreTake(tft_mutex, portMAX_DELAY);

    tft.fillRect(0, y, MAX_IMAGE_WIDTH, 40, TFT_BLACK);

    if (tw <= MAX_IMAGE_WIDTH || field_idx == -1) {
      // No Carousel-ing
      spr->fillSprite(TFT_BLACK);
      spr->setTextDatum(MC_DATUM);
      spr->setTextColor(TFT_WHITE);
      spr->drawString(text, MAX_IMAGE_WIDTH / 2, 20);
      spr->pushSprite(0, y);
      
      if (field_idx >= 0) 
        scroll_fields[field_idx].active = false;

    } else {
      spr->fillSprite(TFT_BLACK);
      spr->setTextDatum(TL_DATUM);
      spr->setTextColor(TFT_WHITE);

      // Setup scrolling field
      auto& f = scroll_fields[field_idx];
      strncpy(f.text, text, MAX_TEXT_LEN - 1);
      f.text[MAX_TEXT_LEN - 1] = '\0';
      f.x = 0;
      f.y = y;
      f.width = tw;
      f.offset = MAX_IMAGE_WIDTH;
      f.active = true;
      f.pause_until = 0;
      start_smooth_scroller();
    }
    xSemaphoreGive(tft_mutex);
  }

  void start_smooth_scroller() {
    if (scroll_task_running) return;
    scroll_task_running = true;
    xTaskCreatePinnedToCore([](void* p) {
      ((DisplayManager*)p)->smooth_scroll_loop();
      vTaskDelete(nullptr);
    }, "SmoothScroll", 7168, this, 0, &smooth_scroll_task_handle, 1);
  }

  void smooth_scroll_loop() {
    TFT_eSprite spr(&tft);
    if (!spr.createSprite(MAX_IMAGE_WIDTH + 120, 40-2)) {
      return;
    }
    spr.loadFont(FONT);
    spr.setColorDepth(8);  // Move up for safety
    spr.setTextWrap(false, true);

    int y_center = (40 - spr.fontHeight()) / 2;
    spr.setTextDatum(TL_DATUM);
    spr.setTextColor(TFT_WHITE);

    while (true) {
      bool any_active = false;
      TickType_t now = xTaskGetTickCount();

      xSemaphoreTake(tft_mutex, portMAX_DELAY);
      for (auto& f : scroll_fields) {
        if (!f.active || now < f.pause_until) continue;
        any_active = true;

        f.offset--;
        if (f.offset <= -(f.width + 100)) {
          f.offset = MAX_IMAGE_WIDTH;
          f.pause_until = now + pdMS_TO_TICKS(1400);
          // No continue; draw off-right if desired (invisible)
        }

        spr.fillSprite(TFT_BLACK);
        spr.drawString(f.text, f.offset, y_center);
        spr.pushSprite(f.x, f.y);
      }
      xSemaphoreGive(tft_mutex);

      if (!any_active) {
        scroll_task_running = false;
        break; 
      }

      vTaskDelay(pdMS_TO_TICKS(33)); // 30 fps
    }
    spr.deleteSprite();
    spr.unloadFont();
  }

  void handle_progress(float cur, float dur) {
    if (dur <= 0) return;
    float prog = constrain(cur / dur, 0.0f, 1.0f);
    int bar_w = (int)(prog * (MAX_IMAGE_WIDTH - 20) + 0.5f);

    char time_str[20];
    snprintf(time_str, sizeof(time_str), "%02d:%02d / %02d:%02d",
             (int)cur / 60, (int)cur % 60,
             (int)dur / 60, (int)dur % 60);

    bool update_bar = (bar_w != last_bar_width) || !progress_bar_initialized;
    bool update_txt = strcmp(time_str, last_time_str) != 0;

    if (update_bar || update_txt) {
      xSemaphoreTake(tft_mutex, portMAX_DELAY);
      if (update_bar) {
        tft.fillRect(10, PROGRESS_Y, MAX_IMAGE_WIDTH - 20, 10, tft.color565(220, 200, 240));
        tft.fillRect(10, PROGRESS_Y, bar_w, 10, tft.color565(180, 150, 220));
        last_bar_width = bar_w;
        progress_bar_initialized = true;
      }
      if (update_txt) {
        tft.fillRect(0, PROGRESS_Y + 15, MAX_IMAGE_WIDTH, 16, TFT_BLACK);
        tft.setTextSize(2);
        tft.setTextColor(tft.color565(240, 230, 255));
        tft.setCursor((MAX_IMAGE_WIDTH - tft.textWidth(time_str)) / 2, PROGRESS_Y + 15);
        tft.print(time_str);
        strcpy(last_time_str, time_str);
      }
      xSemaphoreGive(tft_mutex);
    }
  }

  void handle_volume(float vol) {
    if (vol < 0 || vol > 100) return;
    char buf[8];
    snprintf(buf, sizeof(buf), "%.0f%%", vol);

    xSemaphoreTake(tft_mutex, portMAX_DELAY);
    tft.fillRect(VOLUME_X, VOLUME_Y, VOLUME_W, VOLUME_H, TFT_BLACK);
    tft.setTextSize(1);
    tft.setTextColor(TFT_WHITE);
    tft.setCursor(VOLUME_X + 8, VOLUME_Y + 6);
    tft.print(buf);
    xSemaphoreGive(tft_mutex);

    last_volume = vol;
    vTaskDelay(pdMS_TO_TICKS(2000));

    if (last_volume == vol) {
      xSemaphoreTake(tft_mutex, portMAX_DELAY);
      tft.fillRect(VOLUME_X, VOLUME_Y, VOLUME_W, VOLUME_H, TFT_BLACK);
      xSemaphoreGive(tft_mutex);
      last_volume = -1;
    }
  }
};