#ifndef ANALOG_H
#define ANALOG_H

#include <stdint.h>
#include <stddef.h>
#include "pins.h"
#include "hardware/adc.h"
#include "hardware/irq.h"
#include "hardware/timer.h"
#include "hardware/sync.h"

// Number of logical analog inputs (max 16)
#define NUM_ANALOG_INPUTS 11

// Mux select value (0..15) for each analog setting.
static const uint8_t analog_mux_sel[NUM_ANALOG_INPUTS] = {
         0,  1,  2,  3,  4,  5,  6,  7,
         8,  9, 10
};

#define ANALOG_ENABLE_LEVEL 1 //TODO: Is the mux active-high or active-low?

// Time for the mux to settle upon switching
// Consider increasing if there's analog input issues
// TODO: Maybe this number is in the datasheet?
#define ANALOG_SETTLE_US 20

// Idle time between analog scans
#define ANALOG_SCAN_GAP_US 2000

#define ANALOG_ADC_INPUT 7

// Minimum change before a display redraw is requested. Keeps ADC
// noise from forcing constant redraws.
// TODO: Adjust as appropriate during validation
#define ANALOG_CHANGE_THRESHOLD 100

// The settings are exactly the analog inputs
#ifndef NUM_SETTINGS
#define NUM_SETTINGS NUM_ANALOG_INPUTS
#endif

// 12-bit raw ADC values (0..4095). Based only on the analog inputs.
extern volatile int g_settings[NUM_SETTINGS];

// Incremented whenever any input changes by >= ANALOG_CHANGE_THRESHOLD.
// display.h compares this against the version of its last frame.
// TODO: Would this have issues if it overflows? I think it would, but it shouldn't be an issue unless we leave it on for a very long time.
static volatile uint32_t settings_version = 0;
static int analog_reported[NUM_ANALOG_INPUTS];

// Called from the ADC IRQ each time a full scan completes
// (used by patching.h to run the motor control loop on fresh data).
static void (*volatile analog_scan_hook)(void) = NULL;

static uint analog_alarm;
static volatile uint analog_idx;   // input currently selected on the analog mux


// Select analog input idx on the mux
// Uses mask for slight optimization
static inline void analog_select(uint idx){
        uint32_t s = analog_mux_sel[idx];
        const uint64_t mask = (1ull << ANALOG_SEL_0) | (1ull << ANALOG_SEL_1) |
                              (1ull << ANALOG_SEL_2) | (1ull << ANALOG_SEL_3);
        uint64_t val = ((uint64_t)((s >> 0) & 1) << ANALOG_SEL_0) |
                       ((uint64_t)((s >> 1) & 1) << ANALOG_SEL_1) |
                       ((uint64_t)((s >> 2) & 1) << ANALOG_SEL_2) |
                       ((uint64_t)((s >> 3) & 1) << ANALOG_SEL_3);
        gpio_put_masked64(mask, val);
}

// Alarm IRQ: mux has settled, read analog input
static void analog_alarm_cb(uint alarm_num){
        (void)alarm_num;
        hw_set_bits(&adc_hw->cs, ADC_CS_START_ONCE_BITS);
}


static inline void analog_arm(uint32_t delay_us){
        // Returns true if the target already passed; in that case run the callback now
        if (hardware_alarm_set_target(analog_alarm, make_timeout_time_us(delay_us))){
                analog_alarm_cb(analog_alarm);
        }
}

// ADC FIFO IRQ: conversion finished
static void analog_adc_irq(void){
        int v = (int)adc_fifo_get();
        g_settings[analog_idx] = v;
        int delta = v - analog_reported[analog_idx];
        if (delta < 0) delta = -delta;

	// If a setting was changed, create a new version
        if (delta >= ANALOG_CHANGE_THRESHOLD){
                analog_reported[analog_idx] = v;
                settings_version++;
        }

        uint next = analog_idx + 1;
        uint32_t delay = ANALOG_SETTLE_US;
        if (next >= NUM_ANALOG_INPUTS){
                next = 0;
                delay += ANALOG_SCAN_GAP_US;
                if (analog_scan_hook) analog_scan_hook();
        }
        analog_idx = next;
        analog_select(next);
        analog_arm(delay);
}

// Call after setup_pins() (which does adc_init and the pin directions)
int setup_analog(void){
        for (int i = 0; i < NUM_ANALOG_INPUTS; i++){
                g_settings[i] = 0;
                analog_reported[i] = 0;
        }

        analog_idx = 0;
        analog_select(0);
        gpio_put(ANALOG_ENABLE, ANALOG_ENABLE_LEVEL);

        adc_select_input(ANALOG_ADC_INPUT);
        // FIFO enabled, no DMA, IRQ threshold 1 sample, no error bit, no byte shift
        adc_fifo_setup(true, false, 1, false, false);
        adc_irq_set_enabled(true);
        irq_set_exclusive_handler(ADC_IRQ_FIFO, analog_adc_irq);
        irq_set_priority(ADC_IRQ_FIFO, IRQ_PRIO_FAST);
        irq_set_enabled(ADC_IRQ_FIFO, true);

        analog_alarm = hardware_alarm_claim_unused(true);
        hardware_alarm_set_callback(analog_alarm, analog_alarm_cb);
        analog_arm(ANALOG_SETTLE_US);

        return 0;
}

#endif
