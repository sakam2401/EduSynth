#ifndef DISPLAY_H
#define DISPLAY_H

#include "pins.h"
#include "analog.h"
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "pico/stdlib.h"
#include "hardware/spi.h"
#include "hardware/dma.h"
#include "hardware/irq.h"
#include <stdlib.h>

typedef void (*render_fn)(uint8_t *buf, int y0, int h, const int * settings);
static void draw_waveform_band(uint8_t * buf, int y0, int h, const int * settings);
static void draw_adsr_band(uint8_t * buf, int y0, int h, const int * settings);
static void dma_irq_handler(void);
static bool frame_tick(repeating_timer_t * t);

// Removed declaration of settings; settings array is declared in analog

#define DISPLAY_0 (&displays[0])
#define DISPLAY_1 (&displays[1])
#define DISPLAY_WIDTH 240
#define DISPLAY_HEIGHT 320

// Drawing a full screen at once would push the Pi's memory limits without extremely clever tricks.
// Draw only a portion of the screen at a time, effectively in "scanlines".
#define BAND_H 16
#define BAND_BYTES (DISPLAY_WIDTH * BAND_H * 2)
#define NUM_BANDS (DISPLAY_HEIGHT / BAND_H)

typedef struct {
	spi_inst_t *spi;
	uint cs, rs, reset;
	int dma_chan;
	render_fn render;                 // draw_waveform_band or draw_adsr_band
					  // I can't wait to add a setting that swaps it every frame
					  // so it just makes the display unwatchable
	uint8_t band[2][BAND_BYTES];      //Store two "bands" of the frame at a time
	int snapshot[NUM_SETTINGS];	// Copied when a new frame is started, so no screen tearing.
	volatile int tx_band;
	volatile bool busy;
	volatile bool dirty;	// if true, then a frame is only sent when a change happens
	uint32_t seen_version; //The most recent snapshot of settings this display has seen
} display_t;

static display_t displays[2] = {
	{ .spi = spi0, .cs = TFT_0_CS, .rs = TFT_0_RS, .reset = TFT_0_RESET, .dma_chan = -1, .render = draw_waveform_band },
	{ .spi = spi1, .cs = TFT_1_CS, .rs = TFT_1_RS, .reset = TFT_1_RESET, .dma_chan = -1, .render = draw_adsr_band },
};

// Request a change of both displays
static inline void display_request_redraw(void) {
        for (int i = 0; i < 2; i++) displays[i].dirty = true;
}

// Write a command to the given display
static void write_cmd(display_t *d, uint8_t cmd) {
	gpio_put(d->rs, 0); // Command
	gpio_put(d->cs, 0);
	spi_write_blocking(d->spi, &cmd, 1);
	gpio_put(d->cs, 1);
}

// Write data to the given display
static void write_data(display_t *d, const uint8_t *data, size_t len) {
	gpio_put(d->rs, 1); // Data
	gpio_put(d->cs, 0);
	spi_write_blocking(d->spi, data, len);
	gpio_put(d->cs, 1);
}

// A helper that makes writing a single byte slightly more convenient
static void write_data_byte(display_t *d, uint8_t b) {
	write_data(d, &b, 1);
}

// Initialize the given display
static void ili9341_init(display_t *d) {
	gpio_put(d->reset, 0);
	sleep_ms(50);
	gpio_put(d->reset, 1);
	sleep_ms(150);

	write_cmd(d, 0x01); // Reset the software
	sleep_ms(150);

	write_cmd(d, 0x28); // Display OFF

	write_cmd(d, 0x3A); // Pixel format
	write_data_byte(d, 0x55); // 16 bits/pixel (RGB565)

	write_cmd(d, 0x36); // Memory access control
	write_data_byte(d, 0x48); // MX, BGR

	write_cmd(d, 0x11); // Sleep out
	sleep_ms(150);

	write_cmd(d, 0x29); // Display ON
	sleep_ms(50);
}

// Sets the window within the display
static void ili9341_set_window(display_t *d, uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1) {
	uint8_t buf[4];

	write_cmd(d, 0x2A); // Column address
	buf[0] = x0 >> 8; buf[1] = x0 & 0xFF;
	buf[2] = x1 >> 8; buf[3] = x1 & 0xFF;
	write_data(d, buf, 4);

	write_cmd(d, 0x2B); // Page address
	buf[0] = y0 >> 8; buf[1] = y0 & 0xFF;
	buf[2] = y1 >> 8; buf[3] = y1 & 0xFF;
	write_data(d, buf, 4);

	write_cmd(d, 0x2C); // Memory write
}

static int setup_display(void) {
	for (int i = 0; i < 2; i++) {
		display_t *d = &displays[i];
		// Init binary values to default high (Except RS)
		gpio_put(d->cs, 1);
		gpio_put(d->reset, 1);
		d->dma_chan = dma_claim_unused_channel(true);
		ili9341_init(d);

		dma_channel_config c = dma_channel_get_default_config(d->dma_chan);
		channel_config_set_transfer_data_size(&c, DMA_SIZE_8);
		channel_config_set_dreq(&c, spi_get_dreq(d->spi, true));
		channel_config_set_read_increment(&c, true);
		channel_config_set_write_increment(&c, false);
		dma_channel_configure(d->dma_chan, &c, &spi_get_hw(d->spi)->dr, NULL, 0, false);
		dma_channel_set_irq1_enabled(d->dma_chan, true);
		d -> seen_version = settings_version;
		d -> dirty = true;
	}

	irq_set_exclusive_handler(DMA_IRQ_1, dma_irq_handler);
	irq_set_priority(DMA_IRQ_1, IRQ_PRIO_DISPLAY); //TODO: What priority should this display be over other interrupts?
	irq_set_enabled(DMA_IRQ_1, true);

	// Add a 30Hz timer so FPS cannot exceed 30
	// TODO: Consider changing FPS cap to 60 or higher
	static repeating_timer_t timer;
	alarm_pool_t *pool = alarm_pool_create_with_unused_hardware_alarm(2);
	alarm_pool_add_repeating_timer_us(pool, -33333, frame_tick, NULL, &timer);
	return 0;
}

