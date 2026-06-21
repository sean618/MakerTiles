#include "project.h"

#ifdef ENABLE_WIRELESS_MODULE

typedef void (*processRxPacketFn)(uint8_t * data);
typedef bool (*fillInTxData)(bool firstTxOfSlot, uint8_t * data, uint8_t length);

void nrf24Initialise(
    GPIO_TypeDef* enableGpio, uint16_t enableGpioPin,
    GPIO_TypeDef* interruptGpio, uint16_t interruptGpioPin, 
    GPIO_TypeDef* csnGpio, uint16_t csnGpioPin,
    CRC_HandleTypeDef * hcrc,
    TIM_HandleTypeDef * usTimer,
    SPI_HandleTypeDef * spi
);

void radioTransmit(getTxDataFn getTxData);
void radioReceive(processRxPacketFn processRxPacket);

// void nrf24IssueCommandWithReplyDma(uint8_t *buffer, uint8_t *reply, uint16_t length);
// void nrf24IssueCommandWithReply(uint8_t *buffer, uint8_t *reply, uint16_t length);

// void nrf24CommandByte(uint8_t cmd);
// void nrf24GetRegisterByte(uint8_t address, uint8_t *value);
// void nrf24SetRegisterByte(uint8_t address, uint8_t value);
// uint8_t nrf24CheckStatus();

// void nrf24StartTxMode();
// void nrf24StopTxMode();


#endif
