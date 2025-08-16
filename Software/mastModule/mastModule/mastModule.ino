#include "hardware/uart.h"

#define forcePin 28

#define pitch_CS 14
#define pitch_DT 12
#define pitch_CLK 13

#define twist_CS 15
#define twist_DT 7
#define twist_CLK 6

#define TXPin 8
#define RXPin 9

SerialPIO baseSerial(TXPin,RXPin);

unsigned volatile int lastTransmission = millis();
unsigned volatile int zeroRoll = 0;

unsigned int forceSensor = 0;
unsigned long tipCount = 0;
unsigned long lastTip = 0;
unsigned long twistCount = 0;
unsigned long lastTwist = 0;

void setup(){


  baseSerial.begin(115200);
  while (!baseSerial){
    delay(10);
  }

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

  //Serial.println("Setup complete!");
  delay(500);

}


void loop(){

  bool sent = false;

  if ((millis() - lastTransmission) > 50){

    forceSensor = analogRead(forcePin);

    tipCount = readEncoderBitBang(pitch_CS, pitch_CLK, pitch_DT);
    twistCount = readEncoderBitBang(twist_CS, twist_CLK, twist_DT);

    if (lastTip == tipCount){
    }
    else{
      sendCharAndInt("i", tipCount);
      lastTip = tipCount;
      sent = true;
    }

    if (lastTwist == twistCount){
    }
    else{
      sendCharAndInt("l", twistCount);
      lastTip = tipCount;
      sent = true;
    }

    if (sent){
      lastTransmission = millis();
    }
    
  }

}

void sendCharAndInt(const char* s, int i){
	char stringOut[8];
	sprintf(stringOut, "%s %d", s, i);
	baseSerial.println(stringOut);
  baseSerial.flush();
}

long readEncoderBitBang(int csPin, int ckPin, int dtPin){

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

  outVal = (outVal & 0xFFFF) >> 2;


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

