/* Copyright (c) 2024 Sean Bremner */
#include "project.h"
#ifdef ENABLE_SCREEN_MODULE

#include <stdint.h>
#include <stdbool.h>
#include "tft.h"

// Registers
#define NOP                       0x00
#define SW_RESET                  0x01
#define SLEEP_MODE_IN             0x10
#define SLEEP_MODE_OUT            0x11
#define PARTIAL_MODE_ON           0x12
#define NORMAL_MODE_ON            0x13
#define INVERT_COLOUR_OFF         0x20
#define INVERT_COLOUR_ON          0x21
#define GAMMA_CURVE_SET           0x26
#define DISPLAY_OFF               0x28
#define DISPLAY_ON                0x29
#define CASET                     0x2A // COLUMN_ADDRESS_SET
#define RASET                     0x2B // RASET ROW_ADDRESS_SET
#define RAM_WR                    0x2C
#define RAM_RD                    0x2E
#define PARTIAL_AREA              0x30
#define TEAR_OFF                  0x34
#define TEAR_ON                   0x35
#define MADCTL                    0x36 // MEMORY_DATA_ACCESS_CONTROL
#define     MADCTL_MY                 0x80 // Row direction (up/down)
#define     MADCTL_MX                 0x40 // Column direction (left/right)
#define     MADCTL_MV                 0x20 // Swap columns and rows
#define     MADCTL_ML                 0x10 // vertical refresh direction
#define     MADCTL_MH                 0x04 // horizontal refresh direction
#define     MADCTL_BGR                0x08 // Instead of RGB use BGR
#define     MADCTL_RGB                0x00
#define IDLE_MODE_OFF             0x38
#define IDLE_MODE_ON              0x39
#define COLMOD                    0x3A
#define     COLMOD_12_BIT_COLOUR      0x3
#define     COLMOD_16_BIT_COLOUR      0x55
#define     COLMOD_18_BIT_COLOUR      0x6
#define GAMMA_CORRECTION_POSITIVE 0xE0
#define GAMMA_CORRECTION_NEGATIVE 0xE1


#if IS_ST7735
    #define FRMCTR1                     0xB1
    #define FRMCTR2                     0xB2
    #define FRMCTR3                     0xB3
    #define INVCTR                      0xB4
    #define DISSET5                     0xB6
    #define PWCTR1                      0xC0
    #define PWCTR2                      0xC1
    #define PWCTR3                      0xC2
    #define PWCTR4                      0xC3
    #define PWCTR5                      0xC4
    #define VMCTR1                      0xC5
    #define PWCTR6                      0xFC
    #define MADCTL_RGB_MODE MADCTL_RGB
    #define DEFAULT_ROTATION (MADCTL_MV | MADCTL_MY)
#elif IS_ILI9431
    #define RGB_SIGNAL_CONTROL          0xB0
    #define FRMCTR1                     0xB1 // Normal mode
    #define FRMCTR2                     0xB2 // Idle mode
    #define FRMCTR3                     0xB3 // Partial mode
    #define INVCTR                      0xB4
    #define PORCH_CONTROL               0xB5
    #define DISPLAY_FUNCTION_CONTROLS   0xB6
    #define PWCTR1                      0xC0
    #define PWCTR2                      0xC1
    #define VMCTR1                      0xC5
    #define VMCTR2                      0xC7
    #define POWER_CONTROL_A             0xCB
    #define POWER_CONTROL_B             0xCF
    #define POWER_SEQUENCE_CONTROL      0xED
    #define PUMP_RATIO_CONTROL          0xF7
    #define DIGITAL_GAMMA_CONTROL1      0xE2
    #define DIGITAL_GAMMA_CONTROL2      0xE3
    #define MADCTL_RGB_MODE MADCTL_BGR
    #define DEFAULT_ROTATION (MADCTL_MV)
