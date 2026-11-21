#pragma once

// otterNet hardware definitions: Heltec pins wired to the Pico / otterNet board.
// Each value can be overridden from variant.h or platformio build_flags (e.g. -DOTTERNET_PICO_RESET_PIN=5).

// UART link to the Pico
#ifndef OTTERNET_PICO_UART_TX
#define OTTERNET_PICO_UART_TX 38
#endif

#ifndef OTTERNET_PICO_UART_RX
#define OTTERNET_PICO_UART_RX 39
#endif

// Pull low to reset the Pico
#ifndef OTTERNET_PICO_RESET_PIN
#define OTTERNET_PICO_RESET_PIN 3
#endif

// Drive high to cut the 3.3V rail
#ifndef OTTERNET_3V3_CYCLE_PIN
#define OTTERNET_3V3_CYCLE_PIN 40
#endif

// Timings (ms)

#ifndef OTTERNET_3V3_CYCLE_MS
#define OTTERNET_3V3_CYCLE_MS 1000
#endif
