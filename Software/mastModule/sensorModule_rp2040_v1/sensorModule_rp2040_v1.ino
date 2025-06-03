#include "hardware/uart.h"
#include "pio_encoder.h"

PioEncoder encoder(11); // encoder is connected to GPI11 and GPI12

unsigned const int uartRx = 1;
unsigned const int uartTx = 0;

unsigned const int forcePin = 28;

unsigned const int pitch_CS = 8;
unsigned const int pitch_DT = 13;
unsigned const int pitch_CLK = 14;


unsigned volatile int zeroPitch = 0;
unsigned volatile int zeroRoll = 0;

unsigned int forceSensor = 0;
signed long encCount = 0;
unsigned long pitchCount = 0;

void setup(){

  encoder.begin();

  Serial.begin(115200);
  while (!Serial){
    delay(10);
  }
  
  Serial1.setRX(uartRx);
  Serial1.setTX(uartTx);
  
  Serial1.begin(115200);

  pinMode(forcePin, INPUT);

  // Begin encoder pins
  pinMode(pitch_CS, OUTPUT);
  pinMode(pitch_DT, INPUT);
  pinMode(pitch_CLK, OUTPUT);

  digitalWrite(pitch_CS, true);
  digitalWrite(pitch_CLK, false);

  Serial.println("Setup complete!");

}


void loop(){

  //Serial.println(forceSensor);

  //Serial1.print("Z ");
  encCount = encoder.getCount();
  //Serial1.println(encCount);

  //Serial1.print("F ");
  forceSensor = analogRead(forcePin);
  //Serial1.println(forceSensor);

  Serial.flush();
  //Serial1.flush();

  //Serial1.print("P ");
  pitchCount = readEncoderBitBang(pitch_CS, pitch_CLK, pitch_DT);
  //Serial1.println(pitchCount);


  delay(100);

}

long readEncoderBitBang(int csPin, int ckPin, int dtPin){

  digitalWrite(csPin, false);
  delayMicroseconds(20);

  unsigned int outVal = 0;

  for (int i = 0; i < 16; i++){
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

  outVal = (outVal & 0x3FFF);


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

