#define ESP32
#define ESP32X

#include "uta_Audio.h"
#include "uta_Display.h"
#include "uta_SDCard.h"

#include "music.h"
#include "StaticBg.h"

DisplayManager display;
AudioManager audio;

uint8_t current_dir_index   = 0;
String  current_directory   = DIRECTORIES[current_dir_index];
float   current_volume      = 0.2f;
uint8_t current_brightness  = 50;

constexpr uint16_t PROGRESS_UPDATE_INTERVAL_MS = 1000;

FsFile dir;
NamePrinter name_printer(audio.source);

uint16_t    last_progress_update_ms = 0;
float       last_reported_time_s = -1.0f;

const char* flash_mode_to_string(uint8_t mode) {
    switch (mode) {
        case FM_QIO:  return "QIO";
        case FM_QOUT: return "QOUT";
        case FM_DIO:  return "DIO";
        case FM_DOUT: return "DOUT";
        default:      return "Unknown";
    }
}

void draw_bar(const char* label, uint32_t used, uint32_t total, const char* unit = "B") {
    if (total == 0) return;

    int percent = (used * 100) / total;
    int filled = (percent * 40) / 100;

    Serial.printf(" %s%*s: ", label, 12 - (int)strlen(label), "");
    Serial.printf("%6lu / %6lu %s  (%3d%%) [", used, total, unit, percent);

    for (int i = 0; i < 40; ++i) {
        if (i < filled) {
            if (percent > 90) Serial.print("█");      // Critical
            else if (percent > 70) Serial.print("▓"); // Warning
            else Serial.print("▓");                   // Normal
        } else {
            Serial.print("░");
        }
    }
    Serial.println("]");
}

void draw_simple_bar(const char* label, int percent) {
    int filled = (percent * 40) / 100;
    Serial.printf(" %s%*s: %3d%% [", label, 14 - (int)strlen(label), "", percent);
    for (int i = 0; i < 40; ++i) {
        Serial.print(i < filled ? "█" : "░");
    }
    Serial.println("]");
}

void show_resource_monitor() {
    Serial.println();
    Serial.println(F("╔══════════════════════════════════════════════════════════════╗"));
    Serial.println(F("║                              :3                              ║"));
    Serial.println(F("╚══════════════════════════════════════════════════════════════╝\n"));

    Serial.printf(" Chip      : %s Rev %d | Cores: 2 | Freq: %d MHz\n",
                  ESP.getChipModel(), ESP.getChipRevision(), ESP.getCpuFreqMHz());
    Serial.printf(" Flash     : %d MB %s", ESP.getFlashChipSize() / 1024 / 1024,
                  flash_mode_to_string(ESP.getFlashChipMode()));
    Serial.printf(" | PSRAM: %s", psramFound() ? "Yes" : "No ");
    if (psramFound()) Serial.printf("(%d MB)", ESP.getPsramSize() / 1024 / 1024);
    Serial.printf(" | Temp: %.1f°C\n", temperatureRead());
    Serial.printf(" Uptime    : %lu seconds\n\n", millis() / 1000);

    // Memory
    Serial.println(F("╔══════════════════════════ MEMORY ═══════════════════════════╗"));
    draw_bar("HEAP", ESP.getFreeHeap(), ESP.getHeapSize());
    if (psramFound()) {
        draw_bar("PSRAM", ESP.getFreePsram(), ESP.getPsramSize());
    }
    draw_bar("SKETCH", ESP.getSketchSize(), ESP.getSketchSize() + ESP.getFreeSketchSpace());
    Serial.println(F("╚══════════════════════════════════════════════════════════════╝\n"));

    Serial.println(F("╔══════════════════════════ CPU LOAD ══════════════════════════╗"));
    for (int core = 0; core < 2; ++core) {
        TaskHandle_t idle_task = xTaskGetIdleTaskHandleForCPU(core);
        UBaseType_t stack_hwm = idle_task ? uxTaskGetStackHighWaterMark(idle_task) : 0;

        int load_percent = 5;
        if (stack_hwm > 800) load_percent = 5;
        else if (stack_hwm > 500) load_percent = 20;
        else if (stack_hwm > 300) load_percent = 50;
        else if (stack_hwm > 150) load_percent = 80;
        else load_percent = 95;

        Serial.printf(" CORE %d    ", core);
        draw_simple_bar("", load_percent);
    }
    Serial.println(F("╚══════════════════════════════════════════════════════════════╝\n"));

    // Tasks
    Serial.println(F("╔══════════════════════════ TASKS ═════════════════════════════╗"));
    Serial.printf(" Total Running Tasks : %d\n", uxTaskGetNumberOfTasks());

    char* task_buf = (char*)malloc(2048);
    if (task_buf) {
        vTaskList(task_buf);
        Serial.println(F(" Active Tasks (top 6):"));
        char* line = strtok(task_buf, "\n");
        int count = 0;
        while (line && count < 6) {
            char name[20] = {}, state = ' ', core = '-';
            unsigned prio = 0, stack = 0;
            sscanf(line, "%s %c %u %u %c", name, &state, &prio, &stack, &core);
            char indicator = (state == 'R') ? '●' : (state == 'B') ? '○' : '■';
            Serial.printf("  %c %-16s (Prio %2u, Core %c, Stack %4u)\n", indicator, name, prio, core, stack);
            line = strtok(nullptr, "\n");
            ++count;
        }
        free(task_buf);
    }
    Serial.println(F("╚══════════════════════════════════════════════════════════════╝"));

    Serial.printf("\n Refreshed at %lus • Press 'e' to refresh\n\n", millis() / 1000);
}

