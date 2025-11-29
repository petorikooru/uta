#define ESP32
#define ESP32X

#include "uta_Audio.h"
#include "uta_Display.h"
#include "uta_SDCard.h"

#include "StaticBg.h"

DisplayManager display;

AudioManager audio;

// const char *root = "/Music/Arcaea Sound Collection/";
// const String directories[] = {
//     "/Music/Arcaea Sound Collection/[IROCD-001] Arcaea Sound Collection -Memories of Light- [M3-44]/",
//     "/Music/Arcaea Sound Collection/[IROCD-002] Arcaea Sound Collection -Memories of Conflict- [M3-44]/",
//     "/Music/Arcaea Sound Collection/[IROCD-003] Arcaea Sound Collection -Memories of Realms- [M3-46]/",
//     "/Music/Arcaea Sound Collection/[IROCD-004] Arcaea Sound Collection -Memories of Dreams-/",
//     "/Music/Arcaea Sound Collection/[IROCD-005] Arcaea Sound Collection -Memories of Wrath-/",
//     "/Music/Arcaea Sound Collection/[IROCD-006] Arcaea Sound Collection -Memories of Myth-/",
//     "/Music/Arcaea Sound Collection/[IROCD-007] Arcaea Sound Collection -Memory of Serenity-/",
// };

const char* root = "/Music/Aitsuki Nakuru/";
const String directories[] = {
  "/Music/Aitsuki Nakuru/[2023.01.13] 藍月なくる － 逆沙華 [WEB-FLAC]/",
  "/Music/Aitsuki Nakuru/Fragile Utopia/",
  "/Music/Aitsuki Nakuru/Nakuru Aitsuki 7th Album - Indigrotto (24bit)/",
  "/Music/Aitsuki Nakuru/[M3-51] 藍月なくる - ミシュメリア {HFMM-02} [CD-FLAC]/",
  "/Music/Aitsuki Nakuru/Lucid Hallucination/",
  "/Music/Aitsuki Nakuru/Nacollection4/",
  "/Music/Aitsuki Nakuru/[DVSP-0196] Feryquitous x 藍月なくる - IdenTism (M3-41)/",
  "/Music/Aitsuki Nakuru/[2024.05.22] 藍月なくる×棗いつき 2ndコラボシングル「約束のリンカネーション」[FLAC 48kHz／24bit]/",
  "/Music/Aitsuki Nakuru/Nacollection-3-/",
  "/Music/Aitsuki Nakuru/藍月なくる - ご注文はえいえんに/",
  "/Music/Aitsuki Nakuru/2023.10.18 [GECL-00001] 藍月なくる×棗いつき — 追想のラグナロク【藍月なくる盤】/",
  "/Music/Aitsuki Nakuru/[2024.12.18] 藍月なくる 1stシングル「何も知らないまま。」[FLAC 48kHz／24bit]/",
  "/Music/Aitsuki Nakuru/JelLaboratory/",
  "/Music/Aitsuki Nakuru/[M3-38] Nakuru Aitsuki (藍月なくる) — アプルフィリアの秘め事 [FLAC]/",
  "/Music/Aitsuki Nakuru/Eufolie/",
  "/Music/Aitsuki Nakuru/[2021-01-26] Feryquitous feat. 藍月なくる - Evil Bubble [FLAC]/",
  "/Music/Aitsuki Nakuru/(M3-40) 藍月なくる x Sennzai - soleil de minuit [FLAC]/",
  "/Music/Aitsuki Nakuru/[M3-47] 藍月なくる - Transpain {NRCD-06} [CD-FLAC]/",
  "/Music/Aitsuki Nakuru/藍月なくる - Counterfeit [FLAC]/",
  "/Music/Aitsuki Nakuru/[2023.10.18]藍月なくる & 棗いつき ／ 追想のラグナロク [棗いつき盤][CD-FLAC]/",
  "/Music/Aitsuki Nakuru/{NACV-0001} クラリムステラ - 藍月なくる Cover Collection vol.1 [CD-FLAC]/",
  "/Music/Aitsuki Nakuru/藍月なくる - Dear The Night I Loved/",
  "/Music/Aitsuki Nakuru/藍月なくる x まめこ - Baby Romantica [CD FLAC]/",
  "/Music/Aitsuki Nakuru/(C95) 諸富智紗姫 starring 藍月なくる - STARRY ARTET HELLO WORLD 05 CHISAKI MORODOMI {PROJECT ARTET, CCSA-0005} [CD-FLAC]/",
  "/Music/Aitsuki Nakuru/Singles/",
  "/Music/Aitsuki Nakuru/Nacollection!/",
  "/Music/Aitsuki Nakuru/[M3-55] 藍月なくる - わたしとキミの幸せな終末 {NRCD-09} [CD-FLAC]/",
};

