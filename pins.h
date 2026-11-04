//TODO: Write all the pin definitions here
#ifndef PINS_H
#define PINS_H

#define MIDI_RX_PIN 1
#define MIDI_UART uart1
#define MIDI_UART_IRQ UART1_IRQ
#define MIDI_BAUD 31250
#define MIDI_CHANNEL (-1)
#define TFT_0_MOSI 3
#define TFT_0_RESET 4
#define TFT_0_CS 5
#define TFT_0_SCK 6
#define TFT_0_RS 7
#define TFT_1_CS 9
#define TFT_1_SCK 10
#define TFT_1_MOSI 11
#define TFT_1_RESET 12
#define TFT_1_RS 13
#define MOTOR_PWM_POS 14
#define MOTOR_PWM_NEG 15
#define MOTOR_SEL_0 16
#define MOTOR_SEL_1 17
#define MOTOR_SEL_2 18
#define MOTOR_ENABLE 19
#define RAMP_SEL 20
#define DEBUG_0 21
#define DAC_I2C i2c1
#define DAC_ADDRESS 0x60
#define DAC_SDA 22
#define DAC_SCL 23
#define DAC_SEL_0 24
#define DAC_SEL_1 25
#define DAC_SEL_2 26
#define DAC_ENABLE 27
#define PULSE_SEL 28
#define DEBUG_1 29
#define DEBUG_2 30
#define DEBUG_3 31
#define PATCH_UP 32
#define PATCH_DOWN 33
#define PATCH_LOAD 34
#define PATCH_SAVE 35
#define PATCH_TOGGLE 36
#define RAMP_RESET 37
#define RAMP_CONTROL 39
#define ANALOG_ENABLE 42
#define ANALOG_SEL_2 43
#define ANALOG_SEL_3 44
#define ANALOG_SEL_1 45
#define ANALOG_SEL_0 46
#define ANALOG_CONTROL_IN 47


#endif

int setup_pins(void){
	// Call gpio_init for all the pins listed above
	for (int i = 1; i < 40; i++){
		if (i != 2 && i != 8 && i != 38) gpio_init(i);
	}
	for (int i = 42; i < 47; i++) gpio_init(i);
	adc_gpio_init(47);

	// MIDI_IN
	// UART in, no TX (but PCB has TX just in case)
	uart_init(MIDI_UART, MIDI_BAUD);
	gpio_set_function(MIDI_RX_PIN, GPIO_FUNC_UART);
	uart_set_format(MIDI_UART, 8, 1, UART_PARITY_NONE);
	uart_set_hw_flow(MIDI_UART, 0, 0);
	uart_set_fifo_enabled(MIDI_UART, 0);
	irq_set_exclusive_handler(MIDI_UART_IRQ, midi_uart_irq);
	irq_set_enabled(MIDI_UART_IRQ, 1);
	uart_set_irq_enables(MIDI_UART, 1, 0);

	// Both TFT lines
	// SPI over DMA, except RESET, CS & RS which are GPIO_OUT
	gpio_set_dir(TFT_0_CS, GPIO_OUT);
	gpio_set_dir(TFT_0_RS, GPIO_OUT);
	gpio_set_dir(TFT_0_RESET, GPIO_OUT);
	gpio_set_function(TFT_0_MOSI, GPIO_FUNC_SPI);
	gpio_set_function(TFT_0_SCK, GPIO_FUNC_SPI);
	spi_init(spi0, 40 * 1000 * 1000); // 40MHz
	
	gpio_set_dir(TFT_1_CS, GPIO_OUT);
	gpio_set_dir(TFT_1_RS, GPIO_OUT);
	gpio_set_dir(TFT_1_RESET, GPIO_OUT);
	gpio_set_function(TFT_1_MOSI, GPIO_FUNC_SPI);
	gpio_set_function(TFT_1_SCK, GPIO_FUNC_SPI);
	spi_init(spi1, 40 * 1000 * 1000); // 40MHz

	// Motor pins
	// Two PWM pins, four GPIO_OUT pins
	// Pins 14 & 15 are different sides of the same PWM?
	gpio_set_dir(MOTOR_PWM_POS, GPIO_OUT);
	gpio_set_dir(MOTOR_PWM_NEG, GPIO_OUT);
	gpio_set_dir(MOTOR_SEL_0, GPIO_OUT);
	gpio_set_dir(MOTOR_SEL_1, GPIO_OUT);
	gpio_set_dir(MOTOR_SEL_2, GPIO_OUT);
	gpio_set_dir(MOTOR_ENABLE, GPIO_OUT);
	gpio_set_function(MOTOR_PWM_POS, GPIO_FUNC_PWM);
	gpio_set_function(MOTOR_PWM_NEG, GPIO_FUNC_PWM);

	// The DAC pins
	// Two I2C pins, four GPIO_OUT pins
	gpio_set_dir(DAC_SEL_0, GPIO_OUT);
	gpio_set_dir(DAC_SEL_1, GPIO_OUT);
	gpio_set_dir(DAC_SEL_2, GPIO_OUT);
	gpio_set_dir(DAC_ENABLE, GPIO_OUT);
	i2c_init(DAC_I2C, 100 * 1000); //TODO: How fast does this need to be? May need to change pull-up depending on this speed
	gpio_set_function(DAC_SDA, GPIO_FUNC_I2C);
	gpio_set_function(DAC_SCL, GPIO_FUNC_I2C);
	// Pull-ups unnecessary, external pull-ups at destination

	// Debug LEDs
	// All simple GPIO_OUT
	gpio_set_dir(DEBUG_0, GPIO_OUT);
	gpio_set_dir(DEBUG_1, GPIO_OUT);
	gpio_set_dir(DEBUG_2, GPIO_OUT);
	gpio_set_dir(DEBUG_3, GPIO_OUT);

	// RAMP_SEL & PULSE_SEL
	// Both simple GPIO_OUT
	gpio_set_dir(RAMP_SEL, GPIO_OUT);
	gpio_set_dir(PULSE_SEL, GPIO_OUT);

	// Patching pins; all basic GPIO_IN
	// External pull-up, no need for internal
	gpio_set_dir(PATCH_UP, GPIO_IN);
	gpio_set_dir(PATCH_DOWN, GPIO_IN);
	gpio_set_dir(PATCH_LOAD, GPIO_IN);
	gpio_set_dir(PATCH_SAVE, GPIO_IN);
	gpio_set_dir(PATCH_TOGGLE, GPIO_IN);

	// RAMP_RESET & RAMP_CONTROL
	// Both PWM pins
	gpio_set_dir(RAMP_RESET, GPIO_OUT);
	gpio_set_dir(RAMP_CONTROL, GPIO_OUT);
	gpio_set_function(RAMP_RESET, GPIO_FUNC_PWM);
	gpio_set_function(RAMP_CONTROL, GPIO_FUNC_PWM);
	
	// Analog pins
	// Five GPIO_OUT pins, one analog in
	adc_init();
	adc_select_input(7);
	gpio_set_dir(ADC_SEL_0, GPIO_OUT);
	gpio_set_dir(ADC_SEL_1, GPIO_OUT);
	gpio_set_dir(ADC_SEL_2, GPIO_OUT);
	gpio_set_dir(ADC_SEL_3, GPIO_OUT);
	gpio_set_dir(ANALOG_ENABLE, GPIO_OUT);

	return 0;
}
