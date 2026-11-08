#include "pins.h"

// Example code for later use:
// uint8_t buf[] = {0x00, 0x01};
// int ret = i2c_write_blocking(I2C_PORT, DAC_ADDRESS, buf, sizeof(buf), false);
// ret is bytes written, or PICO_ERROR_GENERIC if no ACK


int write_filter_cutoff(char value){
	gpio_put(DAC_SEL_0, 1);
	gpio_put(DAC_SEL_1, 1);
	gpio_put(DAC_SEL_2, 0);
	gpio_put(DAC_ENABLE, 0);
	//TODO: Verify MCP4706 data format
	int ret = i2c_write_blocking(DAC_I2C, DAC_ADDRESS, &value, sizeof(value), false);
	gpio_put(DAC_ENABLE, 1);
	if (ret < 0) return 1; // Negative value when there's an error
	return 0;
}

int write_filter_resonance(char value){
        gpio_put(DAC_SEL_0, 1); //TODO: Uh-oh
        gpio_put(DAC_SEL_1, 0);
        gpio_put(DAC_SEL_2, 1);
        gpio_put(DAC_ENABLE, 0);
        //TODO: Verify MCP4706 data format
        int ret = i2c_write_blocking(DAC_I2C, DAC_ADDRESS, &value, sizeof(value), false);
        gpio_put(DAC_ENABLE, 1);
        if (ret < 0) return 1; // Negative value when there's an error
        return 0;
}

int write_vca_level(char value){
        gpio_put(DAC_SEL_0, 0); //TODO: Uh-oh
        gpio_put(DAC_SEL_1, 0);
        gpio_put(DAC_SEL_2, 0);
        gpio_put(DAC_ENABLE, 0);
        //TODO: Verify MCP4706 data format
        int ret = i2c_write_blocking(DAC_I2C, DAC_ADDRESS, &value, sizeof(value), false);
        gpio_put(DAC_ENABLE, 1);
        if (ret < 0) return 1; // Negative value when there's an error
        return 0;
}
