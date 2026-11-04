#include "pins.h"

int setup_display(void){
	//TODO: Do a bunch of stuff specific to the ili9341
	dma_chan = dma_claim_unused_channel(true);

}

int draw_waveform(char * image_buffer, int * settings){
	//TODO: Modify the buffer based on the settings
	return 0;
}

int draw_adsr(char * image_buffer, int * settings){
	//TODO: Modify the buffer based on the settings
	return 0;
}
