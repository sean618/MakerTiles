
#include "project.h"

#ifdef ENABLE_LED_MODULE

#include <stdint.h>
#include <stdio.h>
#include <math.h>
#include <string.h>
#include "FieldProtocol/fpCommon.hpp"
#include "ledStrip.h"
#include "myAssert.h"  // myAssert macro
#include "useful.h"    // MIN/MAX


// Timer: (800Khz)
//     Ensure there is a clk source
//     PWM generation on channel
//     Prescalar: 0
//     Counter period: 59 (e.g. 48MHz/800KHz = 60)
//     Auto-reload preload enabled
//     PWM mode 2
//     DMA - TIM2_CH2, memory to peripheral, circular, word, word
//     Enable TIM2 global interrupt - priority medium

#ifndef LED_TIMER_CLK_FREQUENCY
	#define LED_TIMER_CLK_FREQUENCY 48000000
#endif
#ifndef LED_STRIP_MAX_LEDS
	#define LED_STRIP_MAX_LEDS 1000 //350
#endif

#define LED_CLK_FREQUENCY 800000 // 1.25us per bit
#define COUNTER_PERIOD (LED_TIMER_CLK_FREQUENCY/LED_CLK_FREQUENCY) // 80

// WS2812E - (0) high: 300 (220-380), low:  790 (580-1000)
// WS2812E - (1) low:  300 (220-380), high: 790 (580-1000)
// WS2812E - reset: 280us

// WS2812B - (0) high: 400 (250-550), low:  850 (700-1000)
// WS2812B - (1) low:  450 (300-600), high: 800 (650-950)
// WS2812E - reset: >50us

// Combined: 300-380, 700-950 => 340+825 => 1165 (1250 ideal +/- 600)
//           350, 850 => 
//           22, 58 => 345, 906

#define ONE_HIGH_PERIOD   (58) // ((2 * COUNTER_PERIOD) / 3) // High for first 2/3 of period, High for ~416ns then low for ~833ns
#define ZERO_HIGH_PERIOD  (22) // ((1 * COUNTER_PERIOD) / 3) // High for first 1/3 of period, High for ~833ns then low for ~416ns

// #define MAX_CURRENT_PER_RGB_LED_MA_x8 91 // 11.4mA for ws2812b eco
// #define FIXED_CURRENT_PER_LED_MA_x8 11     // 1.32mA for ws2812b eco
#define MAX_CURRENT_PER_RGB_LED_MA_x8 152 // ~19mA for ws2812b
#define FIXED_CURRENT_PER_LED_MA_x8 11     // 1.32mA for ws2812b


// Reset period for new panels is 50us but for old it's was longer
#define RESET_COUNT 15 // x 30us - Number of LED dma's - e.g. number of 24 bit cycles

// 24Mhz - period 41.66ns
// 1H_clocks - 33
// 1L_clocks - 19
// 0H_clocks - 17
// 0L_clocks - 35

#define ONE_LED_DMA_LENGTH 24 // 3 colours of 8 bits each
#define NUM_LEDS_PER_DMA (2*8)
#define FULL_DMA_BUFFER_LENGTH (NUM_LEDS_PER_DMA * ONE_LED_DMA_LENGTH)
#define PIXEL_BUFFER_LENGTH (3 * LED_STRIP_MAX_LEDS) // Limited by the amount of ram we have 

typedef struct {
	uint8_t red;
	uint8_t green;
	uint8_t blue;
} colourStruct;

typedef struct {
	bool initialised;
	bool afterCanInitialised;
	bool testing;
	TIM_HandleTypeDef * tim;
	GPIO_TypeDef* testGpioX;
	uint16_t testGpioPin;
	uint32_t timChannel;
	// Config
	uint16_t numLeds;
	uint16_t panelWidth;
	uint8_t brightness;
	uint8_t userBrightness;
	uint16_t refreshPeriodMs;
	uint16_t maxCurrentMa;
	// Gamma correction
	uint8_t gammaCorrectionEnabled;
	uint8_t redAdjust;
	uint8_t greenAdjust;
	uint8_t blueAdjust;
	// This is a generic store for led colours 
	uint8_t pixelBuffer[PIXEL_BUFFER_LENGTH];
	// DMA state
	uint16_t dmaCount; // This count controls the current LED programming sequence
	// Main loop State
	uint32_t ticksSinceLastShow;
	uint16_t lastEstimatedCurrentMa;
	uint8_t exceededCurrent;
	bool show;
} ledStripDriver;

