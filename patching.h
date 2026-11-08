#ifndef PATCHING_H
#define PATCHING_H

#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include "pins.h"
#include "analog.h"
#include "hardware/pwm.h"
#include "hardware/clocks.h"
#include "hardware/gpio.h"
#include "hardware/irq.h"
#include "hardware/timer.h"

#define NUM_PATCHES 10
#define NUM_MOTORS  8
#define POSITION_UNSET 0xFFFFu //Used to mark if a patch settings is not yet set

// Describes which setting is driven by which motor
static const uint8_t motor_input[NUM_MOTORS] = { 0, 1, 2, 3, 4, 5, 6, 7 };

#define MOTOR_ENABLE_LEVEL  1 //TODO: Was the enable active-high or active-low?
#define MOTOR_DIR_INVERT    0 // Set to 1 if the motors go the wrong way
#define MOTOR_PWM_FREQ_HZ   20000 //Was 1khz in patching but apparently it's abnormal for it to be that low
#define MOTOR_THRESHOLD 225
#define MOTOR_TIMEOUT_SCANS 400

#define PATCHING_ENABLED_AT_BOOT true // Set to false if we find power-on patching issues
#define BUTTON_DEBOUNCE_US  25000 

extern volatile int patch_num;          // 0..NUM_PATCHES-1
extern volatile int motor_num;          // motor currently being driven, -1 if idle

uint16_t patch_positions[NUM_PATCHES][NUM_MOTORS];

static volatile bool patching_enabled = PATCHING_ENABLED_AT_BOOT;
static uint16_t load_targets[NUM_MOTORS];
static uint32_t motor_wrap;
static uint32_t motor_timeout;

//Convenience for much later
static const uint patch_buttons[5] = {PATCH_UP, PATCH_DOWN, PATCH_LOAD, PATCH_SAVE, PATCH_TOGGLE};

// Set the motor
static void motor_set(int speed){
        if (speed > 100) speed = 100; //If we're really struggling to optimize the code, remove the clamping
        else if (speed < -100) speed = -100;
        if (MOTOR_DIR_INVERT) speed = -speed;
 
        uint32_t level = ((uint32_t)abs(speed) * motor_wrap) / 100;
        if (speed >= 0){
                pwm_set_gpio_level(MOTOR_PWM_NEG, 0);
                pwm_set_gpio_level(MOTOR_PWM_POS, level);
        } else {
                pwm_set_gpio_level(MOTOR_PWM_POS, 0);
                pwm_set_gpio_level(MOTOR_PWM_NEG, level);
        }
}


// Stop the motor
static void motor_stop_all(void){
        motor_set(0);
        gpio_put(MOTOR_ENABLE, !MOTOR_ENABLE_LEVEL);
}

// Stop the motor, switch motor, start the next motor
static void motor_start(int m){
        motor_stop_all();
        gpio_put_masked((1u << MOTOR_SEL_0) | (1u << MOTOR_SEL_1) | (1u << MOTOR_SEL_2),
                        (((uint32_t)m >> 0 & 1) << MOTOR_SEL_0) |
                        (((uint32_t)m >> 1 & 1) << MOTOR_SEL_1) |
                        (((uint32_t)m >> 2 & 1) << MOTOR_SEL_2));
        gpio_put(MOTOR_ENABLE, MOTOR_ENABLE_LEVEL);
        motor_timeout = MOTOR_TIMEOUT_SCANS;
        motor_num = m;
}

// Goes through the motors and moves all the ones that are not where they should be
// It's basically gaurenteed to activate for every motor, but only for a moment
static void load_advance(int from){
        for (int m = from; m < NUM_MOTORS; m++){
                if (load_targets[m] != POSITION_UNSET){
                        motor_start(m);
                        return;
                }
        }
        motor_stop_all();
        motor_num = -1;
}

