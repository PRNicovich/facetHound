/*
  BLD510B_Demo.ino

  RP2350 (Pico 2) -> RS485 transceiver on GP26/GP27 -> BLD-510B brushless
  driver -> motor.

  Board package: "Raspberry Pi Pico/RP2040" (arduino-pico core by
  earlephilhower) -- this is what provides the SerialPIO class. Install it
  via Boards Manager if you haven't, then select "Raspberry Pi Pico 2".

  WIRING
    Pico2 GP26  -> transceiver TX-in / DI   (Pico transmits to the module)
    Pico2 GP27  -> transceiver RX-out / RO  (Pico receives from the module)
    Pico2 3V3   -> transceiver VCC (check your module -- some want 5V, in
                   which case use Pico2 VBUS/5V and confirm the module's
                   RO logic level is safe for the RP2350's 3.3V GPIO)
    Pico2 GND   -> transceiver GND -> driver GND
    Transceiver A+/B- -> driver A+/B- (RS485 differential pair)

    If your transceiver module exposes a DE/RE direction pin (most cheap
    MAX485 breakouts do), tie it to a spare GPIO and set DE_PIN below.
    If your "RS232->RS485 converter" is one of the auto-direction-sensing
    USB-style adapters that only exposes TX/RX with no direction pin,
    leave DE_PIN as -1.

  DRIVER SIDE SETTINGS TO CHECK FIRST
    - Modbus site address: factory default is 1 (this demo assumes 1)
    - Baud rate: fixed at 9600 on this driver, not configurable
    - Pole pairs: set POLE_PAIRS below to match YOUR motor. Get this
      wrong and speed/torque behavior will be off even though commands
      "work".
*/

#include <SerialPIO.h>
#include "BLD510B.h"

// ---- pin configuration ----
constexpr int PIN_TX   = 26;   // GP26 -> RS485 module DI
constexpr int PIN_RX   = 27;   // GP27 -> RS485 module RO
constexpr int8_t DE_PIN = -1;  // set to a GPIO number if your module needs
                                // manual direction control, else leave -1

constexpr uint8_t DRIVER_ADDR = 1;   // Modbus site address of the driver
constexpr uint8_t POLE_PAIRS  = 2;   // <-- MATCH THIS TO YOUR MOTOR

SerialPIO rs485(PIN_TX, PIN_RX, 32);
BLD510B motor(rs485, DRIVER_ADDR, DE_PIN);

void printError(const char *what) {
  Serial.print(F("FAILED: "));
  Serial.print(what);
  Serial.print(F(" (err code "));
  Serial.print(motor.lastError());
  Serial.println(F(")"));
}

void setup() {
  Serial.begin(115200);
  uint32_t t0 = millis();
  while (!Serial && millis() - t0 < 3000) { /* wait for USB CDC, but don't hang forever */ }

  rs485.begin(9600); // BLD-510B is fixed at 9600 8N1
  motor.begin(POLE_PAIRS);

  Serial.println(F("BLD-510B demo starting..."));

  // Set some reasonable ramp behavior before we start commanding speed.
  // accel/decel units are 0.1s each; 20 = 2.0 seconds
  if (!motor.setAccelDecelTime(20, 20)) printError("setAccelDecelTime");

  // Read status once up front just to confirm we have working comms
  // before we spin anything.
  uint8_t fault, run;
  if (motor.readStatus(fault, run)) {
    Serial.print(F("Status OK. fault=0x"));
    Serial.print(fault, HEX);
    Serial.print(F(" run=0x"));
    Serial.println(run, HEX);
  } else {
    printError("readStatus (check wiring/address/baud before continuing)");
  }
}

void loop() {
  Serial.println(F("\n--- Ramping forward to 1500 RPM ---"));
  if (!motor.setSpeedRPM(1500)) printError("setSpeedRPM(1500)");
  if (!motor.enable(true /* forward */)) printError("enable(forward)");
  delay(4000);

  int32_t rpm;
  if (motor.readActualSpeedRPM(rpm)) {
    Serial.print(F("Actual speed (see caveats re: scaling): "));
    Serial.print(rpm);
    Serial.println(F(" RPM"));
  } else {
    printError("readActualSpeedRPM");
  }

  Serial.println(F("--- Slowing to 500 RPM ---"));
  if (!motor.setSpeedRPM(500)) printError("setSpeedRPM(500)");
  delay(4000);

  Serial.println(F("--- Stopping (coast) ---"));
  if (!motor.stop()) printError("stop");
  delay(2000);

  Serial.println(F("--- Ramping reverse to 800 RPM ---"));
  if (!motor.setSpeedRPM(800)) printError("setSpeedRPM(800)");
  if (!motor.enable(false /* reverse */)) printError("enable(reverse)");
  delay(4000);

  Serial.println(F("--- Active brake ---"));
  if (!motor.brake()) printError("brake");
  delay(3000);
}
