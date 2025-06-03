#include <TMC2209.h>
#include "FreqCountRP2.h"

#define motorALMpin 8
#define motorPGpin 27
#define motorSVpin 6
#define motorBKpin 14
#define motorENpin 15
#define motorFRpin 26

#define motorMaxRPM 3500
#define motorNPoles 3

const int freqCountDuration = 1000; // ms

const uint8_t EN_PIN = 3;
const uint8_t TX_PIN = 4;
const long SERIAL_BAUD_RATE = 19200;
const uint8_t FAN_PIN = 28;

uint8_t FAN_SPEED = 50;
int8_t accelDir = 1;
unsigned const int uartRx = 1;
unsigned const int uartTx = 0;

bool motorAlarm = false;
bool motorOn = false;
unsigned int motorSpeedRaw = 0;
unsigned int motorSpeedSet = 0;
unsigned int motorSpeedCorr = 0;
unsigned int motorSpeedSetRaw = 0;

unsigned long frequency = 0;

SerialPIO pioSerial(TX_PIN, SerialPIO::NOPIN);

// Instantiate TMC2209
TMC2209 driver;

// current values may need to be reduced to prevent overheating depending on
// specific motor and power supply voltage
const uint8_t RUN_CURRENT_PERCENT = 100;

const int32_t RUN_VELOCITY = 204800;


void setup()
{

  Serial.begin(115200);

  Serial1.setRX(uartRx);
  Serial1.setTX(uartTx);
  
  Serial1.begin(115200);

  driver.setup(pioSerial, SERIAL_BAUD_RATE);
  driver.setHardwareEnablePin(EN_PIN);
  driver.setMicrostepsPerStep(256);
  driver.setRMSCurrent(2500, 0.11);
  driver.enableAutomaticCurrentScaling();
  driver.enableCoolStep();
  driver.enable();
  driver.moveAtVelocity(RUN_VELOCITY);

  pinMode(FAN_PIN, OUTPUT);
  FAN_SPEED = 180;
  analogWrite(FAN_PIN, FAN_SPEED);

  // Activate ESC
  pinMode(motorALMpin, INPUT);
  pinMode(motorPGpin, INPUT);
  pinMode(motorSVpin, OUTPUT);
  pinMode(motorBKpin, OUTPUT);
  pinMode(motorENpin, OUTPUT);
  pinMode(motorFRpin, OUTPUT);

  digitalWrite(motorBKpin, true);
  digitalWrite(motorENpin, motorOn);
  digitalWrite(motorFRpin, false);

  FreqCountRP2.beginTimer(motorPGpin, freqCountDuration);

  motorSpeedSetRaw = int(motorSpeedSet*255/motorMaxRPM);
  analogWrite(motorSVpin, motorSpeedSetRaw);

}

void loop()
{

  if (FAN_SPEED >= 200){
    FAN_SPEED = 200;
    accelDir = -1;
  }
  
  if (FAN_SPEED <= 160){
    FAN_SPEED = 160;
    accelDir = 1;
  }

  analogWrite(FAN_PIN, FAN_SPEED);

  FAN_SPEED += accelDir;

  delay(300);

  Serial.println(FAN_SPEED);

  Serial1.println(0xCC);


  motorSpeedSetRaw = int(motorSpeedSet*255/motorMaxRPM);
  analogWrite(motorSVpin, motorSpeedSetRaw);

  if (FreqCountRP2.available())
  {
    frequency = FreqCountRP2.read();
    // Do something with the frequency...

    motorSpeedCorr = frequency*motorNPoles;
  }



}
