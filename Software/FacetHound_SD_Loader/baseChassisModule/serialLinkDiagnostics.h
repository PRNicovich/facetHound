#pragma once

#include <Arduino.h>
#include <SerialPIO.h>
#include <RP2040Version.h>
#include <hardware/gpio.h>
#include <hardware/irq.h>
#include <hardware/pio.h>

// Inspection only: no UART restart, FIFO read/clear, pin configuration, IRQ
// changes, or forced PIO instructions. Capture before printing so USB output
// does not change the state being described. peek() does not consume a byte.
inline void reportSerialReceiveCheck(const char* name, SerialPIO& port, uint pin)
{
    const int queued = port.available();
    const int first = port.peek();
    const unsigned function = unsigned(gpio_get_function(pin));
    const unsigned level = gpio_get(pin);
    const bool running = bool(port);
    struct Snapshot {
        unsigned block, sm, enabled, fifo, source, nvic;
        uint32_t pc, instruction, divider, shift, exec, pins;
    };
    Snapshot rows[4];
    unsigned count = 0;
    PIO blocks[] = {pio0, pio1,
#if defined(PICO_RP2350)
                    pio2,
#endif
    };
    const unsigned irqNumbers[] = {PIO0_IRQ_0, PIO1_IRQ_0,
#if defined(PICO_RP2350)
                                   PIO2_IRQ_0,
#endif
    };
    for (unsigned b = 0; b < sizeof(blocks) / sizeof(blocks[0]); ++b) {
        if (function != unsigned(GPIO_FUNC_PIO0) + b) continue;
        PIO p = blocks[b];
        for (unsigned sm = 0; sm < 4; ++sm) {
            const uint32_t pins = p->sm[sm].pinctrl;
            const unsigned input = (pins & PIO_SM0_PINCTRL_IN_BASE_BITS) >>
                                   PIO_SM0_PINCTRL_IN_BASE_LSB;
            if (input != pin) continue; // All instrument pins are below GP32.
            rows[count++] = {b, sm, (p->ctrl >> sm) & 1u,
                pio_sm_get_rx_fifo_level(p, sm), (p->inte0 >> sm) & 1u,
                unsigned(irq_is_enabled(irqNumbers[b])),
                p->sm[sm].addr, p->sm[sm].instr, p->sm[sm].clkdiv,
                p->sm[sm].shiftctrl, p->sm[sm].execctrl, pins};
        }
    }
    Serial.print("@RXCHECK,"); Serial.print(name);
    Serial.print(",pin="); Serial.print(pin);
    Serial.print(",running="); Serial.print(running);
    Serial.print(",queued="); Serial.print(queued);
    Serial.print(",peek="); Serial.print(first); // -1 means empty/unavailable.
    Serial.print(",function="); Serial.print(function);
    Serial.print(",level="); Serial.print(level);
    Serial.print(",matching_sm="); Serial.println(count);
    for (unsigned i = 0; i < count; ++i) {
        const auto& r = rows[i];
        Serial.print("@RXPIO,"); Serial.print(name);
        Serial.print(",pio="); Serial.print(r.block);
        Serial.print(",sm="); Serial.print(r.sm);
        Serial.print(",enabled="); Serial.print(r.enabled);
        Serial.print(",fifo="); Serial.print(r.fifo);
        Serial.print(",irq_source="); Serial.print(r.source);
        Serial.print(",nvic="); Serial.print(r.nvic);
        Serial.print(",pc="); Serial.print(r.pc);
        Serial.print(",instruction=0x"); Serial.print(r.instruction, HEX);
        Serial.print(",divider=0x"); Serial.print(r.divider, HEX);
        Serial.print(",shift=0x"); Serial.print(r.shift, HEX);
        Serial.print(",exec=0x"); Serial.print(r.exec, HEX);
        Serial.print(",pins=0x"); Serial.println(r.pins, HEX);
    }
}
