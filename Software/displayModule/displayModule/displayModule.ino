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
#include "dcTerminal_40.h"

#define GEM_ICON diamonds
#define NUMBERS dcTerminal_80
#define LOGO dcTerminal_60
#define NUMBERS_SMALL dcTerminal_60
#define LABELS dcTerminal_30
#define ERRORTEXT dcTerminal_40
#define SPRITE_FILL 0x0000

#define TXPin 8
#define RXPin 9

SerialPIO baseSerial(TXPin,RXPin);

PioEncoder encoder(10); // encoder is connected to GPI11 and GPI12

TFT_eSPI tft = TFT_eSPI();       // Invoke custom library

TFT_eSprite stext1 = TFT_eSprite(&tft); // Sprite object stext1
TFT_eSprite stext2 = TFT_eSprite(&tft); // Sprite object stext1
TFT_eSprite stext3 = TFT_eSprite(&tft); // Sprite object stext1
TFT_eSprite stext4 = TFT_eSprite(&tft); // Sprite object stext1
TFT_eSprite stext5 = TFT_eSprite(&tft); // Sprite object stext1
TFT_eSprite stext6 = TFT_eSprite(&tft); // Sprite object stext1

TFT_eSprite stext7 = TFT_eSprite(&tft); // Sprite object stext1
TFT_eSprite stext8 = TFT_eSprite(&tft); // Sprite object stext1
TFT_eSprite stext9 = TFT_eSprite(&tft); // Sprite object stext1
TFT_eSprite stext10 = TFT_eSprite(&tft); // Sprite object stext1
TFT_eSprite stext11 = TFT_eSprite(&tft); // Sprite object stext1
TFT_eSprite stext12 = TFT_eSprite(&tft); // Sprite object stext1
TFT_eSprite stext13 = TFT_eSprite(&tft); // Sprite object stext1
TFT_eSprite stext14 = TFT_eSprite(&tft); // Sprite object stext1

TFT_eSprite sBar1 = TFT_eSprite(&tft); // Sprite object stext1


TFT_eSprite axisLabels = TFT_eSprite(&tft); // Sprite object stext1
TFT_eSprite unitLabels = TFT_eSprite(&tft); // Sprite object stext1
TFT_eSprite downLabels = TFT_eSprite(&tft); // Sprite object stext1

bool printed_blank = false;

//bool useSerial = true;
//bool useUART = false;
//bool verbose = false;

bool serialFound = false;
bool usbFound = false;
bool updateValuesFlag = false;
bool updateDisplayFlag = false;
bool everybodyGo = false;
bool uartFound = false;


bool updateTiltAngle = true;
bool updateTiltError = true;
float tiltSetAngle = 210.05;
float tiltSetError = 80.02;
bool tiltLock = true;
bool updateTiltLockBool = true;
bool updateTiltStepIndexBool = true;
bool tiltStepIndex = 2;


bool tiltServoMode = true;
bool updateTiltServoMode = true;

bool updateTipAngle = true;
float tipAngle = 44.5;


bool updateZValue = true;
float zValue = 115.44;
bool zedLock = true;
bool updateZedLockBool = true;
bool updateZedStepIndex = true;
bool zedStepIndex = 2;

bool updateRPMValue = true;
uint32_t RPMValue = 4568;
bool RPM_dir = false;
bool updateRPMDirBool = true;

bool updateFlowRate = true;
float flowRate = 25.5;
bool Flow_dir = true;
bool updateFlowDirBool = true;

bool updateForceBarBool = true;
uint8_t forceBar = 118;

int32_t encCount = 0;
int32_t lastEnc = 0;
volatile unsigned long encTick = millis();

int wheelIndex = 256;
bool updateWheelIndexBool = true;

volatile long screenTick = 0;
volatile long mainTick = 0;

void setup() {

  initConnections();

  everybodyGo = true;

}


void loop() {

  while (baseSerial.available()){

    String val = baseSerial.readStringUntil('\n');
    
    inputHandler(val);
  } 

  if ((millis() - screenTick) > 50){
    requestFrame();
    screenTick = millis();
  }

  spriteTickHandler();

}


