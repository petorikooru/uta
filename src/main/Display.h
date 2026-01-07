
#pragma once

#include <SPI.h>
#include <TFT_eSPI.h>
#include <PNGdec.h>

#include "BootBg.h"
#include "NotoSansBold15.h"

#define VAR_NAME(var) #var

#define MAX_IMAGE_WIDTH 320
#define AA_FONT_SMALL NotoSansBold15
#define SMOOTH_FONT

TFT_eSPI tft    = TFT_eSPI();
TFT_eSprite spr = TFT_eSprite(&tft); // Sprite class needs to be invoked
PNG png; 

int16_t xpos = 0;
int16_t ypos = 0;
int16_t rc;


int pngDraw(PNGDRAW *pDraw) {
  uint16_t lineBuffer[MAX_IMAGE_WIDTH];
  png.getLineAsRGB565(pDraw, lineBuffer, PNG_RGB565_BIG_ENDIAN, 0xffffffff);
  tft.pushImage(xpos, ypos + pDraw->y, pDraw->iWidth, 1, lineBuffer);

  return 1;
}

bool display_png(const byte image[], size_t size){
  rc = png.openFLASH((uint8_t *)image, size, pngDraw);
  if (rc == PNG_SUCCESS) {
    tft.startWrite();
    rc = png.decode(NULL, 0);
    tft.endWrite();

    return true;
  }
  
  Serial.println("[ERROR] : Failed to display image!");
  return false;
}

void display_text(const char *text, const uint8_t posx, const uint8_t posy){
  spr.loadFont(AA_FONT_SMALL); // Must load the font first into the sprite class

  spr.createSprite(320, 30);
  spr.fillSprite(TFT_BLACK);
  spr.setTextColor(TFT_WHITE, TFT_WHITE);
  spr.setTextDatum(MC_DATUM);
  spr.drawString(text, 160, 15 );
  spr.pushSprite(posx, posy);
 
  spr.unloadFont(); // Remove the font from sprite class to recover memory used
  spr.deleteSprite(); // Recover memory
}

bool boot_screen() {
  tft.init();
  spr.setColorDepth(16);
  tft.setRotation(0);
  tft.setAttribute(UTF8_SWITCH, true);

  int xpos = tft.width() / 2; // Half the screen width
  int ypos = 50;

  Serial.print("[INFO] : Loading image ");
  Serial.println(VAR_NAME(BootBg));
  if(!display_png(BootBg, sizeof(BootBg))) return false;

  display_text("Welcome back :3", 0, 0);
  Serial.println("[INFO] : ILI9488 (Display) is successfully initialized!");
  delay(2000);

  return true;
}