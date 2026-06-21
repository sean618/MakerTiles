#include "project.h"

#ifdef ENABLE_WIRELESS_MODULE

extern const tFieldTable wirelessFieldTable;

void initialiseWireless(GPIO_TypeDef* enableGpio, uint16_t enableGpioPin,
                        GPIO_TypeDef* csnGpio, uint16_t csnGpioPin,
                        GPIO_TypeDef* interruptGpio, uint16_t interruptGpioPin,
                        TIM_HandleTypeDef * usTimer,
                        CRC_HandleTypeDef * hcrc,
                        SPI_HandleTypeDef * spi);
void loopWireless(uint32_t ticks);
void w_HAL_SPI_TxRxCpltCallback(SPI_HandleTypeDef *hspi);

#endif
