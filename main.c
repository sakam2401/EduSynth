// Non-team written includes
#include "hardware/uart.h"
#include "hardware/spi.h"
#include "pico/stdlib.h"
#include "hardware/gpio.h"
#include "hardware/pwm.h"
#include "hardware/spi.h"
#include "hardware/dma.h"

// Team-written includes
#include "pins.h"

// Function declarations
int setup(void);
int setup_pins(void);
int setup_interrupts(void);

int setup(void){
	if (setup_pins()) return 1;
	if (setup_interrupts()) return 2;
	if (patching_setup()) return 3;
	return 0;
}

int main(void){
	int error = setup();
	if (error){
		return error;
	}

	for (;;){
		__wfi();
	}


	return 0;
}
