/* Copyright (c) 2024 Sean Bremner */
/* Based off: https://github.com/afiskon/stm32-st7735  (which is MIT licensed) */
/*            https://github.com/afiskon/stm32-ili9341 (which is MIT licensed)*/
// Supports ST7735 and ILI9341
#include "project.h"
#ifdef ENABLE_SCREEN_MODULE

#include <stdbool.h>
#include <stdint.h>

#define IS_ST7735 0
#define IS_ILI9431 0
#define IS_ST7789 1

// NOTE: depends on rotation
#if IS_ILI9431
	#define TFT_HEIGHT 240
	#define TFT_WIDTH 320
#endif
#if IS_ST7789
	#define TFT_HEIGHT 240
	#define TFT_WIDTH 320
#endif
#if IS_ST7735
	#define TFT_HEIGHT 128
	#define TFT_WIDTH 160
#endif

// Color definitions
#define	BLACK   0x0000
#define	BLUE    0x001F
#define	RED     0xF800
#define	GREEN   0x07E0
#define CYAN    0x07FF
#define MAGENTA 0xF81F
#define YELLOW  0xFFE0
#define WHITE   0xFFFF
#define GREY    COLOR565(0xFF/2, 0xFF/2, 0xFF/2)
#define COLOR565(r, g, b) (((r & 0xF8) << 8) | ((g & 0xFC) << 3) | ((b & 0xF8) >> 3))

void initTft(GPIO_TypeDef* csGpioSection, uint16_t csGpioPin, GPIO_TypeDef* dcGpioSection, uint16_t dcGpioPin,
           GPIO_TypeDef* resetGpioSection, uint16_t resetGpioPin, SPI_HandleTypeDef * spi);

void tftSetAddressWindow(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1);
void tftWriteDataBlocking(uint8_t* buff, uint16_t buff_size);
void tftWriteDataDma(uint8_t* buff, uint16_t buff_size);
void finishDmaWrite();
void waitForDma(void);
bool dmaBusy();
void tftInvertColors(bool invert);
void tftSetRotation(uint8_t m);

#endif //ENABLE_SCREEN_MODULE
