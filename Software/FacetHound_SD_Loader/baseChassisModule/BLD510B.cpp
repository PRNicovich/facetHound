#include "BLD510B.h"

BLD510B::BLD510B(Stream &port, uint8_t slaveAddr, int8_t dePin)
  : _port(port), _addr(slaveAddr), _dePin(dePin), _polePairs(2),
    _controlHigh(0x08 /* NW=1, everything else 0 */), _lastError(OK) {}

void BLD510B::begin(uint8_t polePairs) {
  _polePairs = polePairs & 0x0F;
  if (_dePin >= 0) {
    pinMode(_dePin, OUTPUT);
    digitalWrite(_dePin, LOW); // receive mode by default
  }
}

// ---------- CRC16 (standard Modbus, poly 0xA001, init 0xFFFF) ----------
uint16_t BLD510B::crc16(const uint8_t *buf, uint8_t len) {
  uint16_t crc = 0xFFFF;
  for (uint8_t i = 0; i < len; i++) {
    crc ^= buf[i];
    for (uint8_t b = 0; b < 8; b++) {
      if (crc & 0x0001) {
        crc >>= 1;
        crc ^= 0xA001;
      } else {
        crc >>= 1;
      }
    }
  }
  return crc;
}

void BLD510B::txEnable(bool on) {
  if (_dePin >= 0) digitalWrite(_dePin, on ? HIGH : LOW);
}

void BLD510B::sendFrame(uint8_t *frame, uint8_t len) {
  // clear any stale bytes sitting in the receive buffer from a previous
  // partial/failed exchange before we transmit
  while (_port.available()) _port.read();

  txEnable(true);
  _port.write(frame, len);
  _port.flush();     // block until the UART has actually shifted all bits out
  txEnable(false);
}

bool BLD510B::recvFrame(uint8_t *buf, uint8_t maxLen, uint8_t &lenOut, uint32_t timeoutMs) {
  uint32_t start = millis();
  uint8_t n = 0;
  // Modbus RTU frames end with a ~3.5-char silence; since we know our
  // expected max frame sizes are small, we just read until timeout or
  // buffer full, with a short inter-byte timeout to detect end-of-frame.
  uint32_t lastByteTime = start;
  while (millis() - start < timeoutMs) {
    if (_port.available()) {
      if (n < maxLen) buf[n++] = _port.read();
      else _port.read(); // discard overflow
      lastByteTime = millis();
    } else if (n > 0 && (millis() - lastByteTime) > 5) {
      break; // 5ms of silence after at least one byte = end of frame
    }
  }
  lenOut = n;
  if (n == 0) { _lastError = ERR_TIMEOUT; return false; }
  return true;
}

bool BLD510B::writeRegister(uint16_t reg, uint16_t data, bool swapDataBytes) {
  uint8_t frame[8];
  frame[0] = _addr;
  frame[1] = 0x06;
  frame[2] = reg >> 8;
  frame[3] = reg & 0xFF;
  if (!swapDataBytes) {
    frame[4] = data >> 8;
    frame[5] = data & 0xFF;
  } else {
    frame[4] = data & 0xFF;
    frame[5] = data >> 8;
  }
  uint16_t crc = crc16(frame, 6);
  frame[6] = crc & 0xFF;
  frame[7] = crc >> 8;

  sendFrame(frame, 8);

  uint8_t resp[16], rlen;
  if (!recvFrame(resp, sizeof(resp), rlen)) return false;

  if (rlen < 8) { _lastError = ERR_SHORT_FRAME; return false; }
  if (resp[0] != _addr) { _lastError = ERR_ADDR_MISMATCH; return false; }
  if (resp[1] & 0x80) { _lastError = ERR_EXCEPTION; return false; }

  uint16_t gotCrc = resp[rlen - 2] | (resp[rlen - 1] << 8);
  if (gotCrc != crc16(resp, rlen - 2)) { _lastError = ERR_CRC; return false; }

  // Function 0x06 echoes the request back verbatim on success.
  _lastError = OK;
  return true;
}

