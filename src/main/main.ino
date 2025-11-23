#define ESP32
#define ESP32X

#include "Audio.h"
#include "Display.h"
#include "SDCard.h"

#include "StaticBg.h"

const char* path = "/Music/Aitsuki Nakuru/[M3-47] 藍月なくる - Transpain {NRCD-06} [CD-FLAC]/";
const char* root = "/Music/Aitsuki Nakuru/";

void initial_setup(){
  player.setVolume(0.2);

  NamePrinter namePrinter(source, path);
  auto dir = sd.open(path, O_READ);
  dir.ls(&namePrinter, 0);
  dir.close();
}

void setup() {
  // put your setup code here, to run once:
  Serial.begin(115200);

  if (!boot_i2s())    stop();
  if (!boot_sdcard()) stop();
  if (!boot_screen()) stop();

  Serial.println("\n\n[================ Welcome Back, User! ================]");
  initial_setup();

  Serial.print("[INFO] : Loading image ");
  Serial.println(VAR_NAME(StaticBg));
  display_png(StaticBg, sizeof(StaticBg));

  Serial.print("Currently in directory: ");
  Serial.println(path);
  sd.ls(path, LS_SIZE);

  Serial.println("================================");
  Serial.println(root);
  sd.ls(root, LS_SIZE);
  if (!player.begin()) {
    Serial.println("Failed to start player");
    stop();
  }
  player.next();player.next();player.next();
}

void loop() {
  player.copy();
}