static ledStripDriver ledDrv = {};

static uint8_t ledDmaBuffer[FULL_DMA_BUFFER_LENGTH];


static void showLedData();
static bool setShowField(fp::FieldEntry * field, fp::FieldIndex fieldIndex, fp::FieldIndex numFields, uint8_t * data) {
	showLedData();
	return true;
}

static void setUserBrightness(uint8_t value) {
	uint16_t newBrightness = MIN(value, 100);
	ledDrv.userBrightness = newBrightness;
	ledDrv.brightness = (255 * newBrightness) / 100;
}

static bool setUserBrightnessField(fp::FieldEntry * field, fp::FieldIndex fieldIndex, fp::FieldIndex numFields, uint8_t * data) {
	setUserBrightness(*data);
	return true;
}

fp::FieldEntry ledStripFields[] = {
    // ptr                          ,  name             ,          span,                  type,                       size,   flags,                                               setFieldFn,              getFieldFn, units
	{ &ledDrv.userBrightness        , "brightness",                1,                  fp::FieldDataType::Uint,     1,      fp::FieldFlags::Gettable | fp::FieldFlags::Settable,  &setUserBrightnessField, nullptr,  ""},
	{ &ledDrv.refreshPeriodMs       , "refresh_period_ms",         1,                  fp::FieldDataType::Uint,     2,      fp::FieldFlags::Gettable | fp::FieldFlags::Settable,  nullptr,                 nullptr,  ""},
	{ &ledDrv.pixelBuffer[0]        , "colours",                   LED_STRIP_MAX_LEDS, fp::FieldDataType::Uint,     3,      fp::FieldFlags::Gettable | fp::FieldFlags::Settable,  nullptr,                 nullptr,  ""},
	{ nullptr                       , "show",                      1,                  fp::FieldDataType::Boolean,  1,      fp::FieldFlags::Settable,                            &setShowField,           nullptr,  ""},
	{ &ledDrv.lastEstimatedCurrentMa, "current",                   1,                  fp::FieldDataType::Uint,     2,      fp::FieldFlags::Gettable,                            nullptr,                 nullptr,  ""},
	{ &ledDrv.maxCurrentMa          , "max_current",               1,                  fp::FieldDataType::Uint,     2,      fp::FieldFlags::Gettable | fp::FieldFlags::Settable,  nullptr,                 nullptr,  ""},
#if (FIXED_NUM_LEDS == 0)
	{ &ledDrv.numLeds               , "num_leds",                  1,                  fp::FieldDataType::Uint,     2,      fp::FieldFlags::Gettable | fp::FieldFlags::Settable,  nullptr,                 nullptr,  ""},
	{ &ledDrv.panelWidth            , "serpentine_panel_width",    1,                  fp::FieldDataType::Uint,     2,      fp::FieldFlags::Gettable | fp::FieldFlags::Settable,  nullptr,                 nullptr,  ""},
#endif
};

const fp::FieldTable ledStripFieldTable = {
    ledStripFields,
    sizeof(ledStripFields)/sizeof(fp::FieldEntry)
};


// =========================================== //

