// Isolated base-board pump test. Other motors remain disabled.
// Serial 115200: START runs five seconds, STOP stops immediately.
// Uses original pump settings; no SD, display, keyboard or lap UARTs.
// Stop the machine before uploading. This sketch cannot stop the external lap
// controller; keep the lap stopped while using it. Restore base firmware after.
#include <Arduino.h>
#include <SerialPIO.h>
#include <TMC2209.h>

SerialPIO pumpSerial(19, 0xff);
TMC2209 pump;
bool running=false;
uint32_t started=0;
char command[24];
uint8_t used=0;

void stopPump() {
    pump.moveAtVelocity(0);
    pump.disable();
    running=false;
    Serial.println("PUMP STOPPED");
}
void setup() {
    // Driver enable pins are active-low. Do not run other axes.
    const int enablePins[]={14,18,22};
    for(int pin : enablePins) {digitalWrite(pin,HIGH);pinMode(pin,OUTPUT);}
    Serial.begin(115200);
    delay(1500);
    pump.setup(pumpSerial,19200);
    pump.setHardwareEnablePin(22);
    pump.setMicrostepsPerStep(4);
    pump.setRMSCurrent(1100,0.11f);
    pump.enableAutomaticCurrentScaling();
    pump.enableCoolStep();
    pump.disableInverseMotorDirection();
    stopPump();
    Serial.println("PUMP BENCH: START = 5 seconds at velocity 400; STOP = stop");
}
void loop() {
    if(running && millis()-started>=5000)stopPump();
    while(Serial.available()) {
        char c=Serial.read();
        if(c=='\r')continue;
        if(c=='\n') {
            command[used]=0;used=0;
            if(!strcmp(command,"STOP"))stopPump();
            else if(!strcmp(command,"START") && !running) {
                pump.enable();pump.moveAtVelocity(400);
                started=millis();running=true;
                Serial.println("PUMP STARTED, timeout=5s");
            }
        } else if(used<sizeof(command)-1)command[used++]=c;
    }
}
