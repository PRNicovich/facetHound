/*
  BLD510B.h
  Minimal Modbus RTU driver for the StepperOnline BLD-510B / BLD-510S
  brushless DC motor driver, for use over any Arduino Stream (SerialPIO,
  HardwareSerial, SoftwareSerial, etc).

  Based on StepperOnline's published manual (register map, page 8-9 of the
  BLD-510S manual, which is identical hardware/firmware family to the 510B)
  and their brushless drive configuration software help article.

  CONFIRMED FROM VENDOR DOCS:
    - 9600 baud, 8N1, Modbus RTU, CRC16 (standard poly 0xA001, init 0xFFFF)
    - Function 0x06 (write single register), Function 0x03 (read holding regs)
    - Reg 0x8000 = control word:
        high byte: bit0 EN, bit1 FR (dir), bit2 BK (brake), bit3 NW
                   (NW=1 -> RS485 controls start/stop/speed instead of the
                    external EN/FR/BK/SV pins)
        low byte:  bits0-3 = pole pair count (1-15), bits4-7 = hall angle
    - Reg 0x8005 = speed setpoint (RPM), BUT the 2 data bytes must be sent
      byte-swapped (low byte first) unlike every other register on this
      device. Vendor's own example: to set 2000 RPM (0x07D0) the wire bytes
      are ...80 05 D0 07... -- confirmed straight from their help doc.
    - Reg 0x8018 = actual motor speed (read-only, function 03)
    - Reg 0x801B = high byte fault flags, low byte run-state flags

  NOT FULLY CONFIRMED (implemented as best guess, verify on your bench):
    - The exact scale factor from the raw 0x8018 register value to RPM.
      The manual mentions a note about multiplying by 20 and dividing by
      pole count for a related speed value; I've applied that here but
      you should sanity check readActualSpeedRPM() against a tachometer
      or the vendor's Windows config tool before trusting it.
    - Whether the 0x8018 *read* response is byte-swapped like the 0x8005
      *write* is. I've implemented it as normal big-endian (standard
      Modbus). If readActualSpeedRPM() looks like nonsense multiply by
      256 too high/low, that's the first thing to try flipping.

  This is a from-scratch minimal implementation, not a wrapper around a
  generic Modbus library, specifically because of the byte-swap quirk on
  the speed register -- a generic ModbusMaster-style library will get
  that register wrong.
*/

#ifndef BLD510B_H
#define BLD510B_H

#include <Arduino.h>

class BLD510B {
public:
  // port     : the Stream your RS485 transceiver is attached to (e.g. SerialPIO)
  // slaveAddr: Modbus site address of the drive (factory default = 1)
  // dePin    : GPIO driving the RS485 transceiver's DE/RE direction pin.
  //            Pass -1 if your adapter auto-directs (many cheap RS232/RS485
  //            USB-style bridge boards do, since they only expose TX/RX).
  BLD510B(Stream &port, uint8_t slaveAddr = 1, int8_t dePin = -1);

  void begin(uint8_t polePairs);

  // --- high level motor control ---
  bool enable(bool forward = true);   // sets EN=1, NW=1, FR=dir
  bool stop();                        // sets EN=0, NW=1 (coast to stop)
  bool brake();                       // sets EN=0, BK=1, NW=1 (active brake)
  bool setSpeedRPM(uint16_t rpm);     // writes reg 0x8005 (byte-swapped write)
  bool setAccelDecelTime(uint8_t accelTenthsSec, uint8_t decelTenthsSec); // reg 0x8003
  bool setBrakingForce(uint16_t force0to1023);                            // reg 0x8006

  // --- feedback ---
  bool readActualSpeedRPM(int32_t &rpmOut);   // reg 0x8018 (see caveats above)
  bool readStatus(uint8_t &faultFlags, uint8_t &runFlags); // reg 0x801B

  // --- low level, for anything not wrapped above ---
  bool writeRegister(uint16_t reg, uint16_t data, bool swapDataBytes = false);
  bool readRegisters(uint16_t startReg, uint8_t count, uint16_t *out);

  uint8_t lastError() const { return _lastError; }

  enum ErrorCode {
    OK = 0,
    ERR_TIMEOUT,
    ERR_CRC,
    ERR_SHORT_FRAME,
    ERR_ADDR_MISMATCH,
    ERR_EXCEPTION
  };

private:
  Stream &_port;
  uint8_t _addr;
  int8_t  _dePin;
  uint8_t _polePairs;
  uint8_t _controlHigh; // cached EN/FR/BK/NW bits between calls
  uint8_t _lastError;

  void txEnable(bool on);
  void sendFrame(uint8_t *frame, uint8_t len);
  bool recvFrame(uint8_t *buf, uint8_t maxLen, uint8_t &lenOut, uint32_t timeoutMs = 200);
  static uint16_t crc16(const uint8_t *buf, uint8_t len);
  bool writeControlWord();
};

#endif