static void fillDMABufferWithColourComponent(uint8_t * buffer, uint8_t colourComponent) {
	// Seems seem to take 120 cycles to execute (on STM32H7 at 192Mhz)
	buffer[0] = ZERO_HIGH_PERIOD;
	buffer[1] = ZERO_HIGH_PERIOD;
	buffer[2] = ZERO_HIGH_PERIOD;
	buffer[3] = ZERO_HIGH_PERIOD;
	buffer[4] = ZERO_HIGH_PERIOD;
	buffer[5] = ZERO_HIGH_PERIOD;
	buffer[6] = ZERO_HIGH_PERIOD;
	buffer[7] = ZERO_HIGH_PERIOD;
	if (colourComponent & 0x80)
		buffer[0] = ONE_HIGH_PERIOD;
	if (colourComponent & 0x40)
		buffer[1] = ONE_HIGH_PERIOD;
	if (colourComponent & 0x20)
		buffer[2] = ONE_HIGH_PERIOD;
	if (colourComponent & 0x10)
		buffer[3] = ONE_HIGH_PERIOD;
	if (colourComponent & 0x08)
		buffer[4] = ONE_HIGH_PERIOD;
	if (colourComponent & 0x04)
		buffer[5] = ONE_HIGH_PERIOD;
	if (colourComponent & 0x02)
		buffer[6] = ONE_HIGH_PERIOD;
	if (colourComponent & 0x01)
		buffer[7] = ONE_HIGH_PERIOD;
}

// ========================================== //

// sRGB - power = 2.4
// Without adding 
#define GCO 1 // Offset - add 1 as for low values the LED's are unlit which is confusing and often looks strange
// put into RAM for performance
// __attribute__ ((section(".data")))
uint8_t gammaCorrectionTable[256] = {
	0, 0+GCO, 0+GCO, 0+GCO, 0+GCO, 0+GCO, 0+GCO, 1+GCO, 1+GCO, 1+GCO, 1+GCO, 1+GCO, 1+GCO, 1+GCO, 1+GCO, 1+GCO,
	1+GCO, 1+GCO, 2+GCO, 2+GCO, 2+GCO, 2+GCO, 2+GCO, 2+GCO, 2+GCO, 2+GCO, 3+GCO, 3+GCO, 3+GCO, 3+GCO, 3+GCO, 3+GCO,
	4+GCO, 4+GCO, 4+GCO, 4+GCO, 4+GCO, 5+GCO, 5+GCO, 5+GCO, 5+GCO, 6+GCO, 6+GCO, 6+GCO, 6+GCO, 7+GCO, 7+GCO, 7+GCO,
	8+GCO, 8+GCO, 8+GCO, 8+GCO, 9+GCO, 9+GCO, 9+GCO, 10+GCO, 10+GCO, 10+GCO, 11+GCO, 11+GCO, 12+GCO, 12+GCO, 12+GCO, 13+GCO,
	13+GCO, 13+GCO, 14+GCO, 14+GCO, 15+GCO, 15+GCO, 16+GCO, 16+GCO, 17+GCO, 17+GCO, 17+GCO, 18+GCO, 18+GCO, 19+GCO, 19+GCO, 20+GCO,
	20+GCO, 21+GCO, 22+GCO, 22+GCO, 23+GCO, 23+GCO, 24+GCO, 24+GCO, 25+GCO, 25+GCO, 26+GCO, 27+GCO, 27+GCO, 28+GCO, 29+GCO, 29+GCO,
	30+GCO, 30+GCO, 31+GCO, 32+GCO, 32+GCO, 33+GCO, 34+GCO, 35+GCO, 35+GCO, 36+GCO, 37+GCO, 37+GCO, 38+GCO, 39+GCO, 40+GCO, 41+GCO,
	41+GCO, 42+GCO, 43+GCO, 44+GCO, 45+GCO, 45+GCO, 46+GCO, 47+GCO, 48+GCO, 49+GCO, 50+GCO, 51+GCO, 51+GCO, 52+GCO, 53+GCO, 54+GCO,
	55+GCO, 56+GCO, 57+GCO, 58+GCO, 59+GCO, 60+GCO, 61+GCO, 62+GCO, 63+GCO, 64+GCO, 65+GCO, 66+GCO, 67+GCO, 68+GCO, 69+GCO, 70+GCO,
	71+GCO, 72+GCO, 73+GCO, 74+GCO, 76+GCO, 77+GCO, 78+GCO, 79+GCO, 80+GCO, 81+GCO, 82+GCO, 84+GCO, 85+GCO, 86+GCO, 87+GCO, 88+GCO,
	90+GCO, 91+GCO, 92+GCO, 93+GCO, 95+GCO, 96+GCO, 97+GCO, 99+GCO, 100+GCO, 101+GCO, 103+GCO, 104+GCO, 105+GCO, 107+GCO, 108+GCO, 109+GCO,
	111+GCO, 112+GCO, 114+GCO, 115+GCO, 116+GCO, 118+GCO, 119+GCO, 121+GCO, 122+GCO, 124+GCO, 125+GCO, 127+GCO, 128+GCO, 130+GCO, 131+GCO, 133+GCO,
	134+GCO, 136+GCO, 138+GCO, 139+GCO, 141+GCO, 142+GCO, 144+GCO, 146+GCO, 147+GCO, 149+GCO, 151+GCO, 152+GCO, 154+GCO, 156+GCO, 157+GCO, 159+GCO,
	161+GCO, 163+GCO, 164+GCO, 166+GCO, 168+GCO, 170+GCO, 171+GCO, 173+GCO, 175+GCO, 177+GCO, 179+GCO, 181+GCO, 183+GCO, 184+GCO, 186+GCO, 188+GCO,
	190+GCO, 192+GCO, 194+GCO, 196+GCO, 198+GCO, 200+GCO, 202+GCO, 204+GCO, 206+GCO, 208+GCO, 210+GCO, 212+GCO, 214+GCO, 216+GCO, 218+GCO, 220+GCO,
	222+GCO, 224+GCO, 226+GCO, 229+GCO, 231+GCO, 233+GCO, 235+GCO, 237+GCO, 239+GCO, 242+GCO, 244+GCO, 246+GCO, 248+GCO, 250+GCO, 253+GCO, 255,
};
//static uint8_t gammaCorrection(uint16_t colourComponent) {
//	// hardAssert(colourComponent < 256, "Values above 255 not supported");
//	return gammaCorrectionTable[colourComponent];
//}

