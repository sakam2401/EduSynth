// Team-written includes
#include "pins.h"
#include "analog.h"
#include "patching.h"
#include "display.h"

// Variables
volatile uint16_t g_settings[NUM_ANALOG_INPUTS];
volatile int patch_num;
volatile int motor_num;

// Function declarations
int setup(void);
int setup_pins(void);

int setup(void){
	if (setup_pins()) return 1;
	if (setup_patching()) return 2;
	if (setup_display()) return 3;
	if (setup_analog()) return 4;
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
