#include "uta_Audio.h"
#include "uta_Display.h"
#include "uta_SDCard.h"
#include "uta_handler.h"

void system_reboot_with_display(){
    delay(3000);
    display.display_text("Rebooting in: 3", 0, 0); delay(1000);
    display.display_text("Rebooting in: 2", 0, 0); delay(1000);
    display.display_text("Rebooting in: `", 0, 0); delay(1000);
}

void setup() {
    Serial.begin(115200);
    setCpuFrequencyMhz(240);
    ts.begin(40, 16, 15);

    Serial.println(F("╔══════════════════ おかえり~~~ :3 ═══════════════════╗"));

    if (!display.begin())  { 
        Serial.println("Display init failed");
        system_reboot();
    }
    if (!audio.begin())    { 
        Serial.println("Audio init failed");
        display.display_text("DAC error", 0, 0);
        system_reboot_with_display();
    }
    if (!sdcard_begin())    { 
        Serial.println("SD card init failed");  
        display.display_text("SD card error", 0, 0);
        system_reboot_with_display();
    }

    load_directory(current_directory);

    Serial.println(F("╚════════════════════════════════════════════════════╝"));
    display.display_png(StaticBg, sizeof(StaticBg));
    display.set_brightness(current_brightness);

    view_help();

    if (!audio.player.begin()) {
        Serial.println("Failed to start player");
        while(1);
    }
    audio.player.setVolume(current_volume);
    audio.player.setAutoNext(true);
}

void loop() {
    audio.player.copy();

    handle_serial();
    handle_touch();
    handle_keypad();

    // Progress bar update
    unsigned long now = millis();
    float current_time = audio.get_current_time();

    if (audio.player.isActive() &&
        (now - last_progress_update_ms >= PROGRESS_UPDATE_INTERVAL_MS ||
         fabsf(current_time - last_reported_time_s) >= 1.0f)) {

        display.update_progress(current_time, audio.get_audio_duration());
        last_progress_update_ms = now;
        last_reported_time_s = current_time;
    }
}