//// TODO: I never got the gamma correction right
//static void fullGammaCorrect(colourStruct * colour) {
////	uint16_t desiredBrightness = (colour->red + colour->green + colour->blue);
//
//	colour->red   = (gammaCorrection(colour->red  ) * ledDrv.redAdjust)/256;
//	colour->green = (gammaCorrection(colour->green) * ledDrv.greenAdjust)/256;
//	colour->blue  = (gammaCorrection(colour->blue ) * ledDrv.blueAdjust)/256;
//
//	// This adds about 2.5us
//	// The gamma correction functions only handle up to 255 - so have to do something special when sum is greater than 255
//	// uint16_t actualBrightness;
//	// uint16_t sum = colour->red + colour->green + colour->blue;
//	// if (sum > 255) {
//	// 	actualBrightness = (350 * gammaUncorrection(sum / 2)) / 256;
//	// } else {
//	// 	actualBrightness = gammaUncorrection(sum);
//	// }
//	// uint16_t ratio = (256 * desiredBrightness) / actualBrightness;
//	// colour->red   = (ratio * colour->red  ) / 256;
//	// colour->green = (ratio * colour->green) / 256;
//	// colour->blue  = (ratio * colour->blue ) / 256;
//}

// ========================================== //


static void setBuffer(uint16_t index, colourStruct colour) {
	if (index >= LED_STRIP_MAX_LEDS) {
		myAssert(0, "");
		return;
	}
	ledDrv.pixelBuffer[index * 3 + 0] = colour.blue;
	ledDrv.pixelBuffer[index * 3 + 1] = colour.green;
	ledDrv.pixelBuffer[index * 3 + 2] = colour.red;
}

// ========================================== //
// Fill DMA buffers with the correct pixel from the pixelBuffer