bool BLD510B::readRegisters(uint16_t startReg, uint8_t count, uint16_t *out) {
  uint8_t frame[8];
  frame[0] = _addr;
  frame[1] = 0x03;
  frame[2] = startReg >> 8;
  frame[3] = startReg & 0xFF;
  frame[4] = 0x00;
  frame[5] = count;
  uint16_t crc = crc16(frame, 6);
  frame[6] = crc & 0xFF;
  frame[7] = crc >> 8;

  sendFrame(frame, 8);

  uint8_t resp[64], rlen;
  if (!recvFrame(resp, sizeof(resp), rlen)) return false;

  if (rlen < 5) { _lastError = ERR_SHORT_FRAME; return false; }
  if (resp[0] != _addr) { _lastError = ERR_ADDR_MISMATCH; return false; }
  if (resp[1] & 0x80) { _lastError = ERR_EXCEPTION; return false; }

  uint8_t byteCount = resp[2];
  if (rlen < (uint8_t)(3 + byteCount + 2)) { _lastError = ERR_SHORT_FRAME; return false; }

  uint16_t gotCrc = resp[3 + byteCount] | (resp[3 + byteCount + 1] << 8);
  if (gotCrc != crc16(resp, 3 + byteCount)) { _lastError = ERR_CRC; return false; }

  for (uint8_t i = 0; i < count && (3 + i * 2 + 1) < rlen; i++) {
    out[i] = (resp[3 + i * 2] << 8) | resp[3 + i * 2 + 1];
  }

  _lastError = OK;
  return true;
}

bool BLD510B::writeControlWord() {
  uint16_t data = (_controlHigh << 8) | (_polePairs & 0x0F);
  return writeRegister(0x8000, data, false);
}

bool BLD510B::enable(bool forward) {
  _controlHigh = 0x08;               // NW=1
  _controlHigh |= 0x01;               // EN=1
  if (forward) _controlHigh &= ~0x02; // FR=0 -> forward
  else         _controlHigh |= 0x02;  // FR=1 -> reverse
  return writeControlWord();
}

bool BLD510B::stop() {
  _controlHigh = 0x08;  // NW=1, EN=0 -> coast to stop
  return writeControlWord();
}

bool BLD510B::brake() {
  _controlHigh = 0x08 | 0x04; // NW=1, BK=1
  return writeControlWord();
}

bool BLD510B::setSpeedRPM(uint16_t rpm) {
  // Confirmed quirk: this register's data bytes must be sent byte-swapped.
  return writeRegister(0x8005, rpm, true);
}

bool BLD510B::setAccelDecelTime(uint8_t accelTenthsSec, uint8_t decelTenthsSec) {
  uint16_t data = ((uint16_t)accelTenthsSec << 8) | decelTenthsSec;
  return writeRegister(0x8003, data, false);
}

bool BLD510B::setBrakingForce(uint16_t force0to1023) {
  if (force0to1023 > 1023) force0to1023 = 1023;
  return writeRegister(0x8006, force0to1023, false);
}

bool BLD510B::readActualSpeedRPM(int32_t &rpmOut) {
  uint16_t raw;
  if (!readRegisters(0x8018, 1, &raw)) return false;
  // Speed register only: installed BLD returns low byte first. Keep generic
  // register/status decoding unchanged (faults occupy the status high byte).
  raw = uint16_t((raw >> 8) | (raw << 8));
  // Unconfirmed scaling -- see header comment. If this looks wrong on
  // your bench (e.g. off by ~pole-pair-count or ~20x), that's the first
  // thing to adjust. Fall back to raw value if in doubt:
  //   rpmOut = raw;
  if (_polePairs == 0) { rpmOut = raw; return true; }
  rpmOut = ((int32_t)raw * 20) / _polePairs;
  return true;
}

bool BLD510B::readStatus(uint8_t &faultFlags, uint8_t &runFlags) {
  uint16_t raw;
  if (!readRegisters(0x801B, 1, &raw)) return false;
  faultFlags = raw >> 8;
  runFlags = raw & 0xFF;
  return true;
}