// Starts writing the given band to the given display
static void start_band_dma(display_t *d, int buf) {
	dma_channel_transfer_from_buffer_now(d->dma_chan, d->band[buf], BAND_BYTES);
}

// Each time a band is done transmitting...
static void display_band_done(display_t *d) {
	// Go to next band
	d->tx_band++;
	// If that was the last band...
	if (d->tx_band >= NUM_BANDS) {
		// Wait
		while (spi_is_busy(d->spi)) tight_loop_contents();
		// End pixel writing
		gpio_put(d->cs, 1);
		d->busy = false;
		return;
	}
	// Start transmitting the next band
	start_band_dma(d, d->tx_band & 1);          // pre-rendered band
	int next = d->tx_band + 1;                  // refill the buffer just freed
						    // If there's another band left, render it.
	if (next < NUM_BANDS)
		d->render(d->band[next & 1], next * BAND_H, BAND_H, d->snapshot);
}

// Waits for the DMA to finish, calls display_band_done
static void dma_irq_handler(void) {
	for (int i = 0; i < 2; i++) {
		display_t *d = &displays[i];
		uint32_t mask = 1u << d -> dma_chan;
		if (dma_hw -> ints1 & mask) {
			dma_hw -> ints1 = mask;
			display_band_done(d);
		}
	}
}

// Called once at the start of a new frame
static void display_start_frame(display_t *d) {
        // Read the version before the snapshot: a change landing in between
        // just triggers one extra redraw rather than being missed
        d->seen_version = settings_version;
        for (int i = 0; i < NUM_SETTINGS; i++) d->snapshot[i] = g_settings[i];
        d->tx_band = 0;
        d->busy = true;
        ili9341_set_window(d, 0, 0, DISPLAY_WIDTH - 1, DISPLAY_HEIGHT - 1);
        // Draw both buffers
        d->render(d->band[0], 0, BAND_H, d->snapshot);
        d->render(d->band[1], BAND_H, BAND_H, d->snapshot);
        gpio_put(d->rs, 1);
        gpio_put(d->cs, 0); // CS stays low until the whole frame is done
        start_band_dma(d, 0);
}


// When called, start a new frame
static bool frame_tick(repeating_timer_t *t) {
	uint32_t version = settings_version;
	for (int i = 0; i < 2; i++) {
		display_t *d = &displays[i];
		// If settings have not changed, don't bother
		if (version != d -> seen_version) d -> dirty = true;
		if (d -> dirty && !d -> busy) {
			d->dirty = false;
			display_start_frame(d);
		}
	}
	return true;
}


// TODO: EVERYTHING BELOW IS AN AI-GENERATED PLACEHOLDER: DO NOT USE IN FINAL PRODUCT

#define COLOR_BLACK 0x0000
#define COLOR_WHITE 0xFFFF

// r is the row within the band (0 to h-1)
static inline void band_put(uint8_t *buf, int x, int r, uint16_t color) {
	if (x < 0 || x >= DISPLAY_WIDTH) return;
	size_t i = ((size_t)r * DISPLAY_WIDTH + x) * 2;
	buf[i]     = color >> 8;
	buf[i + 1] = color & 0xFF;
}

static void band_clear(uint8_t *buf, int h, uint16_t color) {
	for (size_t i = 0; i < (size_t)DISPLAY_WIDTH * h * 2; i += 2) {
		buf[i]     = color >> 8;
		buf[i + 1] = color & 0xFF;
	}
}

// Placeholder: draws an X (both diagonals, 3 pixels wide)
static void draw_waveform_band(uint8_t * buf, int y0, int h, const int * settings) {
	band_clear(buf, h, COLOR_BLACK);
	for (int r = 0; r < h; r++) {
		int y  = y0 + r;
		int x1 = y * (DISPLAY_WIDTH - 1) / (DISPLAY_HEIGHT - 1); // top-left to bottom-right
		int x2 = (DISPLAY_WIDTH - 1) - x1;                       // top-right to bottom-left
		for (int ox = -1; ox <= 1; ox++) {
			band_put(buf, x1 + ox, r, COLOR_WHITE);
			band_put(buf, x2 + ox, r, COLOR_WHITE);
		}
	}
}

// Placeholder: draws a + (3 pixels thick)
static void draw_adsr_band(uint8_t * buf, int y0, int h, const int * settings) {
	band_clear(buf, h, COLOR_BLACK);
	for (int r = 0; r < h; r++) {
		int y = y0 + r;
		if (abs(y - DISPLAY_HEIGHT / 2) <= 1) {
			// Horizontal bar: fill the whole row
			for (int x = 0; x < DISPLAY_WIDTH; x++) band_put(buf, x, r, COLOR_WHITE);
		} else {
			// Vertical bar: three columns in every row
			for (int ox = -1; ox <= 1; ox++)
				band_put(buf, DISPLAY_WIDTH / 2 + ox, r, COLOR_WHITE);
		}
	}
}

#endif