uint8_t directory_index = 5;
String current_directory = directories[directory_index];
float current_volume = 0.2;
const int buffer_size = 1024;
char* stats_buffer;

FsFile dir;
NamePrinter name_printer(audio.source);

unsigned long last_progress_update = 0;
const unsigned long progress_update_interval = 1000;
float last_current_time = -1.0f;

void initial_setup() {
  audio.player.setVolume(current_volume);
  dir = sd.open(current_directory.c_str(), O_READ);
  if (!dir) {
    Serial.println("[ERROR] Failed to open directory");
    return;
  }
  name_printer.setPrefix(current_directory.c_str());
  dir.ls(&name_printer, O_RDONLY);
  dir.close();
}

void change_directory(String new_directory) {
  audio.player.stop();
  audio.player.setIndex(0);
  Serial.printf("[INFO] Changing directory to: %s\n", new_directory.c_str());
  current_directory = new_directory;

  name_printer.flush();
  audio.source.clear();

  dir = sd.open(current_directory.c_str(), O_READ);
  if (!dir) {
    Serial.println("[ERROR] Failed to open new directory");
    return;
  }
  name_printer.setPrefix(current_directory.c_str());
  dir.ls(&name_printer, O_RDONLY);
  dir.close();
}

void not_htop() {

  Serial.println();
  Serial.println(F("╔══════════════════════════════════════════════════════════════╗"));
  Serial.println(F("║              ESP32-S3 Resource Monitor • BAR VIEW           ║"));
  Serial.println(F("╚══════════════════════════════════════════════════════════════╝\n"));

  auto drawBar = [](const char* label, uint32_t value, uint32_t total, const char* unit = "B") {
    if (total == 0) return;
    int percent = (value * 100) / total;
    int bars = (percent * 40) / 100;

    Serial.printf(" %s%s: ", label, strlen(label) < 12 ? "           " + (12 - strlen(label)) : "");
    Serial.printf("%6lu / %6lu %s", value, total, unit);
    Serial.printf("\t\n(%3d%%) [", percent);

    for (int i = 0; i < 40; i++) {
      if (i < bars) {
        if (percent > 90) Serial.print("█");       // red zone
        else if (percent > 70) Serial.print("▓");  // orange
        else Serial.print("▓");                    // green
      } else {
        Serial.print("░");
      }
    }
    Serial.println("]");
  };

  auto drawSimpleBar = [](const char* label, int percent, const char* color = "") {
    int bars = (percent * 40) / 100;
    Serial.printf(" %s%s: %3d%% [", label, strlen(label) < 14 ? "             " + (14 - strlen(label)) : "", percent);
    for (int i = 0; i < 40; i++) {
      if (i < bars) Serial.print("█");
      else Serial.print("░");
    }
    Serial.println("]");
  };

  // --- System Info ---
  Serial.printf(" Chip      : %s Rev %d | Cores: 2 | Freq: %d MHz\n",
                ESP.getChipModel(), ESP.getChipRevision(), ESP.getCpuFreqMHz());
  Serial.printf(" Flash     : %d MB %s", ESP.getFlashChipSize() / 1024 / 1024,
                flashModeToString(ESP.getFlashChipMode()));
  Serial.printf(" | PSRAM: %s", psramFound() ? "Yes" : "No ");
  if (psramFound()) Serial.printf("(%d MB)", ESP.getPsramSize() / 1024 / 1024);
  Serial.printf(" | Temp: %.1f°C\n", temperatureRead());
  Serial.printf(" Uptime    : %lu seconds\n\n", millis() / 1000);

  // --- Memory Bars ---
  Serial.println(F("╔══════════════════════════ MEMORY ═══════════════════════════╗"));

  uint32_t totalHeap = ESP.getHeapSize();
  uint32_t freeHeap = ESP.getFreeHeap();
  drawBar("HEAP\t", freeHeap, totalHeap);

  if (psramFound()) {
    drawBar("PSRAM", ESP.getFreePsram(), ESP.getPsramSize());
  }

  uint32_t usedSketch = ESP.getSketchSize();
  uint32_t totalSketch = usedSketch + ESP.getFreeSketchSpace();
  drawBar("SKETCH", usedSketch, totalSketch);

  Serial.println(F("╚══════════════════════════════════════════════════════════════╝\n"));


  Serial.println(F("╔══════════════════════════ CPU LOAD ══════════════════════════╗"));
  for (int core = 0; core < 2; core++) {
    TaskHandle_t idle = xTaskGetIdleTaskHandleForCPU(core);
    UBaseType_t hwm = idle ? uxTaskGetStackHighWaterMark(idle) : 0;

    int load = 0;
    if (hwm > 800) load = 5;
    else if (hwm > 500) load = 20;
    else if (hwm > 300) load = 50;
    else if (hwm > 150) load = 80;
    else load = 95;

    Serial.printf(" CORE %d    ", core);
    drawSimpleBar("", load);
  }
  Serial.println(F("╚══════════════════════════════════════════════════════════════╝\n"));

  Serial.println(F("╔══════════════════════════ TASKS ═════════════════════════════╗"));
  int totalTasks = uxTaskGetNumberOfTasks();
  Serial.printf(" Total Running Tasks : %d\n", totalTasks);

  char* buf = (char*)malloc(2048);
  if (buf) {
    vTaskList(buf);
    Serial.println(F(" Active Tasks:"));
    char* line = strtok(buf, "\n");
    int count = 0;
    while (line && count < 6) {
      char name[20] = "", state = ' ', core = '-';
      unsigned prio = 0, stack = 0;
      sscanf(line, "%s %c %u %u %c", name, &state, &prio, &stack, &core);
      char st = (state == 'R') ? '●' : (state == 'B') ? '○'
                                                      : '■';
      Serial.printf("  %c %-16s (Prio %d, Core %c, Stack %u)\n", st, name, prio, core, stack);
      line = strtok(NULL, "\n");
      count++;
    }
    free(buf);
  }
  Serial.println(F("╚══════════════════════════════════════════════════════════════╝"));

  Serial.printf("\n Refreshed at %lus • Type 'e' for update!\n\n", millis() / 1000);
}

