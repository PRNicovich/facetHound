// SPDX-FileCopyrightText: 2025 Tim Cocks for Adafruit Industries
//
// SPDX-License-Identifier: MIT

/*********************************************************************
 Adafruit invests time and resources providing this open source code,
 please support Adafruit and open-source hardware by purchasing
 products from Adafruit!

 MIT license, check LICENSE for more information
 Copyright (c) 2019 Ha Thach for Adafruit Industries
 All text above, and the splash screen below must be included in
 any redistribution
*********************************************************************/

#include <SPI.h>
#include <TFT_eSPI.h> // Hardware-specific library
#include "pio_encoder.h"
#include "diamonds.h"
#include "dcTerminal_30.h"
#include "dcTerminal_80.h"
#include "dcTerminal_60.h"

#define GEM_ICON diamonds
#define NUMBERS dcTerminal_80
#define LOGO dcTerminal_60
#define NUMBERS_SMALL dcTerminal_60
#define LABELS dcTerminal_30
#define SPRITE_FILL 0x0000

PioEncoder encoder(10); // encoder is connected to GPI11 and GPI12

TFT_eSPI tft = TFT_eSPI();       // Invoke custom library

TFT_eSprite stext1 = TFT_eSprite(&tft); // Sprite object stext1
TFT_eSprite stext2 = TFT_eSprite(&tft); // Sprite object stext1
TFT_eSprite stext3 = TFT_eSprite(&tft); // Sprite object stext1
TFT_eSprite stext4 = TFT_eSprite(&tft); // Sprite object stext1
TFT_eSprite stext5 = TFT_eSprite(&tft); // Sprite object stext1

TFT_eSprite axisLabels = TFT_eSprite(&tft); // Sprite object stext1
TFT_eSprite unitLabels = TFT_eSprite(&tft); // Sprite object stext1
TFT_eSprite downLabels = TFT_eSprite(&tft); // Sprite object stext1

bool printed_blank = false;

bool useSerial = true;
bool verbose = false;

bool serialFound = false;
bool usbFound = false;
bool updateValuesFlag = false;
bool updateDisplayFlag = false;
bool everybodyGo = false;

volatile long mainTick = 0;
volatile long encCount = 0;
volatile long knobCount = 0;
volatile long tickCount = 0;

volatile long screenTick = 0;
volatile long screenTick2 = millis();

uint32_t val = 0;
uint32_t key = 0;

void setup() {

  initConnections();

  everybodyGo = true;

}


void loop() {


  if (screenTick > 100000){

    mainTick = mainTick + 1;

    updateDisplayFlag = true;
    screenTick = 0;
  }
  else{
    screenTick++;
  }

  if (updateDisplayFlag){ 
    drawMainScreen(false, true); // draw screen, non-init mode
    updateDisplayFlag = false;    
  }
  

}


void initConnections(){

  if (useSerial){
    Serial.begin(115200);
    while (!Serial){
      delay(10);
    }
  }
  else{
    delay(100);
  }
  serialFound = true;


  tft.init();
  tft.setRotation(2);

  tft.fillScreen(0x8888); 

  drawSplashScreen();


  drawMainScreen(true, false);

  tft.loadFont("Unicode-Test-72");  


}

void drawSplashScreen(){

  tft.fillScreen(0x0000);
  
  TFT_eSprite splash = TFT_eSprite(&tft); // Sprite object stext1

  splash.loadFont(GEM_ICON);
  splash.createSprite(200, 200);
  splash.fillSprite(TFT_BLACK);
  splash.setTextColor(TFT_WHITE);
  splash.setTextDatum(MC_DATUM);
  splash.drawString("m", 100, 100);
  splash.pushSprite(60, 200);
  splash.unloadFont();
  
  delay(1000);

  TFT_eSprite logo = TFT_eSprite(&tft); // Sprite object stext1

  logo.loadFont(LOGO);
  logo.createSprite(320, 50);
  logo.fillSprite(TFT_BLACK);
  logo.setTextColor(TFT_WHITE);
  logo.setTextDatum(MC_DATUM);
  logo.drawString("FACET HOUND", 160, 25);
  logo.pushSprite(0, 100);
  logo.unloadFont();
  delay(2000);
  
  logo.unloadFont();

  
    

}