#elif IS_ST7789
    #define COLOUR_SET                  0x2D
    #define RGBCTRL                     0xB1
    #define PORCTRL                     0xB2
    #define FRCTRL1                     0xB3
    #define PARCTRL                     0xB5
    #define GCTRL                       0xB7
    #define GTADJ                       0xB8
    #define DGMEN                       0xBA // Digital gamma enable
    #define VCOMS                       0xBB
    #define LCMCTRL                     0xC0
    #define VDVVRHEN                    0xC2
    #define VRHS                        0xC3
    #define VDVS                        0xC4
    #define VCMOFSET                    0xC5
    #define FRCTRL2                     0xC6
    #define CABCCTRL                    0xC7
    #define PWMFRSEL                    0xCC
    #define PWCTRL1                     0xD0
    #define VAPVANEN                    0xD2
    #define DIGITAL_GAMMA_RED_LUT       0xE2
    #define DIGITAL_GAMMA_BLUE_LUT      0xE3
    #define MADCTL_RGB_MODE MADCTL_RGB
    #define DEFAULT_ROTATION (MADCTL_MV | MADCTL_MX)
#endif



typedef struct {
    GPIO_TypeDef* csGpioSection;
    uint16_t csGpioPin;
    GPIO_TypeDef* dcGpioSection;
    uint16_t dcGpioPin;
    GPIO_TypeDef* resetGpioSection;
    uint16_t resetGpioPin;
    SPI_HandleTypeDef * spi;
} tTftDrv;

tTftDrv tftDrv = {};


// bool spiDmaBusy = 0;

static void writeDma(void *data, uint16_t length) {
    // spiDmaBusy = 1;
    // HAL_SPI_Transmit(tftDrv.spi, data, length, HAL_MAX_DELAY);
    // return;

// #ifdef __STM32F0xx_HAL_H
//    tftDrv.spi->hdmatx->Instance->CCR =  (DMA_CCR_MINC | DMA_CCR_DIR); // Memory increment, direction to peripherial
//    tftDrv.spi->hdmatx->Instance->CMAR  = (uint32_t) data; // Source address
//    tftDrv.spi->hdmatx->Instance->CPAR  = (uint32_t) &tftDrv.spi->Instance->DR; // Destination address
//    tftDrv.spi->hdmatx->Instance->CNDTR = length;
//    tftDrv.spi->Instance->CR1 &= ~(SPI_CR1_SPE);  // Disable SPI
//    tftDrv.spi->Instance->CR2 |= SPI_CR2_TXDMAEN; // Enable DMA transfer
//    tftDrv.spi->Instance->CR1 |= SPI_CR1_SPE;     // Enable SPI
//    tftDrv.spi->hdmatx->Instance->CCR |= DMA_CCR_EN;     // Start DMA transfer
// #else
    HAL_SPI_Transmit_DMA(tftDrv.spi, data, length);
// #endif
}

bool dmaBusy() {
    // return spiDmaBusy;
    
    // return (tftDrv.spi->State != HAL_SPI_STATE_READY);
    // return tftDrv.dmaBusy;
#ifdef __STM32F0xx_HAL_H
    return ((__HAL_DMA_GET_COUNTER(tftDrv.spi->hdmatx) > 0) && (tftDrv.spi->hdmatx->Lock == HAL_LOCKED));
#else
    return (tftDrv.spi->hdmatx->Lock == HAL_LOCKED);
#endif
}

// void HAL_SPI_TxCpltCallback(SPI_HandleTypeDef *hspi) {
//     if (hspi == tftDrv.spi) {
//         spiDmaBusy = 0;
//     }
// }

void waitForDma(void) {
    uint32_t startTicks = HAL_GetTick();
    while(dmaBusy()) {
        if (HAL_GetTick() > 100 + startTicks) {
            softAssert(0, "Screen SPI DMA timed out");
            break;
        }
    }
    // softAssert(!dmaBusy(), "");
}

static void tftReset() {
    HAL_GPIO_WritePin(tftDrv.csGpioSection, tftDrv.csGpioPin, GPIO_PIN_SET);
    HAL_GPIO_WritePin(tftDrv.resetGpioSection, tftDrv.resetGpioPin, GPIO_PIN_SET);
    HAL_Delay(50);
    HAL_GPIO_WritePin(tftDrv.resetGpioSection, tftDrv.resetGpioPin, GPIO_PIN_RESET);
    HAL_Delay(50);
    HAL_GPIO_WritePin(tftDrv.resetGpioSection, tftDrv.resetGpioPin, GPIO_PIN_SET);
    HAL_Delay(150);
}