void show_help_menu() {
    Serial.println();
    Serial.println(F("╔══════════════════════════════════════════════════════════════╗"));
    Serial.println(F("║                              :3                              ║"));
    Serial.println(F("╚══════════════════════════════════════════════════════════════╝\n"));
    Serial.println(F("  Directory Control"));
    Serial.println(F("   [r]  Next Folder      →"));
    Serial.printf("     Current: %s\n", current_directory.c_str());
    Serial.println(F("   [R]  Previous Folder"));
    Serial.println(F("   [v]  View Current Queue"));
    Serial.printf((  "   [V]  View Root (%s)\n"), ROOT);
    Serial.println();
    Serial.println(F("  Audio Control"));
    Serial.println(F("   [n]  Next Track        [p]  Previous Track"));
    Serial.println(F("   [s]  Play / Stop       [+]  Volume Up    [-]  Volume Down"));
    Serial.printf(   "   Volume: %d%%\n", (int)(current_volume * 100));
    Serial.println();
    Serial.println(F("  System"));
    Serial.println(F("   [e]  Resource Monitor"));
    Serial.println(F("   [x]  Restart ESP32"));
    Serial.println(F("   [h]  Show this help"));
    Serial.println();
    Serial.printf(" Uptime: %lus  |  Free Heap: %lu KB  |  Tasks: %d\n",
                  millis() / 1000, ESP.getFreeHeap() / 1024, uxTaskGetNumberOfTasks());
    Serial.println(F("╚══════════════════════════════════════════════════════════════╝\n"));
}

void load_directory(const String& path) {
    audio.player.stop();
    Serial.printf("[INFO] Loading directory: %s\n", path.c_str());
    current_directory = path;

    name_printer.flush();
    audio.source.clear();

    dir = sd.open(path.c_str(), O_READ);
    if (!dir) {
        Serial.println("[ERROR] Failed to open directory");
        return;
    }

    name_printer.setPrefix(path.c_str());
    dir.ls(&name_printer, LS_R);
    Serial.println(dir.curPosition());
    dir.close();
}

void change_to_next_directory() {
    current_dir_index = (current_dir_index + 1) % (sizeof(DIRECTORIES) / sizeof(DIRECTORIES[0]));
    load_directory(DIRECTORIES[current_dir_index]);

    Serial.println();
    Serial.println(F( "╔══════════════════ FOLDER CHANGED ══════════════════╗"));
    Serial.printf(    "║  → Now Playing From: %-30s ║\n", current_directory.c_str());
    Serial.println(F( "╚════════════════════════════════════════════════════╝"));

    sd.ls(current_directory.c_str(), LS_R | LS_SIZE);

    if (!audio.player.begin()) {
        Serial.println(F("Failed to initialize player → please skip to next folder"));
    }
}

void change_to_previous_directory() {
    current_dir_index = (current_dir_index == 0)
        ? (sizeof(DIRECTORIES) / sizeof(DIRECTORIES[0])) - 1
        : current_dir_index - 1;

    load_directory(DIRECTORIES[current_dir_index]);

    Serial.println();
    Serial.println(F( "╔══════════════════ FOLDER CHANGED ══════════════════╗"));
    Serial.printf(    "║  ← Now Playing From: %-30s ║\n", current_directory.c_str());
    Serial.println(F( "╚════════════════════════════════════════════════════╝"));

    sd.ls(current_directory.c_str(), LS_R | LS_SIZE);

    if (!audio.player.begin()) {
        Serial.println(F("Failed to initialize player → please skip to next folder"));
    }
}

// ======================== Input Handlers ========================