// Each LED takes 30us to program - so we need to calculate each LED in much less than 30us
// Currently takes 5.4us on 64Mhz G0
static void setupNextLedDma(uint8_t * buffer) {
	static uint32_t current = 0;
	static uint32_t maxCurrent = 0;
	uint32_t numLeds = ledDrv.numLeds;
	uint32_t ledIndex = ledDrv.dmaCount - RESET_COUNT;
	
	if (ledDrv.dmaCount < RESET_COUNT) {
		if (ledDrv.dmaCount == 0) {
			current = (FIXED_CURRENT_PER_LED_MA_x8 * numLeds * 256) / MAX_CURRENT_PER_RGB_LED_MA_x8;
			maxCurrent = (ledDrv.maxCurrentMa * 256 * 8) / MAX_CURRENT_PER_RGB_LED_MA_x8;
		}
		memset(buffer, 0, ONE_LED_DMA_LENGTH * sizeof(ledDmaBuffer[0]));

	} else if (ledIndex < numLeds) {
		// Find the pixel crossponding to the led
		// The LED panels have every other rows flipped (because it snakes)
		uint32_t pixelIndex = ledIndex;
		uint32_t ledDown = ledIndex / ledDrv.panelWidth; // TODO: kind of expensive to do this integer divide
		uint32_t ledAcross = ledIndex % ledDrv.panelWidth;
		bool rowFlipped = (ledDown % 2 == 1);
		if (rowFlipped) {
			pixelIndex = (ledIndex + ledDrv.panelWidth - 1 - (2 * ledAcross));
		}

		// Do brightness and gamma correction here because the operation is lossy - this way the raw colours remain intact			
		// Could use a separate buffer to store gamma corrected, brightness corrected values?
		uint32_t brightness = ledDrv.brightness;
		uint8_t * pixelBuffer = &ledDrv.pixelBuffer[pixelIndex * 3];
		uint32_t blue  = (pixelBuffer[0] * brightness)/256;
		uint32_t green = (pixelBuffer[1] * brightness)/256;
		uint32_t red   = (pixelBuffer[2] * brightness)/256;

		if (ledDrv.gammaCorrectionEnabled) {
			red   = (gammaCorrectionTable[red  ] * ledDrv.redAdjust  )/256;
			green = (gammaCorrectionTable[green] * ledDrv.greenAdjust)/256;
			blue  = (gammaCorrectionTable[blue ] * ledDrv.blueAdjust )/256;
		}
		
		fillDMABufferWithColourComponent(&buffer[0], green);
		fillDMABufferWithColourComponent(&buffer[8], red);
		fillDMABufferWithColourComponent(&buffer[16], blue);

		// Update the expected current
		current += (red + green + blue);
		// Dim the LED strip if max current exceeded 
		if (current > maxCurrent) {
			ledDrv.exceededCurrent = 1;
			setUserBrightness(5);
			ledDrv.dmaCount = 0; // Go back to the beginning and set use the lower brightness
		}
	} else {
		ledDrv.lastEstimatedCurrentMa = (MAX_CURRENT_PER_RGB_LED_MA_x8 * current ) / (256 * 8);
		memset(buffer, 0, ONE_LED_DMA_LENGTH * sizeof(ledDmaBuffer[0]));
		if (ledDrv.dmaCount >= (numLeds + RESET_COUNT + RESET_COUNT)) {
			ledDrv.show = false;
		}
	}
	ledDrv.dmaCount++;
}

void ledStrip_TIM_PWM_PulseFinishedCallback() {
	if (!ledDrv.show)
		return;
	if (ledDrv.testing)
		return;
	// Finished using the second half of the buffer so we can now overwrite it with the next lot of data

	for (uint32_t l=0; l<NUM_LEDS_PER_DMA/2; l++) {
		setupNextLedDma(&ledDmaBuffer[FULL_DMA_BUFFER_LENGTH/2 + l*ONE_LED_DMA_LENGTH]);
	}
}
void ledStrip_TIM_PWM_PulseFinishedHalfCpltCallback() {
	if (!ledDrv.show)
		return;
	if (ledDrv.testing)
		return;
	// Finished using the first half of the buffer so we can now overwrite it with the next lot of data
	for (uint32_t l=0; l<NUM_LEDS_PER_DMA/2; l++) {
		setupNextLedDma(&ledDmaBuffer[0 + l*ONE_LED_DMA_LENGTH]);
	}
}

