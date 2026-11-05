#pragma once

// Team-written includes
#include "pins.h"

// Function declarations
int setup(void);
int setup_pins(void);
int setup_interrupts(void);

int setup(void){
	if (setup_pins()) return 1;
	if (setup_interrupts()) return 2;
	if (setup_patching()) return 3;
	if (setup_display()) return 4;
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
