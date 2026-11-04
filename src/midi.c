// src/MIDI.c
//
// Minimal MIDI 1.0 input for RP2350B.

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include "pico/stdlib.h"
#include "hardware/uart.h"
#include "hardware/irq.h"
 
// ---------------------------------------------------------------- config

#define MIDI_UART uart1
#define MIDI_UART_IRQ UART1_IRQ
#define MIDI_RX_PIN 5 // GP5 = UART1 RX
#define MIDI_BAUD 31250
 
// Channel to listen on, 0..15. Set to -1 for omni (all channels).
#define MIDI_CHANNEL (-1)
 
// ---------------------------------------------------- user-facing events
// TODO: for respective oscillator drivers
static void note_on(uint8_t chan, uint8_t note, uint8_t vel)
{
    printf("NOTE ON  ch=%u note=%u vel=%u\n", chan, note, vel);
}
 
static void note_off(uint8_t chan, uint8_t note, uint8_t vel)
{
    printf("NOTE OFF ch=%u note=%u vel=%u\n", chan, note, vel);
}
 
// ------------------------------------------------------------ ring buffer
// Size must be a power of two so the mask wraps correctly.
 
#define RB_SIZE 256
#define RB_MASK (RB_SIZE - 1)
 
static volatile uint8_t rb_buf[RB_SIZE];
static volatile uint16_t rb_head = 0; // written by ISR
static volatile uint16_t rb_tail = 0; // written by main loop
 
static inline bool rb_pop(uint8_t *out)
{
    if (rb_tail == rb_head) return false;
    *out = rb_buf[rb_tail];
    rb_tail = (rb_tail + 1) & RB_MASK;
    return true;
}
 
// ---------------------------------------------------------------- ISR
 
static void midi_uart_irq(void)
{
    while (uart_is_readable(MIDI_UART)) {
        uint8_t b = (uint8_t)uart_get_hw(MIDI_UART)->dr;
        uint16_t next = (rb_head + 1) & RB_MASK;
        if (next != rb_tail) {
            rb_buf[rb_head] = b;
            rb_head = next;
        }
        // byte is dropped rather than overwriting unread data.
    }
}
 
// --------------------------------------------------------------- parser
//
// Handles:
//   running status           (status byte omitted on repeated messages)
//   Note On with velocity 0  (treated as Note Off, per the spec)
//   real-time bytes F8..FF   (may appear mid-message, ignored here)
//   system common F1..F7     (cancels running status)
//   SysEx F0..F7             (skipped)
 
static uint8_t  p_status = 0;   // current running status byte, 0 = none
static uint8_t  p_data[2];
static uint8_t  p_count = 0;   // data bytes collected so far
static uint8_t  p_expect = 0;   // data bytes this message needs
static bool     p_in_sysex = false;
 
static uint8_t data_bytes_for(uint8_t status)
{
    switch (status & 0xF0) {
        case 0x80:  // Note Off
        case 0x90:  // Note On
        case 0xA0:  // Poly Aftertouch
        case 0xB0:  // Control Change
        case 0xE0:  // Pitch Bend
            return 2;
        case 0xC0:  // Program Change
        case 0xD0:  // Channel Aftertouch
            return 1;
        default:
            return 0;
    }
}
 
static void dispatch(void)
{
    uint8_t type = p_status & 0xF0;
    uint8_t chan = p_status & 0x0F;
 
    if (MIDI_CHANNEL >= 0 && chan != (uint8_t)MIDI_CHANNEL) return;
 
    if (type == 0x90) {
        if (p_data[1] == 0) note_off(chan, p_data[0], 0);
        else                note_on(chan, p_data[0], p_data[1]);
    } else if (type == 0x80) {
        note_off(chan, p_data[0], p_data[1]);
    }
}
 
static void midi_parse_byte(uint8_t b)
{
    // Real-time messages are single bytes and may interrupt any other
    // message, including SysEx. They never disturb running status.
    if (b >= 0xF8) return;
 
    if (b & 0x80) {                     // status byte
        if (b == 0xF7) {                // end of SysEx
            p_in_sysex = false;
            return;
        }
        if (b == 0xF0) {                // start of SysEx
            p_in_sysex = true;
            p_status = 0;
            p_count = 0;
            return;
        }
        p_in_sysex = false;
 
        if (b >= 0xF1 && b <= 0xF6) {   // system common cancels running status
            p_status = 0;
            p_count = 0;
            return;
        }
 
        p_status = b;
        p_expect = data_bytes_for(b);
        p_count  = 0;
        return;
    }
 
    // data byte
    if (p_in_sysex) return;
    if (p_status == 0) return; // data with no status, discard
 
    p_data[p_count++] = b;
    if (p_count >= p_expect) {
        dispatch();
        p_count = 0; // stay armed for running status
    }
}
 
// ------------------------------------------------------------ public API
 
void midi_init(void)
{
    uart_init(MIDI_UART, MIDI_BAUD);
    gpio_set_function(MIDI_RX_PIN, GPIO_FUNC_UART);
 
    uart_set_format(MIDI_UART, 8, 1, UART_PARITY_NONE);
    uart_set_hw_flow(MIDI_UART, false, false);
 
    // 31250 baud a byte takes
    // 320 us, so the interrupt rate is low and latency is minimal.
    uart_set_fifo_enabled(MIDI_UART, false);
 
    irq_set_exclusive_handler(MIDI_UART_IRQ, midi_uart_irq);
    irq_set_enabled(MIDI_UART_IRQ, true);
    uart_set_irq_enables(MIDI_UART, true, false);   // RX only, no TX
}
 
// Call this repeatedly from the main loop.
void midi_task(void)
{
    uint8_t b;
    while (rb_pop(&b)) midi_parse_byte(b);
}
 
// ------------------------------------------------------------ entry point
 
#ifdef PROTON
 
// framework = arduino. The core supplies main() and calls these.
// Serial here is USB CDC; printf() in the handlers above still routes
// through the SDK's stdio, so call Serial.begin() only if you switch the
// handlers over to Serial.printf().
 
void setup(void)
{
    midi_init();
}
 
void loop(void)
{
    midi_task();
    // oscillator update code goes here
}
 
#else
 
// framework = baremetal (pico-sdk).
 
int main(void)
{
    stdio_init_all();
    midi_init();
 
    while (true) {
        midi_task();
        // oscillator update code goes here
    }
}

#endif