static void showLedData() {
	// This will start the DMA reset loop
	ledDrv.dmaCount = 0;
	ledDrv.show = true;
	ledDrv.ticksSinceLastShow = HAL_GetTick();
}

static void initialiseLedDma() {
	// The DMA will run continuously - we never stop it we just control it's output
	HAL_StatusTypeDef status = HAL_TIM_PWM_Start_DMA(ledDrv.tim, ledDrv.timChannel, (uint32_t *) &ledDmaBuffer[0], FULL_DMA_BUFFER_LENGTH);
	myAssert(status == HAL_OK, "");
}

uint8_t selfTestLedStrip() {
	uint8_t failed = 0;
	ledDrv.testing = 1; // stop DMA buffer being set by the callback
	// Write dma all 0's and check output
	HAL_Delay(1);
	memset(ledDmaBuffer, 0, FULL_DMA_BUFFER_LENGTH * sizeof(ledDmaBuffer[0]));
	HAL_Delay(1);
	GPIO_PinState state = HAL_GPIO_ReadPin(ledDrv.testGpioX, ledDrv.testGpioPin);
	failed |= (state != GPIO_PIN_RESET);
	myAssert(!failed, "");

	// Write dma all 1's and check output
	memset(ledDmaBuffer, 0xFF, FULL_DMA_BUFFER_LENGTH * sizeof(ledDmaBuffer[0]));
	HAL_Delay(1);
	state = HAL_GPIO_ReadPin(ledDrv.testGpioX, ledDrv.testGpioPin);
	failed |= (state != GPIO_PIN_SET);
	myAssert(!failed, "");

	ledDrv.testing = 0;
	return failed;
}

void initialiseLedStrip(TIM_HandleTypeDef * tim, uint32_t timChannel, GPIO_TypeDef* testGpioX, uint16_t testGpioPin) {
	memset(ledDmaBuffer, 0, sizeof(ledDmaBuffer));
	memset(&ledDrv, 0, sizeof(ledDrv));
	ledDrv.testGpioX = testGpioX;
	ledDrv.testGpioPin = testGpioPin;
	ledDrv.tim = tim;
	ledDrv.timChannel = timChannel;
	ledDrv.maxCurrentMa = 1500;
	ledDrv.gammaCorrectionEnabled = 1;
	ledDrv.redAdjust = 255;
	ledDrv.greenAdjust = 200;
	ledDrv.blueAdjust = 220;
	ledDrv.refreshPeriodMs = 50;
	// Set initial LED pattern just to signal it's working
	ledDrv.numLeds = FIXED_NUM_LEDS == 0 ? 64 : FIXED_NUM_LEDS;
	ledDrv.panelWidth = FIXED_LED_PANEL_WIDTH == 0 ? 1 : FIXED_LED_PANEL_WIDTH;
	
	setUserBrightness(40);

	// Start the DMA
	initialiseLedDma();
	// selfTestLedStrip();


	for (uint16_t i=0; i<ledDrv.numLeds; i++) {
		colourStruct cyanGreen = {0x0, 0xFF, 0x80};
		setBuffer(ledDrv.numLeds-i-1, cyanGreen);
		if (i % 6 == 0)
			showLedData();
		// Turn leds on one by one
		HAL_Delay(1);
	}
	showLedData();
}

void loopLedStrip(uint32_t ticks) {
	// Refresh
	if ((ticks - ledDrv.ticksSinceLastShow) > ledDrv.refreshPeriodMs && ledDrv.refreshPeriodMs != 0) {
		showLedData();
	}
}

#endif