void spriteTickHandler(){


  if (updateTipAngle){
    updateTipSprite();
    updateTipAngle = false;
  }

  if (updateTiltAngle){
    updateTiltSprite();
    updateTiltAngle = false;
  }

  if (updateTiltError){
    updateTiltErrorSprite();
    updateTiltError = false;
  }



  if (updateZValue){
    updateZSprite();
    updateZValue = false;
  }
    
  if (updateRPMValue){
    updateRPMSprite();
    updateRPMValue = false;
  }
  
  if (updateFlowRate){
    updateFlowSprite();
    updateFlowRate = false;
  }

  if (updateForceBarBool){
    updateForceBarSprite();
    updateForceBarBool = false;
  }

  if (updateTiltLockBool){
    updateTiltLockSprite();
    updateTiltLockBool = false;
  }

  if (updateZedLockBool){
    updateZedLockSprite();
    updateZedLockBool = false;
  }

  if (updateWheelIndexBool){
    updateWheelIndexSprite();
    updateWheelIndexBool = false;
  }

  if (updateFlowDirBool){
    updateFlowDirSprite();
    updateFlowDirBool = false;
  }

  if (updateRPMDirBool){
    updateRPMDirSprite();
    updateRPMDirBool = false;
  }

  if (updateTiltStepIndexBool){
    updateTiltStepIndexSprite();
    updateTiltStepIndexBool = false;
  }

  if (updateTiltServoMode){
    updateStepServoSprite();
    updateTiltServoMode = false;
  }

  if (updateZedStepIndex){
    updateZedStepIndexSprite();
    updateZedStepIndex = false;
  }




}



void encoderHandler(){
    encCount = encoder.getCount();
}


void sendCharAndInt(const char* s, int i){
	char stringOut[8];
	sprintf(stringOut, "%s %d", s, i);
	baseSerial.println(stringOut);
}

void inputHandler(String val){

  char switchChar = val.charAt(0);

  //Serial.println(val);
  //Serial.println(int(switchChar));

  switch (switchChar)
  {
    case ('a'):
      Serial.println("a");
      break;

    case ('b'):
      Serial.println("b");
      break;

    case ('c'):
      Serial.println("c");
      break;

    case ('d'):
      Serial.println("d");
      break;

    case ('e'):
      Serial.println("e");
      break;

    case ('f'):
      updateForce(val.substring(2));
      break;

    case ('g'):
      Serial.println("g");
      break;

    case ('h'):
      Serial.println("h");
      break;

    case ('i'):
      Serial.println("i");
      break;

    case ('R'):

      updateR(val.substring(2));

      break;

    case ('F'):

      updateF(val.substring(2));

      break;

    case ('T'):

      updateT(val.substring(2));

      break;

    case ('Z'):

      updateZ(val.substring(2));

      break;

    case ('P'):
      // tip
      updateP(val.substring(2));

      break;

    case ('?') :
      encoderHandler();
      transmitFrame();
      break;

    default:
      //Serial.println(switchChar);
      break;
  }

  
}

void transmitFrame(){

	char stringOut[8];
	sprintf(stringOut, "w %d", encCount);
	baseSerial.println(stringOut);

  //Serial.println(stringOut);

  baseSerial.flush();

}

void requestFrame(){
  //baseSerial.flush();
  baseSerial.println('?');
}

void updateT(String subVal){
  char chars[8];
  subVal.toCharArray(chars, subVal.length()+1);
  tiltSetAngle =  atof(chars);
  updateTiltAngle = true;
}


void updateZ(String subVal){
  char chars[8];
  subVal.toCharArray(chars, subVal.length()+1);
  zValue =  atof(chars);
  updateZValue = true;
}

void updateP(String subVal){
  char chars[8];
  subVal.toCharArray(chars, subVal.length()+1);
  tipAngle =  atof(chars);
  updateTipAngle = true;
}

void updateR(String subVal){
  char chars[8];
  subVal.toCharArray(chars, subVal.length()+1);
  RPMValue =  atof(chars);
  //Serial.println(RPMValue);
  updateRPMValue = true;
}

void updateF(String subVal){
  char chars[8];
  subVal.toCharArray(chars, subVal.length()+1);
  flowRate =  atof(chars);
  updateFlowRate = true;
}

void updateForce(String subVal){
  char chars[8];
  subVal.toCharArray(chars, subVal.length()+1);
  forceBar =  atof(chars);
  updateForceBarBool = true;
}

void updateTiltErrorIncoming(String subVal){
  char chars[8];
  subVal.toCharArray(chars, subVal.length()+1);
  tiltSetError =  atof(chars);
  updateTiltError = true;
}



