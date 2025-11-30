#define ESP32
#define ESP32X

#include "uta_Audio.h"
#include "uta_Display.h"
#include "uta_SDCard.h"
#include "StaticBg.h"

DisplayManager display;
AudioManager audio;

constexpr const char* MUSIC_ROOT = "/Music/Aitsuki Nakuru/";

const String DIRECTORIES[] = {
    // ... your folders here
};

uint8_t current_dir_index = 5;
String current_directory = DIRECTORIES[current_dir_index];
float current_volume = 0.2f;

constexpr int BUFFER_SIZE = 1024;
constexpr unsigned long PROGRESS_UPDATE_INTERVAL_MS = 1000;

FsFile dir;
NamePrinter name_printer(audio.source);

unsigned long last_progress_update_ms = 0;
float last_reported_time_s = -1.0f;

// ======================== Helper Functions ========================

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

    // Align label
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
    Serial.println(F("║              ESP32-S3 Resource Monitor • BAR VIEW           ║"));
    Serial.println(F("╚══════════════════════════════════════════════════════════════╝\n"));

    // System Info
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

    // CPU Load (approximated via idle task stack)
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
    Serial.println(F("║               ESP32-S3 Music Player Dashboard                ║"));
    Serial.println(F("╚══════════════════════════════════════════════════════════════╝\n"));
    Serial.println(F("  Directory Control"));
    Serial.println(F("   [r]  Next Folder      →"));
    Serial.printf("     Current: %s\n", current_directory.c_str());
    Serial.println(F("   [R]  Previous Folder"));
    Serial.println(F("   [v]  View Current Queue"));
    Serial.println(F("   [V]  View Root (/Music/Aitsuki Nakuru)"));
    Serial.println();
    Serial.println(F("  Audio Control"));
    Serial.println(F("   [n]  Next Track        [p]  Previous Track"));
    Serial.println(F("   [s]  Play / Stop       [+]  Volume Up    [-]  Volume Down"));
    Serial.printf("   Volume: %d%%\n", (int)(current_volume * 100));
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

// ======================== Directory Management ========================

void load_directory(const String& path) {
    audio.player.stop();
    audio.player.setIndex(0);
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
    dir.ls(&name_printer, O_RDONLY);
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
        Serial.println(F("Failed to initialize player → skipping to next folder"));
        change_to_next_directory();  // Recursive skip on failure
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
            sd.ls(MUSIC_ROOT, LS_SIZE);
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
        case 'h': case 'H': case '?':
            show_help_menu();
            break;
    }
}

void handle_audio_command(char cmd) {
    switch (cmd) {
        case 'n':
            audio.player.next();
            Serial.println(F( "╔══════════════════ TRACK ══════════════════╗\n"
                              "║               ▶▶ Next Track               ║\n"
                              "╚═══════════════════════════════════════════╝"));
            break;
        case 'p':
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
            Serial.printf("\033[1m Volume Up → %d%%\033[0m\n", (int)(current_volume * 100));
            break;
        case '-':
            current_volume = max(0.0f, current_volume - 0.05f);
            audio.player.setVolume(current_volume);
            display.show_volume((int)(current_volume * 100));
            Serial.printf("\033[1m Volume Down → %d%%\033[0m\n", (int)(current_volume * 100));
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

    show_resource_monitor();
    show_help_menu();

    if (!audio.player.begin()) {
        Serial.println("Failed to start player");
        while(1);
    }
    audio.player.setAutoNext(true);
}

void loop() {
    audio.player.copy();

    if (Serial.available()) {
        char cmd = Serial.read();
        handle_audio_command(cmd);
        handle_directory_command(cmd);
    }

    // Progress bar update
    unsigned long now = millis();
    float current_time = audio.getAudioCurrentTime();

    if (audio.player.isActive() &&
        (now - last_progress_update_ms >= PROGRESS_UPDATE_INTERVAL_MS ||
         fabsf(current_time - last_reported_time_s) >= 1.0f)) {

        display.update_progress(current_time, audio.getAudioFileDuration());
        last_progress_update_ms = now;
        last_reported_time_s = current_time;
    }
}