// Patch loaded -> call ADC measurement -> call this function
// Checks the difference between the currently focused motor and the intended settings
// Sets the motor if difference is too high
// Moves on to the next motor eventually
static void patching_scan_hook(void){
        if (motor_num < 0) return;
 
        int diff = (int)g_settings[motor_input[motor_num]] - (int)load_targets[motor_num];
 
        if (diff > MOTOR_THRESHOLD){
                motor_set(100);
        } else if (-diff > MOTOR_THRESHOLD){
                motor_set(-100);
        } else {
                // Arrived: stop and move on to the next motor
                load_advance(motor_num + 1);
                return;
        }
 
        // Motor timed out, move on
	// (Should be very unlikely or impossible)
	// Prototyping showed that we didn't really need this,
	// but I think we listed it as a requirement at some point
        if (--motor_timeout == 0) load_advance(motor_num + 1);
}

void increment_patch_num(void){
        patch_num = (patch_num + 1) % NUM_PATCHES;
}

void decrement_patch_num(void){
        patch_num = (patch_num + NUM_PATCHES - 1) % NUM_PATCHES;
}

void patch_save(int p){
        for (int m = 0; m < NUM_MOTORS; m++){
                patch_positions[p][m] = g_settings[motor_input[m]];
        }
}

void patch_load(int p){
        for (int m = 0; m < NUM_MOTORS; m++) load_targets[m] = patch_positions[p][m];
        load_advance(0);
}

// The RP only has one GPIO interrupt core, but apparently UART interrupt
// doesn't count, so I think this file is the only one that needs to handle
// GPIO interrupts. 
// The function below activates when ANY of the buttons are pressed,
// so it has to work out which button was pressed
static void patch_gpio_cb(uint gpio, uint32_t events){
        (void)events;
        static uint32_t last_us[5];

	// Double check just to make sure no other GPIO interrupt activated this unintentionally
        int i;
        for (i = 0; i < 5; i++) if (patch_buttons[i] == gpio) break;
        if (i == 5) return;

	// Debouce
        uint32_t now = time_us_32();
        if ((uint32_t)(now - last_us[i]) < BUTTON_DEBOUNCE_US) return;
        last_us[i] = now;

        if (gpio == PATCH_TOGGLE){
                patching_enabled = !patching_enabled;
                return;
        }
        if (!patching_enabled) return;

        switch (gpio){
	        case PATCH_UP:   increment_patch_num(); break;
        	case PATCH_DOWN: decrement_patch_num(); break;
	        case PATCH_SAVE: if (motor_num < 0) patch_save(patch_num); break;
        	case PATCH_LOAD: if (motor_num < 0) patch_load(patch_num); break;
        }
}

int setup_patching(void){
        patch_num = 0;
        motor_num = -1;

        for (int p = 0; p < NUM_PATCHES; p++)
                for (int m = 0; m < NUM_MOTORS; m++)
                        patch_positions[p][m] = POSITION_UNSET;

        // Motor PWM: pins 14/15 are channels A/B of the same slice
	// They pulse at the same time, but with differing widths
        uint slice = pwm_gpio_to_slice_num(MOTOR_PWM_POS);
        motor_wrap = clock_get_hz(clk_sys) / MOTOR_PWM_FREQ_HZ - 1;
        pwm_config cfg = pwm_get_default_config();
        pwm_config_set_wrap(&cfg, motor_wrap);
        pwm_init(slice, &cfg, true);
        motor_stop_all();

        // Motor control step rides on the analog scan
        analog_scan_hook = patching_scan_hook;

        // Buttons: only one GPIO IRQ callback exists per core, so if other code
        // also uses GPIO IRQs, merge its handling into patch_gpio_cb.
        gpio_set_irq_callback(&patch_gpio_cb);
        for (int i = 0; i < 5; i++){
                gpio_set_irq_enabled(patch_buttons[i], GPIO_IRQ_EDGE_FALL, true);
        }
	irq_set_priority(IO_IRQ_BANK0, IRQ_PRIO_FAST);
        irq_set_enabled(IO_IRQ_BANK0, true);

        return 0;
}

#endif
