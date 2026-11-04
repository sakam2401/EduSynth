#include "pins.h"

extern int patch_num;
extern int motor_num;


int setup_patching(void){
	patch_num = 0;
	motor_num = -1;
}

void increment_patch_num_(void){
	patch_num++;
}

void decrement_patch_num(void){
	patch_num--;
}