const char* flashModeToString(uint8_t mode) {
  switch (mode) {
    case FM_QIO: return "QIO";
    case FM_QOUT: return "QOUT";
    case FM_DIO: return "DIO";
    case FM_DOUT: return "DOUT";
    default: return "Unknown";
  }
}

void halp_menu() {
  Serial.println();
  Serial.println(F("╔══════════════════════════════════════════════════════════════╗"));
  Serial.println(F("║               ESP32-S3 Music Player Dashboard                ║"));
  Serial.println(F("╚══════════════════════════════════════════════════════════════╝"));
  Serial.println(F("  Directory Control"));
  Serial.println(F("   [r]  Next Folder      → "));
  Serial.printf(" %s\n", current_directory.c_str());
  Serial.println(F("   [R]  Previous Folder"));
  Serial.println(F("   [v]  View Current Queue"));
  Serial.println(F("   [V]  View /Music/Aitsuki Nakuru"));
  Serial.println();
  Serial.println(F("  Audio Control"));
  Serial.println(F("   [n]  Next Track        [p]  Previous Track"));
  Serial.println(F("   [s]  Play / Stop       [+]  Volume Up    [-]  Volume Down"));
  Serial.println(F("   Current Volume: "));
  Serial.printf(" %d%%\n", (int)(current_volume * 100));
  Serial.println();
  Serial.println(F("  System"));
  Serial.println(F("   [e]  Resource Monitor (htop-style)"));
  Serial.println(F("   [x]  Restart ESP32"));
  Serial.println(F("   [h]  Show this help"));
  Serial.println();
  Serial.printf(" Uptime: %lus  |  Free Heap: %lu KB  |  Tasks: %d\n",
                millis() / 1000, ESP.getFreeHeap() / 1024, uxTaskGetNumberOfTasks());
  Serial.println(F("╚══════════════════════════════════════════════════════════════╝\n"));
}