void tftWriteCommand(uint8_t cmd) {
    HAL_GPIO_WritePin(tftDrv.csGpioSection, tftDrv.csGpioPin, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(tftDrv.dcGpioSection, tftDrv.dcGpioPin, GPIO_PIN_RESET);
    HAL_SPI_Transmit(tftDrv.spi, &cmd, 1, HAL_MAX_DELAY);
    HAL_GPIO_WritePin(tftDrv.csGpioSection, tftDrv.csGpioPin, GPIO_PIN_SET);
}

void tftWriteDataBlocking(uint8_t* buff, uint16_t buff_size) {
    HAL_GPIO_WritePin(tftDrv.csGpioSection, tftDrv.csGpioPin, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(tftDrv.dcGpioSection, tftDrv.dcGpioPin, GPIO_PIN_SET);
    HAL_SPI_Transmit(tftDrv.spi, buff, buff_size, HAL_MAX_DELAY);
    HAL_GPIO_WritePin(tftDrv.csGpioSection, tftDrv.csGpioPin, GPIO_PIN_SET);
}

void tftWriteDataDma(uint8_t* buff, uint16_t buff_size) {
    // tftWriteDataBlocking(buff, buff_size);
    HAL_GPIO_WritePin(tftDrv.csGpioSection, tftDrv.csGpioPin, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(tftDrv.dcGpioSection, tftDrv.dcGpioPin, GPIO_PIN_SET);
    writeDma(buff, buff_size);
}

void finishDmaWrite() {
    HAL_GPIO_WritePin(tftDrv.csGpioSection, tftDrv.csGpioPin, GPIO_PIN_SET);
}

#define WRITE_CMD_AND_DATA(CMD, bytes...) \
    { \
        uint8_t data[] = {bytes}; \
        tftWriteCommand(CMD); \
        tftWriteDataBlocking(data, sizeof(data)); \
    }

#if IS_ST7735

void tftConfigureVoltagesTimingsAndGammaCorrection() {
    // Set GVDD voltage and current
    // WRITE_CMD_AND_DATA(PWCTR1,  0x02, 0x70); // -4.6V, AUTO mode - Default is 4.7V, 2.5uA
    // // Set VGL and VGL voltages - default is 5
    // WRITE_CMD_AND_DATA(PWCTR2, 0x5);
    // // Set op-amp current and boost frequency - for different modes
    // WRITE_CMD_AND_DATA(PWCTR3,  0x01, 0x02);
    // WRITE_CMD_AND_DATA(PWCTR6,  0x11, 0x15);
    // // Set VCOM high and low voltages - default is 4.5V and -0.57V
    // WRITE_CMD_AND_DATA(VMCTR1, 0x3C, 0x38);
    // // Frame rate control
    // WRITE_CMD_AND_DATA(FRMCTR1,  0x00, 0x06, 0x03); // Normal mode
    // // WRITE_CMD_AND_DATA(FRMCTR2,  0x01, 0x2C, 0x2D); // Idle mode
    // // WRITE_CMD_AND_DATA(FRMCTR3,  0x01, 0x2C, 0x2D, 0x01, 0x2C, 0x2D); // Partial mode
    // WRITE_CMD_AND_DATA(DISSET5,  0x15, 0x02);
    // WRITE_CMD_AND_DATA(INVCTR, 0x00);
    // Gamma correction
    WRITE_CMD_AND_DATA(GAMMA_CORRECTION_POSITIVE,
        0x09, 0x16, 0x09, 0x20,
        0x21, 0x1B, 0x13, 0x19,
        0x17, 0x15, 0x1E, 0x2B,
        0x04, 0x05, 0x02, 0x0E);
    WRITE_CMD_AND_DATA(GAMMA_CORRECTION_NEGATIVE,
        0x0B, 0x14, 0x08, 0x1E,
        0x22, 0x1D, 0x18, 0x1E,
        0x1B, 0x1A, 0x24, 0x2B,
        0x06, 0x06, 0x02, 0x0F);
}

#elif IS_ILI9431

void tftConfigureVoltagesTimingsAndGammaCorrection() {
    WRITE_CMD_AND_DATA(PWCTR1, 0x23); // 4.6V (default 4.5V)
    WRITE_CMD_AND_DATA(PWCTR2, 0x10); // SAP[2:0];BT[3:0] - default is 0x0
    WRITE_CMD_AND_DATA(VMCTR1,  0x3E, 0x28 );  // high: 4.25V, low: -1.5V  (default 0x31, 0x3C - 3.925,-0.6V)
    WRITE_CMD_AND_DATA(VMCTR2,  0x86 );
    WRITE_CMD_AND_DATA(POWER_CONTROL_A,  0x39, 0x2C, 0x00, 0x34, 0x02 );  // Same as default
    WRITE_CMD_AND_DATA(POWER_CONTROL_B,  0x00, 0xC1, 0x30 ); // default 0x00, 0x81, 0x30
    WRITE_CMD_AND_DATA(POWER_SEQUENCE_CONTROL,   0x64, 0x03, 0x12, 0x81 ); // default 0x55, 0x01, 0x23, 0x01
    WRITE_CMD_AND_DATA(PUMP_RATIO_CONTROL, 0x20);
    // DRIVER TIMING CONTROL A
    WRITE_CMD_AND_DATA(0xE8,  0x85, 0x00, 0x78 );
    // DRIVER TIMING CONTROL B
    WRITE_CMD_AND_DATA(0xEA,  0x00, 0x00 );
    // Frame rate
    WRITE_CMD_AND_DATA(FRMCTR1,  0x00, 0x18 ); // 79Hz, default 70Hz
    WRITE_CMD_AND_DATA(DISPLAY_FUNCTION_CONTROLS,  0x08, 0x82, 0x27 ); // Default - 0x0A, 0x82, 0x27
    // 3GAMMA FUNCTION DISABLE
    WRITE_CMD_AND_DATA(INVCTR, 0x07);
    WRITE_CMD_AND_DATA(0xF2,  0x00 );
    WRITE_CMD_AND_DATA(GAMMA_CURVE_SET,  0x01 );
    WRITE_CMD_AND_DATA(GAMMA_CORRECTION_POSITIVE,  0x0F, 0x31, 0x2B, 0x0C, 0x0E, 0x08, 0x4E, 0xF1, 0x37, 0x07, 0x10, 0x03, 0x0E, 0x09, 0x00 );
    WRITE_CMD_AND_DATA(GAMMA_CORRECTION_NEGATIVE,  0x00, 0x0E, 0x14, 0x03, 0x11, 0x07, 0x31, 0xC1, 0x48, 0x08, 0x0F, 0x0C, 0x31, 0x36, 0x0F );
}

#elif IS_ST7789

void tftConfigureVoltagesTimingsAndGammaCorrection(void)
{
    HAL_Delay(50);
    WRITE_CMD_AND_DATA(PORCTRL, 0x0C, 0x0C, 0x00, 0x33, 0x33);
    HAL_Delay(50);
    /* Internal LCD Voltage generator settings */
    WRITE_CMD_AND_DATA(GCTRL, 0x35);         //  Gate Control - Default value
    WRITE_CMD_AND_DATA(VCOMS, 0x19);         //  VCOM setting - 0.725v (default 0.75v for 0x20)
    WRITE_CMD_AND_DATA(LCMCTRL, 0x2C);       //  LCMCTRL -Default value
    WRITE_CMD_AND_DATA(VDVVRHEN, 0x01);      //  VDV and VRH command Enable - Default value
    WRITE_CMD_AND_DATA(VRHS, 0x12);          //  VRH set:  +-4.45v (defalut +-4.1v for 0x0B)
    WRITE_CMD_AND_DATA(VDVS, 0x20);          //  VDV set - defaul value
    WRITE_CMD_AND_DATA(FRCTRL2, 0x0F);       //  Frame rate control in normal mode - defaul value (60Hz)
    WRITE_CMD_AND_DATA(PWCTRL1, 0xA4, 0xA1); //  Power control - defaul value
    // Gamma correction
    WRITE_CMD_AND_DATA(GAMMA_CORRECTION_POSITIVE,  0xD0, 0x04, 0x0D, 0x11, 0x13, 0x2B, 0x3F, 0x54, 0x4C, 0x18, 0x0D, 0x0B, 0x1F, 0x23);
    WRITE_CMD_AND_DATA(GAMMA_CORRECTION_NEGATIVE,  0xd0, 0x08, 0x10, 0x08, 0x06, 0x06, 0x39, 0x44, 0x51, 0x0b, 0x16, 0x14, 0x2f, 0x31 );
}

#endif


void initTft(GPIO_TypeDef* csGpioSection, uint16_t csGpioPin, GPIO_TypeDef* dcGpioSection, uint16_t dcGpioPin,
            GPIO_TypeDef* resetGpioSection, uint16_t resetGpioPin, SPI_HandleTypeDef * spi) {

    tftDrv.csGpioSection = csGpioSection;
    tftDrv.csGpioPin = csGpioPin;
    tftDrv.dcGpioSection = dcGpioSection;
    tftDrv.dcGpioPin = dcGpioPin;
    tftDrv.resetGpioSection = resetGpioSection;
    tftDrv.resetGpioPin = resetGpioPin;
    tftDrv.spi = spi;

    tftReset();
    // tftWriteCommand(SW_RESET);
    // HAL_Delay(200);
    tftWriteCommand(SLEEP_MODE_OUT);
    HAL_Delay(200);

    WRITE_CMD_AND_DATA(COLMOD, COLMOD_16_BIT_COLOUR);
    HAL_Delay(25);
    WRITE_CMD_AND_DATA(MADCTL, (MADCTL_RGB_MODE | DEFAULT_ROTATION));
    tftConfigureVoltagesTimingsAndGammaCorrection();

    // Rotation and colour type
    tftWriteCommand (INVERT_COLOUR_ON); //  Inversion ON
    HAL_Delay(15);
    tftWriteCommand(NORMAL_MODE_ON); // Normal display on
    HAL_Delay(15);
    tftWriteCommand(DISPLAY_ON); // Main screen turn on
    HAL_Delay(100);
}

void tftSetAddressWindow(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1) {
    static uint16_t lastX0 = 0xFFFF;
    static uint16_t lastX1 = 0xFFFF;
    static uint16_t lastY0 = 0xFFFF;
    static uint16_t lastY1 = 0xFFFF;
    if (x0 != lastX0 || x1 != lastX1) {
        WRITE_CMD_AND_DATA(CASET, (x0 >> 8) & 0xFF, x0 & 0xFF, (x1 >> 8) & 0xFF, x1 & 0xFF);
        lastX0 = x0;
        lastX1 = x1;
    }
    if (y0 != lastY0 || y1 != lastY1) {
        WRITE_CMD_AND_DATA(RASET, (y0 >> 8) & 0xFF, y0 & 0xFF, (y1 >> 8) & 0xFF, y1 & 0xFF);
        lastY0 = y0;
        lastY1 = y1;
    }
    tftWriteCommand(RAM_WR);
}

void tftSetRotation(uint8_t m)
{
    switch (m) {
    case 0:
        WRITE_CMD_AND_DATA(MADCTL, MADCTL_MX | MADCTL_MY | MADCTL_RGB_MODE);
        break;
    case 1:
        WRITE_CMD_AND_DATA(MADCTL, MADCTL_MY | MADCTL_MV | MADCTL_RGB_MODE);
        break;
    case 2:
        WRITE_CMD_AND_DATA(MADCTL, MADCTL_RGB_MODE);
        break;
    case 3:
        WRITE_CMD_AND_DATA(MADCTL, MADCTL_MX | MADCTL_MV | MADCTL_RGB_MODE);
        break;
    default:
        break;
    }
}

void tftInvertColors(bool invert) {
    tftWriteCommand(invert ? INVERT_COLOUR_ON : INVERT_COLOUR_OFF);
}

#endif //ENABLE_SCREEN_MODULE
