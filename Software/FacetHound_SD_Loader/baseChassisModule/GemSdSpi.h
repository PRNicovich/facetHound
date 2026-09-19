#pragma once

#include <Arduino.h>
#include <SPI.h>

// Private SD transport for the v9 header's non-hardware-SPI pin mapping.
// No PIO allocation, FIFO waits, DMA, or interrupt masking. Repeated begin/end
// is safe. Keep SD/SdFat responsible for commands, timeouts and filesystems.
class GemSdSpi final : public arduino::HardwareSPI {
public:
    GemSdSpi(pin_size_t sck, pin_size_t miso, pin_size_t mosi)
        : sck_(sck), miso_(miso), mosi_(mosi) {}

    void begin() override {
        if (started_) return;
        digitalWrite(sck_, LOW);
        digitalWrite(mosi_, HIGH);
        pinMode(sck_, OUTPUT);
        pinMode(mosi_, OUTPUT);
        pinMode(miso_, INPUT_PULLUP);
        started_ = true;
    }
    void end() override {
        if (started_) digitalWrite(sck_, LOW);
        started_ = false;
    }
    void beginTransaction(SPISettings settings) override {
        begin();
        // This private bus is SD-only: mode 0, MSB first. Never exceed 250kHz,
        // including on overclocked CPUs. Honour requests for slower clocks.
        const uint32_t hz = settings.getClockFreq();
        halfPeriodUs_ = hz ? uint32_t((500000ULL + hz - 1) / hz) : 2;
        if (halfPeriodUs_ < 2) halfPeriodUs_ = 2;
        digitalWrite(sck_, LOW);
    }
    void endTransaction() override { digitalWrite(sck_, LOW); }
    uint8_t transfer(uint8_t value) override {
        uint8_t result = 0;
        for (uint8_t mask = 0x80; mask; mask >>= 1) {
            digitalWrite(mosi_, (value & mask) ? HIGH : LOW);
            delayMicroseconds(halfPeriodUs_);
            digitalWrite(sck_, HIGH);
            delayMicroseconds(halfPeriodUs_);
            result = uint8_t((result << 1) | (digitalRead(miso_) ? 1 : 0));
            digitalWrite(sck_, LOW);
        }
        return result;
    }
    uint16_t transfer16(uint16_t value) override {
        const uint16_t high = transfer(uint8_t(value >> 8));
        return uint16_t((high << 8) | transfer(uint8_t(value)));
    }
    void transfer(void* buffer, size_t count) override {
        transfer(static_cast<const void*>(buffer), buffer, count);
    }
    void transfer(const void* source, void* destination, size_t count) override {
        const uint8_t* tx = static_cast<const uint8_t*>(source);
        uint8_t* rx = static_cast<uint8_t*>(destination);
        while (count--) {
            // SdFat reads with null TX and writes with null RX. SD requires
            // MOSI high (0xff), not zero/undefined memory, while receiving.
            const uint8_t received = transfer(tx ? *tx++ : uint8_t(0xff));
            if (rx) *rx++ = received;
        }
    }
    void usingInterrupt(int) override {}
    void notUsingInterrupt(int) override {}
    void attachInterrupt() override {}
    void detachInterrupt() override {}

private:
    pin_size_t sck_, miso_, mosi_;
    uint32_t halfPeriodUs_ = 2;
    bool started_ = false;
};