void directory_control_begin(char incoming_char) {
  const int dir_count = sizeof(directories) / sizeof(directories[0]);

  switch (incoming_char) {
    case 'r':
      {
        directory_index = (directory_index + 1) % dir_count;
        change_directory(directories[directory_index]);

        Serial.println();
        Serial.println(F("╔══════════════════ FOLDER CHANGED ══════════════════╗"));
        Serial.printf("║  → Now Playing From: %-30s ║\n", current_directory.c_str());
        Serial.println(F("╚════════════════════════════════════════════════════╝"));
        sd.ls(current_directory.c_str(), LS_R | LS_SIZE);

        if (!audio.player.begin()) {
          Serial.println(F("Failed to initialize player in this folder!"));
          Serial.println(F("Trying next folder..."));
          directory_index = (directory_index + 1) % dir_count;
          change_directory(directories[directory_index]);
        }
        break;
      }

    case 'R':
      {
        directory_index = (directory_index == 0) ? dir_count - 1 : directory_index - 1;
        change_directory(directories[directory_index]);

        Serial.println();
        Serial.println(F("╔══════════════════ FOLDER CHANGED ══════════════════╗"));
        Serial.printf("║  ← Now Playing From: %-30s ║\n", current_directory.c_str());
        Serial.println(F("╚════════════════════════════════════════════════════╝"));
        sd.ls(current_directory.c_str(), LS_R | LS_SIZE);

        if (!audio.player.begin()) {
          Serial.println(F("Failed to initialize player!"));
        }
        break;
      }

    case 'v':
      Serial.println();
      Serial.println(F("╔══════════════════ CURRENT QUEUE ═══════════════════╗"));
      Serial.printf("║  Directory: %s\n", current_directory.c_str());
      Serial.println(F("╟────────────────────────────────────────────────────╢"));
      sd.ls(current_directory.c_str(), LS_SIZE);
      Serial.println(F("╚════════════════════════════════════════════════════╝"));
      break;

    case 'V':
      Serial.println();
      Serial.println(F("╔═══════════════════ ROOT DIRECTORY ═════════════════╗"));
      sd.ls(root, LS_SIZE);
      Serial.println(F("╚════════════════════════════════════════════════════╝"));
      break;

    case 'e':
      not_htop();
      break;

    case 'x':
      Serial.println();
      Serial.println(F("╔════════════════════ SYSTEM ════════════════════════╗"));
      Serial.println(F("║                Restarting ESP32 in 3...            ║"));
      delay(1000);
      Serial.println(F("║                Restarting ESP32 in 2...            ║"));
      delay(1000);
      Serial.println(F("║                Restarting ESP32 in 1...            ║"));
      delay(1000);
      Serial.println(F("╚════════════════════════════════════════════════════╝"));
      ESP.restart();
      break;

    case 'h':
    case 'H':
    case '?':
      halp_menu();
      break;

    default:
      break;
  }
}