void initConnections(){

  
  Serial.begin(115200);

  if (true){
   // Serial1.setRX(RXPin);
   // Serial1.setTX(TXPin);
    Serial1.begin(115200);
    while (!Serial1){
      delay(10);
    }
    
  }

  encoder.begin();

  baseSerial.begin(115200);
	while (!baseSerial){
		delay(10);
	}
  uartFound = true;
	//baseSerial.flush();

  tft.init();
  tft.setRotation(2);

  tft.fillScreen(0x8888); 

  delay(100);

  drawSplashScreen();

  drawMainScreen(true);


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


void drawMainScreen(bool initHere) {

  if (initHere){
    // Initialize screen
    tft.fillScreen(0x0000);
    
    unitLabels.createSprite(30, 370);
    unitLabels.loadFont(LABELS);
    unitLabels.setTextDatum(BL_DATUM);  // Bottom right coordinate datum
    //unitLabels.fillSprite(SPRITE_FILL);
    unitLabels.setTextColor(0x07FE); // White text, no background
    
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
    axisLabels.drawString("Φ", 22, 50);
    axisLabels.setTextColor(0xEF00);
    axisLabels.drawString("Θ", 22, 270);
    axisLabels.setTextColor(0xF8F4);
    axisLabels.drawString("z", 22, 368);
    axisLabels.pushSprite(0, 0);

    downLabels.createSprite(320, 25);
    downLabels.setTextDatum(BR_DATUM);  // Bottom right coordinate datum
    downLabels.loadFont(LABELS);
    //downLabels.fillSprite(SPRITE_FILL);
    downLabels.setTextColor(0xFFFF); // White text, no background
    downLabels.drawString("RPM", 100, 27);
    downLabels.drawString("mL/min", 300, 27);
    downLabels.pushSprite(0, 450);


      // Create a sprite for the scrolling numbers
    stext1.setColorDepth(8);
    stext1.loadFont(NUMBERS);
    stext1.createSprite(205, 60);
    stext1.setTextColor(0x07FE); // White text, no background
    stext1.setTextDatum(BR_DATUM);  // Bottom right coordinate datum

    stext2.setColorDepth(8);
    stext2.createSprite(256, 110);
    stext2.loadFont(NUMBERS);
    stext2.setTextColor(0xEF00); // White text, no background
    stext2.setTextDatum(BR_DATUM);  // Bottom right coordinate datum


    stext3.setColorDepth(8);
    stext3.createSprite(256, 110);
    stext3.loadFont(NUMBERS);
    stext3.setTextColor(0xF8F4); // White text, no background
    stext3.setTextDatum(BR_DATUM);  // Bottom right coordinate datum


    stext4.setColorDepth(8);
    stext4.createSprite(140, 70);
    stext4.loadFont(NUMBERS_SMALL);
    stext4.setTextColor(0xFE19); // White text, no background
    stext4.setTextDatum(BR_DATUM);  // Bottom right coordinate datum


    stext5.setColorDepth(8);
    stext5.createSprite(140, 70);
    stext5.loadFont(NUMBERS_SMALL);
    stext5.setTextColor(0xC618); // White text, no background
    stext5.setTextDatum(BR_DATUM);  // Bottom right coordinate datum


    sBar1.setColorDepth(8);
    sBar1.createSprite(300, 20);   


    stext6.setColorDepth(8);
    stext6.createSprite(110, 40);
    stext6.loadFont(ERRORTEXT);
    stext6.setTextColor(0x07FE); // White text, no background
    stext6.setTextDatum(BR_DATUM);  // Bottom right coordinate datum


    stext7.setColorDepth(8);
    stext7.createSprite(30, 30);
    stext7.loadFont(LABELS);
    stext7.setTextColor(0x07FE); // White text, no background
    stext7.setTextDatum(BR_DATUM);  // Bottom right coordinate datum
    

    stext8.setColorDepth(8);
    stext8.createSprite(30, 30);
    stext8.loadFont(LABELS);
    stext8.setTextColor(0xF8F4); // White text, no background
    stext8.setTextDatum(BR_DATUM);  // Bottom right coordinate datum

    stext9.setColorDepth(8);
    stext9.createSprite(40, 30);
    stext9.loadFont(LABELS);
    stext9.setTextColor(0x07FE); // White text, no background
    stext9.setTextDatum(BR_DATUM);  // Bottom right coordinate datum

    stext10.setColorDepth(8);
    stext10.createSprite(30, 30);
    stext10.loadFont(LABELS);
    stext10.setTextColor(0xC618); // White text, no background
    stext10.setTextDatum(BR_DATUM);  // Bottom right coordinate datum

    stext11.setColorDepth(8);
    stext11.createSprite(30, 30);
    stext11.loadFont(LABELS);
    stext11.setTextColor(0xC618); // White text, no background
    stext11.setTextDatum(BR_DATUM);  // Bottom right coordinate datum

    stext12.setColorDepth(8);
    stext12.createSprite(30, 30);
    stext12.loadFont(LABELS);
    stext12.setTextColor(0xF8F4); // White text, no background
    stext12.setTextDatum(BR_DATUM);  // Bottom right coordinate datum

    stext13.setColorDepth(8);
    stext13.createSprite(30, 30);
    stext13.loadFont(LABELS);
    stext13.setTextColor(0x07FE); // White text, no background
    stext13.setTextDatum(BR_DATUM);  // Bottom right coordinate datum

    stext14.setColorDepth(8);
    stext14.createSprite(30, 30);
    stext14.loadFont(LABELS);
    stext14.setTextColor(0x07FE); // White text, no background
    stext14.setTextDatum(BR_DATUM);  // Bottom right coordinate datum


  }
  else{


    updateAllSprites();

  }
}
      
void updateAllSprites(){
  updateTiltSprite();
  updateTipSprite();
  updateZSprite();
  updateRPMSprite();
  updateFlowSprite();
  updateForceBarSprite();
  updateTiltLockSprite();
}

void updateTiltSprite(){
  stext1.fillSprite(SPRITE_FILL); // Fill sprite with blue
  stext1.drawFloat(tiltSetAngle, 2, 205, 75); // plot value in font 2
  stext1.pushSprite(5, 60);
}

void updateTipSprite(){
  stext2.fillSprite(SPRITE_FILL); // Fill sprite with blue
  stext2.drawFloat(tipAngle, 2, 256, 110); // plot value in font 2
  stext2.pushSprite(30, 170);
}

void updateZSprite(){
  stext3.fillSprite(SPRITE_FILL); // Fill sprite with blue
  stext3.drawFloat(zValue, 3, 256, 110); // plot value in font 2
  stext3.pushSprite(30, 270);
}

void updateRPMSprite(){
  stext4.fillSprite(SPRITE_FILL); // Fill sprite with blue
  stext4.drawNumber(RPMValue, 120, 60); // plot value in font 2
  stext4.pushSprite(5, 380);
}

void updateFlowSprite(){
  stext5.fillSprite(SPRITE_FILL); // Fill sprite with blue
  stext5.drawFloat(flowRate, 1, 140, 60); // plot value in font 2
  stext5.pushSprite(170, 380);
}

void updateForceBarSprite(){

  sBar1.fillSprite(SPRITE_FILL); // Fill sprite with blue
  sBar1.drawRect(0, 0, 280, 10, 0x00FF00);
  

  int fillBarWidth = forceBar*(280/255);
  sBar1.fillRect(0, 0, fillBarWidth, 10, 0x00FF00);
  sBar1.pushSprite(20, 170);
}

void updateTiltErrorSprite(){
  stext6.fillSprite(SPRITE_FILL); // Fill sprite with blue
  stext6.drawFloat(tiltSetError, 2, 110, 40); // plot value in font 2
  if (tiltSetError >= 0){
    stext6.drawString("+",15, 40);
  }
  else{
    stext6.drawString("-", 15, 40);
  }
  
  stext6.pushSprite(210, 87);
}

void updateTiltLockSprite(){
  
  stext7.fillSprite(SPRITE_FILL); // Fill sprite with blue
  if (tiltLock){
    stext7.drawString("X", 20, 30);
  }
  else{
    stext7.drawString("O", 20, 30);
  }
  
  stext7.pushSprite(40, 20);
}

void updateWheelIndexSprite(){
  stext9.fillSprite(SPRITE_FILL); // Fill sprite with blue
  stext9.drawString(String(wheelIndex), 40, 30);
  stext9.pushSprite(180, 20);
}


void updateZedLockSprite(){
  
  stext8.fillSprite(SPRITE_FILL); // Fill sprite with blue
  if (zedLock){
    stext8.drawString("X", 20, 30);
  }
  else{
    stext8.drawString("O", 20, 30);
  }
  
  stext8.pushSprite(2, 305);
}

void updateRPMDirSprite(){
  stext10.fillSprite(SPRITE_FILL); // Fill sprite with blue
  if (RPM_dir){
    stext10.drawString("X", 20, 30);
  }
  else{
    stext10.drawString("O", 20, 30);
  }
  
  stext10.pushSprite(10, 447);
}

void updateFlowDirSprite(){
  stext11.fillSprite(SPRITE_FILL); // Fill sprite with blue
  if (Flow_dir){
    stext11.drawString("X", 20, 30);
  }
  else{
    stext11.drawString("O", 20, 30);
  }
  
  stext11.pushSprite(190, 447);
}


void updateZedStepIndexSprite(){
  stext12.fillSprite(SPRITE_FILL); // Fill sprite with blue
  stext12.drawNumber(zedStepIndex, 20, 30);  
  stext12.pushSprite(290, 305);
}


void updateTiltStepIndexSprite(){
  stext13.fillSprite(SPRITE_FILL); // Fill sprite with blue
  stext13.drawNumber(tiltStepIndex, 20, 30);  
  stext13.pushSprite(290, 20);
}


void updateStepServoSprite(){
  stext14.fillSprite(SPRITE_FILL); // Fill sprite with blue
  if (tiltServoMode){
    stext14.drawString("K", 20, 30);
  }
  else{
    stext14.drawString("S", 20, 30);
  }
  
  stext14.pushSprite(80, 20);
}


