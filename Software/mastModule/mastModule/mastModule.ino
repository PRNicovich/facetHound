#include "hardware/uart.h"
#include "RunningAverage.h"

#define forcePin 28

#define pitch_CS 14
#define pitch_DT 12
#define pitch_CLK 13

#define twist_CS 15
#define twist_DT 7
#define twist_CLK 6

#define TXPin 8
#define RXPin 9

#define CUI_12_BIT 0
#define CUI_14_BIT 1

#define TWIST_AVG_N 16
#define TIP_AVG_N 8
#define FORCE_AVG_N 4

SerialPIO baseSerial(TXPin,RXPin);

unsigned volatile int lastTransmission = millis();
unsigned volatile int zeroRoll = 0;

unsigned int forceSensor = 0;
unsigned long tipCount = 0;
unsigned long lastTip = 0;
unsigned long twistCount = 0;
unsigned long lastTwist = 0;

RunningAverage twistRA(TWIST_AVG_N);
RunningAverage tipRA(TIP_AVG_N);
RunningAverage forceRA(FORCE_AVG_N);

void setup(){


  baseSerial.begin(115200);
  while (!baseSerial){
    delay(10);
  }

  Serial.begin(115200);

  pinMode(forcePin, INPUT);

  // Begin encoder pins
  pinMode(pitch_CS, OUTPUT);
  pinMode(pitch_DT, INPUT);
  pinMode(pitch_CLK, OUTPUT);

  digitalWrite(pitch_CS, true);
  digitalWrite(pitch_CLK, false);

  pinMode(twist_CS, OUTPUT);
  pinMode(twist_DT, INPUT);
  pinMode(twist_CLK, OUTPUT);

  digitalWrite(twist_CS, true);
  digitalWrite(twist_CLK, false);

  baseSerial.flush();

  twistRA.clear();
  tipRA.clear();
  forceRA.clear();

  //Serial.println("Setup complete!");
  delay(500);

}


void loop(){

  bool sent = false;
  String val;
  long twistAvg;
  long tipAvg;
  long forceAvg;

  forceSensor = analogRead(forcePin);

  tipCount = readEncoderBitBang(pitch_CS, pitch_CLK, pitch_DT, CUI_14_BIT);
  twistCount = readEncoderBitBang(twist_CS, twist_CLK, twist_DT, CUI_12_BIT);

  twistRA.addValue(twistCount*TWIST_AVG_N);
  twistAvg = int(twistRA.getFastAverage());

  tipRA.addValue(tipCount*TIP_AVG_N);
  tipAvg = int(tipRA.getFastAverage());

  forceRA.addValue(forceSensor*FORCE_AVG_N);
  forceAvg = int(forceRA.getFastAverage());
/*
  Serial.print(forceAvg);
  Serial.print("  -  ");
  Serial.print(tipAvg);
  Serial.print("  -  ");
  Serial.println(twistAvg);
*/
  if (baseSerial.available()){
    
    val = baseSerial.readStringUntil('\n');

    char switchChar = val.charAt(0);

    switch (switchChar){
      case('?') : 
        // Send Frame        
        sendCharAndInt("i", tipAvg);
        sendCharAndInt("l", twistAvg);
        //Serial.println(twistCount);

        sendCharAndInt("e", forceAvg);
    }


  }

  
}

void sendCharAndInt(const char* s, int i){
	char stringOut[8];
	sprintf(stringOut, "%s %d", s, i);
	baseSerial.println(stringOut);
  baseSerial.flush();
}

long readEncoderBitBang(int csPin, int ckPin, int dtPin, int mode){

  digitalWrite(csPin, false);
  delayMicroseconds(20);

  unsigned int outVal = 0;

  for (int i = 0; i < 16; i++){

    if (i == 8){
      delayMicroseconds(8);
    }

    digitalWrite(ckPin, true);
    delayMicroseconds(2);
    outVal = outVal << 1;

    if (digitalRead(dtPin)){

      outVal += 1;

    }
    digitalWrite(ckPin, false);
    delayMicroseconds(2);
    
    
  }

  delayMicroseconds(20);


  digitalWrite(csPin, true);

  if (mode == CUI_12_BIT){
    outVal = (outVal & 0x3FFF) >> 2;
  }
  else if (mode == CUI_14_BIT){
    outVal = (outVal & 0xFFFF) >> 2;
  }
  else{
    outVal = 0;
  }

  


  return outVal;

}

void print_binary(int number, byte Length){
  static int Bits;
  if (number) { //The remaining bits have a value greater than zero continue
    Bits++; // Count the number of bits so we know how many leading zeros to print first
    print_binary(number >> 1, Length); // Remove a bit and do a recursive call this function.
    if (Bits) for (byte x = (Length - Bits); x; x--)Serial.write('0'); // Add the leading zeros
    Bits = 0; // clear no need for this any more
    Serial.write((number & 1) ? '1' : '0'); // print the bits in reverse order as we depart the recursive function
    
  }

}