void audio_control_begin(char incoming_char) {
  switch (incoming_char) {
    case 'n':
      audio.player.next();
      Serial.println();
      Serial.println(F("╔══════════════════ TRACK ══════════════════╗"));
      Serial.println(F("║               ▶▶ Next Track               ║"));
      Serial.println(F("╚═══════════════════════════════════════════╝"));
      break;

    case 'p':
      audio.player.previous();
      Serial.println();
      Serial.println(F("╔══════════════════ TRACK ══════════════════╗"));
      Serial.println(F("║               ◀◀ Previous Track           ║"));
      Serial.println(F("╚═══════════════════════════════════════════╝"));
      break;

    case 's':
      if (audio.player.isActive()) {
        audio.player.stop();
        Serial.println();
        Serial.println(F("╔══════════════════ PLAYER ═════════════════╗"));
        Serial.println(F("║                   STOPPED                 ║"));
        Serial.println(F("╚═══════════════════════════════════════════╝"));
      } else {
        audio.player.play();
        Serial.println();
        Serial.println(F("╔══════════════════ PLAYER ═════════════════╗"));
        Serial.println(F("║                 ▶ NOW PLAYING             ║"));
        Serial.println(F("╚═══════════════════════════════════════════╝"));
      }
      break;

    case '+':
      current_volume = min(1.0f, current_volume + 0.05f);
      audio.player.setVolume(current_volume);
      display.show_volume((int)(current_volume * 100));
      Serial.printf("\033[1m Volume Up: %d%%\033[0m\n", (int)(current_volume * 100));
      break;

    case '-':
      current_volume = max(0.0f, current_volume - 0.05f);
      audio.player.setVolume(current_volume);
      display.show_volume((int)(current_volume * 100));
      Serial.printf("\033[1m Volume Down: %d%%\033[0m\n", (int)(current_volume * 100));
      break;

    default:
      break;
  }
}

void setup() {
  Serial.begin(115200);
  setCpuFrequencyMhz(240);

  Serial.println(F("╔══════════════════ おかえり~~~ :3 ═══════════════════╗"));
  if (!audio.begin()) stop();
  if (!display.begin()) stop();
  if (!boot_sdcard()) stop();
  initial_setup();
  Serial.println("Loading image StaticBg...");
  display.display_png(StaticBg, sizeof(StaticBg));
  Serial.println(F("╚════════════════════════════════════════════════════╝"));
    
  not_htop();

  Serial.println();
  Serial.println(F("╔══════════════════ CURRENT QUEUE ═══════════════════╗"));
  Serial.printf("║  Directory: %s\n", current_directory.c_str());
  Serial.println(F("╟────────────────────────────────────────────────────╢"));
  sd.ls(current_directory.c_str(), LS_SIZE);
  Serial.println(F("╚════════════════════════════════════════════════════╝"));

  Serial.println();
  Serial.println(F("╔═══════════════════ ROOT DIRECTORY ═════════════════╗"));
  sd.ls(root, LS_SIZE);
  Serial.println("Path: " + current_directory);
  Serial.println(F("╚════════════════════════════════════════════════════╝"));

  if (!audio.player.begin()) {
    Serial.println("Failed to start player");
    stop();
  }
  audio.player.setAutoNext(true);
}

void loop() {
  audio.player.copy();
  char incoming_char = Serial.read();
  audio_control_begin(incoming_char);
  directory_control_begin(incoming_char);

  unsigned long current_millis = millis();
  float current_time = audio.getAudioCurrentTime();
  if (audio.player.isActive() && (current_millis - last_progress_update >= progress_update_interval || abs(current_time - last_current_time) >= 1.0f)) {
    display.update_progress(current_time, audio.getAudioFileDuration());
    last_progress_update = current_millis;
    last_current_time = current_time;
  }
}