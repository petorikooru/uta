#include "Keypad.h"

#include "music.h"
#include "StaticBg.h"

DisplayManager  display;
AudioManager    audio;

uint8_t current_dir_index   = 0;
String  current_directory   = DIRECTORIES[current_dir_index];
float   current_volume      = 0.2f;
uint8_t current_brightness  = 50;
bool    screen_off          = false;

constexpr uint16_t PROGRESS_UPDATE_INTERVAL_MS = 1000;

FsFile dir;
NamePrinter name_printer(audio.source);

uint16_t    last_progress_update_ms = 0;
float       last_reported_time_s = -1.0f;

void draw_bar(const char* label, uint32_t used, uint32_t total, const char* unit = "B") {
    if (total == 0) return;

    int percent = (used * 100) / total;
    int filled  = (percent * 40) / 100;

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

void draw_bar(const char* label, int percent) {
    int filled = (percent * 40) / 100;
    Serial.printf(" %s%*s: %3d%% [", label, 14 - (int)strlen(label), "", percent);
    for (int i = 0; i < 40; ++i) {
        Serial.print(i < filled ? "█" : "░");
    }
    Serial.println("]");
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

void load_next_directory() {
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

void load_previous_directory() {
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

void volume_up(){
    current_volume = min(1.0f, current_volume + 0.05f);
    audio.player.setVolume(current_volume);
    display.show_volume((int)(current_volume * 100));
    Serial.printf("Volume Up → %d%%\n", (int)(current_volume * 100));
}

void volume_down(){
    current_volume = max(0.0f, current_volume - 0.05f);
    audio.player.setVolume(current_volume);
    display.show_volume((int)(current_volume * 100));
    Serial.printf("Volume Down → %d%%\n", (int)(current_volume * 100));
}

void display_toggle(){
    display.set_brightness(screen_off ? current_brightness : 0);
    screen_off != screen_off;
}

void brightness_up(){
    current_brightness = min(100, current_brightness + 10);
    display.set_brightness(current_brightness);
}

void brightness_down(){
    current_brightness = max(0, current_brightness - 10);
    display.set_brightness(current_brightness);
}

void audio_toggle(){
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
}

void audio_next(){
    audio.player.next();
    Serial.println(F( "╔══════════════════ TRACK ══════════════════╗\n"
                      "║               ▶▶ Next Track               ║\n"
                      "╚═══════════════════════════════════════════╝"));
}

void audio_previous(){
    audio.player.previous();
    Serial.println(F( "╔══════════════════ TRACK ══════════════════╗\n"
                      "║            ▶▶ Previous Track              ║\n"
                      "╚═══════════════════════════════════════════╝"));
}

void view_queue(){
    Serial.println();
    Serial.println(F( "╔══════════════════ CURRENT QUEUE ═══════════════════╗"));
    Serial.printf(    "║  Directory: %s\n", current_directory.c_str());
    Serial.println(F( "╟────────────────────────────────────────────────────╢"));
    sd.ls(current_directory.c_str(), LS_SIZE);
    Serial.println(F( "╚════════════════════════════════════════════════════╝"));
}

void view_root_directory(){
    Serial.println();
    Serial.println(F("╔═══════════════════ ROOT DIRECTORY ═════════════════╗"));
    sd.ls(ROOT, LS_SIZE);
    Serial.println(F("╚════════════════════════════════════════════════════╝"));
}

void view_help(){
    Serial.println();
    Serial.println(F("╔══════════════════════════════════════════════════════════════╗"));
    Serial.println(F("║                              :3                              ║"));
    Serial.println(F("╚══════════════════════════════════════════════════════════════╝"));
    Serial.println(F("  Directory Control                                             "));
    Serial.println(F("   [r]  Next Folder      →                                      "));
    Serial.printf(   "     Current: %-49s", current_directory.c_str());
    Serial.println(F("   [R]  Previous Folder                                         "));
    Serial.println(F("   [v]  View Current Queue                                      "));
    Serial.printf((  "   [V]  View Root (%s)                                          "), ROOT);
    Serial.println();
    Serial.println(F("  Audio Control                                                 "));
    Serial.println(F("   [>]  Next Track        [<]  Previous Track                   "));
    Serial.println(F("   [p]  Play / Stop       [+]  Volume Up    [-]  Volume Down    "));
    Serial.printf(   "   Volume: %d%%\n                                               ", (int)(current_volume * 100));
    Serial.println();
    Serial.println(F("  System                                                        "));
    Serial.println(F("   [e]  Resource Monitor                                        "));
    Serial.println(F("   [x]  Restart ESP32                                           "));
    Serial.println(F("   [h]  Show this help                                          "));
    Serial.println();
    Serial.printf(   " Uptime: %lus  |  Free Heap: %lu KB  |  Tasks: %d\n",
                     millis() / 1000, ESP.getFreeHeap() / 1024, uxTaskGetNumberOfTasks());
    Serial.println(F("╚══════════════════════════════════════════════════════════════╝\n"));
}

void view_resources() {
    Serial.println();
    Serial.println(F("╔══════════════════════════════════════════════════════════════╗"));
    Serial.println(F("║                              :3                              ║"));
    Serial.println(F("╚══════════════════════════════════════════════════════════════╝\n"));

    Serial.printf(   " Chip      : %s Rev %d | Cores: 2 | Freq: %d MHz                \n", ESP.getChipModel(), ESP.getChipRevision(), ESP.getCpuFreqMHz());
    Serial.printf(   " Flash     : %d MB %s", ESP.getFlashChipSize() / 1024 / 1024,
                  [&](uint8_t mode) -> const char* {
                    switch (mode) {
                        case FM_QIO:  return "QIO";
                        case FM_QOUT: return "QOUT";
                        case FM_DIO:  return "DIO";
                        case FM_DOUT: return "DOUT";
                        default:      return "Unknown";
                    }
                  }(ESP.getFlashChipMode()));
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
        draw_bar("", load_percent);
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

void system_reboot(){
    Serial.println(F("╔════════════════════ SYSTEM ════════════════════════╗"));
    Serial.println(F("              Restarting ESP32 in 3...                ")); delay(1000);
    Serial.println(F("              Restarting ESP32 in 2...                ")); delay(1000);
    Serial.println(F("              Restarting ESP32 in 1...                ")); delay(1000);
    ESP.restart();
}

void system_poweroff(){
    Serial.println(F("╔════════════════════ SYSTEM ════════════════════════╗"));
    Serial.println(F("              Shutting Down ESP32 in 3...             ")); delay(1000);
    Serial.println(F("              Shutting Down ESP32 in 2...             ")); delay(1000);
    Serial.println(F("              Shutting Down ESP32 in 1...             ")); delay(1000);
    esp_deep_sleep_start(); 
}

// ======================================================================== //
// =========================== Serial Handler ============================= //
// ======================================================================== //
void handle_serial() {
    char cmd = Serial.read();

    switch (cmd) {

        ////////////////////////////////////////////////////////////////////
        //                        Directory Command                       //
        ////////////////////////////////////////////////////////////////////
        case 'r': load_next_directory();      break;
        case 'R': load_previous_directory();  break;
        case 'v': view_queue();               break;
        case 'V': view_root_directory();      break;

        ////////////////////////////////////////////////////////////////////
        //                          System Command                        //
        ////////////////////////////////////////////////////////////////////
        case 'e': view_resources();                 break;
        case 'x': system_reboot();                  break;
        case 'X': system_poweroff();                break;
        case 'h': case 'H': case '?': view_help();  break;

        ////////////////////////////////////////////////////////////////////
        //                          Player Command                        //
        ////////////////////////////////////////////////////////////////////
        case '>': audio_next();     break;
        case '<': audio_previous(); break;
        case 'p': audio_toggle();   break;
        case '+': volume_up();      break;
        case '-': volume_down();    break;

        ////////////////////////////////////////////////////////////////////
        //                         Display Command                        //
        ////////////////////////////////////////////////////////////////////
        case '(': brightness_down();  break;
        case ')': brightness_up();    break;
        case 's': display_toggle();   break;
    }
}

// ======================================================================== //
// ============================ Touch Handler ============================= //
// ======================================================================== //
FT6236 ts = FT6236();

constexpr uint8_t   TAP_THRESHOLD   = 15;
constexpr uint8_t   DIRECTION_LOCK  = 10;
constexpr uint16_t  HOLD_TIME_MS = 1000;

constexpr uint16_t  SCREEN_WIDTH = 320;
constexpr uint16_t  LEFT_ZONE_END    = SCREEN_WIDTH / 3;
constexpr uint16_t  RIGHT_ZONE_START = (SCREEN_WIDTH * 2) / 3;

enum class SwipeDirection {
    None,
    Left,
    Right,
    Up,
    Down,
};

bool is_touching      = false;
bool direction_locked = false;
bool hold_triggered   = false;

int16_t start_x = 0;
int16_t start_y = 0;
int16_t last_x  = 0;
int16_t last_y  = 0;

SwipeDirection locked_dir = SwipeDirection::None;
uint16_t hold_start_time = 0;

void trigger_swipe(SwipeDirection dir) {
    switch (dir) {
        case SwipeDirection::Left:  audio.player.next();      break;
        case SwipeDirection::Right: audio.player.previous();  break;
        case SwipeDirection::Up:    volume_up();              break;
        case SwipeDirection::Down:  volume_down();            break;
    }
}

void trigger_swipe_hold(SwipeDirection dir) {
    switch (dir) {
        case SwipeDirection::Left:  load_next_directory();     break;
        case SwipeDirection::Right: load_previous_directory(); break;
        case SwipeDirection::Up:    brightness_up();           break;
        case SwipeDirection::Down:  brightness_down();         break;
    }
}

void handle_touch() {
    if (ts.touched()) {
        TS_Point p = ts.getPoint();

        if (!is_touching) {
            start_x = last_x = p.x;
            start_y = last_y = p.y;

            is_touching      = true;
            direction_locked = false;
            hold_triggered   = false;
            locked_dir       = SwipeDirection::None;
        } 
        else {
            last_x = p.x;
            last_y = p.y;

            int dx = last_x - start_x;
            int dy = last_y - start_y;

            if (!direction_locked &&
                (abs(dx) > DIRECTION_LOCK || abs(dy) > DIRECTION_LOCK)) {

                direction_locked = true;
                hold_start_time = millis();

                if (abs(dx) > abs(dy)) {
                    locked_dir = (dx > 0)
                        ? SwipeDirection::Right
                        : SwipeDirection::Left;
                } else {
                    locked_dir = (dy > 0)
                        ? SwipeDirection::Down
                        : SwipeDirection::Up;
                }
            }

            // Hold?
            if (direction_locked && !hold_triggered) {
                if (millis() - hold_start_time >= HOLD_TIME_MS) {
                    trigger_swipe_hold(locked_dir);
                    hold_triggered = true;
                }
            }
        }
    }
    else if (is_touching) {
        int dx = last_x - start_x;
        int dy = last_y - start_y;

        // Tap
        if (!direction_locked &&
            abs(dx) < TAP_THRESHOLD &&
            abs(dy) < TAP_THRESHOLD) {

            if (start_x < LEFT_ZONE_END) {
                audio.player.previous();
            }
            else if (start_x > RIGHT_ZONE_START) {
                audio.player.next();
            }
            else {
                audio_toggle();   // CENTER TAP
            }
        }
        // Swipe
        else if (direction_locked && !hold_triggered) {
            trigger_swipe(locked_dir);
        }

        is_touching = false;
    }
}

// ======================================================================== //
// ============================ Keypad Handler ============================ //
// ======================================================================== //

constexpr uint8_t ROWS = 3;
constexpr uint8_t COLS = 3;
char keys[ROWS][COLS] = {
    {'>', 'p', '<'},
    {'+', '-', 'x'},
    {')', '(', 's'}
};
uint8_t pin_rows[ROWS] = {4, 5, 14};
uint8_t pin_cols[COLS] = {47, 48, 13};

Keypad keypad = Keypad(makeKeymap(keys), pin_rows, pin_cols, ROWS, COLS); 

void handle_keypad(){
  char key = keypad.getKey();

  if (key){
     switch(key){
      
        ////////////////////////////////////////////////////////////////////
        //                          System Command                        //
        ////////////////////////////////////////////////////////////////////
        case 'x': system_reboot(); break;

        ////////////////////////////////////////////////////////////////////
        //                          Player Command                        //
        ////////////////////////////////////////////////////////////////////
        case '>': audio_next();     break;
        case '<': audio_previous(); break;
        case 'p': audio_toggle();   break;
        case '+': volume_up();      break;
        case '-': volume_down();    break;

        ////////////////////////////////////////////////////////////////////
        //                         Display Command                        //
        ////////////////////////////////////////////////////////////////////
        case '(': brightness_down();  break;
        case ')': brightness_up();    break;
        case 's': display_toggle();   break;
     }
  }
}