void drawMainScreen(bool initHere, bool updateJustFirstOne) {

  if (initHere){
    // Initialize screen
    tft.fillScreen(0x0000);
    
    unitLabels.createSprite(30, 370);
    unitLabels.loadFont(LABELS);
    unitLabels.setTextDatum(BL_DATUM);  // Bottom right coordinate datum
    //unitLabels.fillSprite(SPRITE_FILL);
    unitLabels.setTextColor(0x07FE); // White text, no background
    unitLabels.drawString("°", 0, 85);
    unitLabels.setTextColor(0xEF00);
    unitLabels.drawString("°", 0, 230);
    unitLabels.setTextColor(0xF8F4);
    unitLabels.drawString("mm", 0, 368);
    unitLabels.pushSprite(290, 0);

    axisLabels.createSprite(25, 370);
    axisLabels.setTextDatum(BR_DATUM);  // Bottom right coordinate datum
    axisLabels.loadFont(LABELS);
    //axisLabels.fillSprite(SPRITE_FILL);
    axisLabels.setTextColor(0x07FE); // White text, no background
    axisLabels.drawString("Θ", 22, 118);
    axisLabels.setTextColor(0xEF00);
    axisLabels.drawString("Φ", 22, 270);
    axisLabels.setTextColor(0xF8F4);
    axisLabels.drawString("Z", 22, 368);
    axisLabels.pushSprite(0, 0);

    downLabels.createSprite(320, 25);
    downLabels.setTextDatum(BR_DATUM);  // Bottom right coordinate datum
    downLabels.loadFont(LABELS);
    //downLabels.fillSprite(SPRITE_FILL);
    downLabels.setTextColor(0xFFFF); // White text, no background
    downLabels.drawString("RPM", 100, 27);
    downLabels.drawString("mL/min", 280, 27);
    downLabels.pushSprite(0, 450);


      // Create a sprite for the scrolling numbers
    stext1.setColorDepth(8);
    stext1.loadFont(NUMBERS);
    stext1.createSprite(256, 110);
    stext1.setTextColor(0x07FE); // White text, no background
    stext1.setTextDatum(BR_DATUM);  // Bottom right coordinate datum
    stext1.pushSprite(30, 20);

    stext2.setColorDepth(8);
    stext2.createSprite(256, 110);
    stext2.loadFont(NUMBERS);
    stext2.setTextColor(0xEF00); // White text, no background
    stext2.setTextDatum(BR_DATUM);  // Bottom right coordinate datum
    //stext2.fillSprite(SPRITE_FILL); // Fill sprite with blue
    stext2.drawFloat(9234.56, 2, 256, 110); // plot value in font 2
    stext2.pushSprite(30, 170);


    stext3.setColorDepth(8);
    stext3.createSprite(256, 110);
    stext3.loadFont(NUMBERS);
    stext3.setTextColor(0xF8F4); // White text, no background
    stext3.setTextDatum(BR_DATUM);  // Bottom right coordinate datum
    //stext3.fillSprite(SPRITE_FILL); // Fill sprite with blue
    stext3.drawFloat(890.10, 3, 256, 110); // plot value in font 2
    stext3.pushSprite(30, 270);



    stext4.setColorDepth(8);
    stext4.createSprite(140, 70);
    stext4.loadFont(NUMBERS_SMALL);
    stext4.setTextColor(0xFE19); // White text, no background
    stext4.setTextDatum(BR_DATUM);  // Bottom right coordinate datum
    //stext4.fillSprite(SPRITE_FILL); // Fill sprite with blue
    stext4.drawNumber(4125, 132, 70); // plot value in font 2
    stext4.pushSprite(5, 380);



    stext5.setColorDepth(8);
    stext5.createSprite(140, 70);
    stext5.loadFont(NUMBERS_SMALL);
    stext5.setTextColor(0xC618); // White text, no background
    stext5.setTextDatum(BR_DATUM);  // Bottom right coordinate datum
    //stext5.fillSprite(SPRITE_FILL); // Fill sprite with blue
    stext5.drawFloat(800.0, 1, 140, 70); // plot value in font 2
    stext5.pushSprite(170, 380);


  }
  else{
    //Update sprites

    if (updateJustFirstOne){

      
      stext1.fillSprite(SPRITE_FILL); // Fill sprite with blue
      stext1.drawFloat(mainTick, 2, 256, 110); // plot value in font 2
      stext1.pushSprite(30, 20);

    }
    else {

      stext2.fillSprite(SPRITE_FILL); // Fill sprite with blue
      stext2.drawFloat(9234.56, 2, 256, 110); // plot value in font 2
      stext2.pushSprite(30, 170);

      stext3.fillSprite(SPRITE_FILL); // Fill sprite with blue
      stext3.drawFloat(890.10, 3, 256, 110); // plot value in font 2
      stext3.pushSprite(30, 260);

      stext4.fillSprite(SPRITE_FILL); // Fill sprite with blue
      stext4.drawNumber(4125, 132, 60); // plot value in font 2
      stext4.pushSprite(5, 380);

      stext5.fillSprite(SPRITE_FILL); // Fill sprite with blue
      stext5.drawFloat(800.0, 1, 140, 60); // plot value in font 2
      stext5.pushSprite(170, 380);

    }

    Serial.print(mainTick);
    Serial.print(" | ");

    Serial.print(knobCount);
    Serial.print(" | ");

    Serial.print(tickCount);
    Serial.print(" | ");

    Serial.print(encCount);
    Serial.println();
    
  }

}