void handle_directory_command(char cmd) {
    switch (cmd) {
        case 'r': change_to_next_directory(); break;
        case 'R': change_to_previous_directory(); break;
        case 'v':
            Serial.println();
            Serial.println(F( "╔══════════════════ CURRENT QUEUE ═══════════════════╗"));
            Serial.printf(    "║  Directory: %s\n", current_directory.c_str());
            Serial.println(F( "╟────────────────────────────────────────────────────╢"));
            sd.ls(current_directory.c_str(), LS_SIZE);
            Serial.println(F( "╚════════════════════════════════════════════════════╝"));
            break;
        case 'V':
            Serial.println();
            Serial.println(F("╔═══════════════════ ROOT DIRECTORY ═════════════════╗"));
            sd.ls(ROOT, LS_SIZE);
            Serial.println(F("╚════════════════════════════════════════════════════╝"));
            break;
        case 'e': show_resource_monitor(); break;
        case 'x':
            Serial.println(F("╔════════════════════ SYSTEM ════════════════════════╗"));
            Serial.println(F("║             Restarting ESP32 in 3...               ║")); delay(1000);
            Serial.println(F("║             Restarting ESP32 in 2...               ║")); delay(1000);
            Serial.println(F("║             Restarting ESP32 in 1...               ║")); delay(1000);
            ESP.restart();
            break;
        case 'X': 
            Serial.println(F("╔════════════════════ SYSTEM ════════════════════════╗"));
            Serial.println(F("║             Shutting Down ESP32 in 3...            ║")); delay(1000);
            Serial.println(F("║             Shutting Down ESP32 in 2...            ║")); delay(1000);
            Serial.println(F("║             Shutting Down ESP32 in 1...            ║")); delay(1000);
            esp_deep_sleep_start(); 
            break;
        case 'h': case 'H': case '?':
            show_help_menu();
            break;
    }
}

void handle_audio_command(char cmd) {
    switch (cmd) {
        case '>':
            audio.player.next();
            Serial.println(F( "╔══════════════════ TRACK ══════════════════╗\n"
                              "║               ▶▶ Next Track               ║\n"
                              "╚═══════════════════════════════════════════╝"));
            break;
        case '<':
            audio.player.previous();
            Serial.println(F( "╔══════════════════ TRACK ══════════════════╗\n"
                              "║            ◀◀ Previous Track              ║\n"
                              "╚═══════════════════════════════════════════╝"));
            break;
        case 's':
            if (audio.player.isActive()) {
                audio.player.stop();
                Serial.println(F( "╔══════════════════ PLAYER ═════════════════╗\n"
                                  "║                   STOPPED                 ║\n"
                                  "╚═══════════════════════════════════════════╝"));
            } else {
                audio.player.play();
                Serial.println(F( "╔══════════════════ PLAYER ═════════════════╗\n"
                                  "║               ▶ NOW PLAYING               ║\n"
                                  "╚═══════════════════════════════════════════╝"));
            }
            break;
        case '+':
            current_volume = min(1.0f, current_volume + 0.05f);
            audio.player.setVolume(current_volume);
            display.show_volume((int)(current_volume * 100));
            Serial.printf("Volume Up → %d%%\n", (int)(current_volume * 100));
            break;
        case '-':
            current_volume = max(0.0f, current_volume - 0.05f);
            audio.player.setVolume(current_volume);
            display.show_volume((int)(current_volume * 100));
            Serial.printf("Volume Down → %d%%\n", (int)(current_volume * 100));
            break;
    }
}

void handle_display_command(char cmd){
    switch(cmd){
        case '(':
            current_brightness = max(0, current_brightness - 10);
            TFT_SET_BL(current_brightness);
            break;
        case ')':
            current_brightness = min(100, current_brightness + 10);
            TFT_SET_BL(current_brightness);
            break; 
    }
}

void setup() {
    Serial.begin(115200);
    setCpuFrequencyMhz(240);

    Serial.println(F("╔══════════════════ おかえり~~~ :3 ═══════════════════╗"));

    if (!audio.begin())    { Serial.println("Audio init failed");    while(1); }
    if (!display.begin())  { Serial.println("Display init failed");  while(1); }
    if (!boot_sdcard())    { Serial.println("SD card init failed");  while(1); }

    load_directory(current_directory);

    Serial.println(F("╚════════════════════════════════════════════════════╝"));
    display.display_png(StaticBg, sizeof(StaticBg));
    TFT_SET_BL(current_brightness);

    show_resource_monitor();
    show_help_menu();

    if (!audio.player.begin()) {
        Serial.println("Failed to start player");
        while(1);
    }
    audio.player.setVolume(current_volume);
    audio.player.setAutoNext(true);

}

void loop() {
    audio.player.copy();

    char cmd = Serial.read();
    handle_audio_command(cmd);
    handle_directory_command(cmd);
    handle_display_command(cmd);

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