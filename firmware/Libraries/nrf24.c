#include "project.h"
#ifdef ENABLE_WIRELESS_MODULE


#include <stdio.h>
#include <math.h>
#include <string.h>
#include <stdint.h>

#include "useful.h"
#include "microbus.h"
#include "master.h"
#include "node.h"


// HW setup
// ========
// SPI at 8bit 6Mbps
// GPIO - Add pull up to Interrupt pin (falling edge trigger if using as EXTI interrupt)
// TIM2: (32 bit timer)
//    	Clock source: Internal clock
//		Prescalar: 47 (for 1Mhz timer)

// TODO:
// - Speed up Rx side - currently overwhelmed
// - Perhaps cut down version of spi functions
// 

#define MAX_WIRELESS_PACKET_TX_TIME_US 220 // 32 bytes at 1.2mbps


// Commands codes
#define READ_REGISTER   0x00
#define WRITE_REGISTER  0x20
#define READ_PAYLOAD    0x61
#define WRITE_PAYLOAD   0xA0
#define WRITE_PAYLOAD_NO_ACK   0xA0
#define FLUSH_TX        0xE1
#define FLUSH_RX        0xE2
#define NO_OPERATION    0xFF

// 1-byte wide registers
#define CONFIG_REGISTER             0x00
#define AUTO_ACKNOWLEDGE_REGISTER   0x01
#define ENABLE_RX_PIPE_REGISTER     0x02
#define ADDRESS_WIDTH_REGISTER      0x03
#define AUTO_RETRANSMIT_REGISTER    0x04
#define RF_CHANNEL_REGISTER         0x05
#define RF_SETUP_REGISTER           0x06
#define STATUS_REGISTER             0x07
#define OBSERVE_TX_REGISTER         0x08
#define RX_POWER_DETECTOR_REGISTER  0x09
#define RX_PIPE_2_ADDRESS_REGISTER  0x0C
#define RX_PIPE_3_ADDRESS_REGISTER  0x0D
#define RX_PIPE_4_ADDRESS_REGISTER  0x0E
#define RX_PIPE_5_ADDRESS_REGISTER  0x0F
#define RX_PIPE_0_WIDTH_REGISTER    0x11
#define RX_PIPE_1_WIDTH_REGISTER    0x12
#define RX_PIPE_2_WIDTH_REGISTER    0x13
#define RX_PIPE_3_WIDTH_REGISTER    0x14
#define RX_PIPE_4_WIDTH_REGISTER    0x15
#define RX_PIPE_5_WIDTH_REGISTER    0x16
#define FIFO_STATUS_REGISTER        0x17
#define FEATURE_REGISTER            0x1D

// 5-byte wide registers
#define RX_PIPE_0_ADDRESS_REGISTER  0x0A
#define RX_PIPE_1_ADDRESS_REGISTER  0x0B
#define TX_ADDRESS_REGISTER         0x10


// // FIFO status bits
// #define TX_FULL  5
#define TX_EMPTY 4
// #define RX_FULL  1
#define RX_EMPTY 0

// Status bits
#define RX_DR 6
#define TX_DS 5
#define MAX_RT 4
#define TX_FULL 0



typedef struct sWirelessDriver {
    GPIO_TypeDef* interruptGpio;
    uint16_t interruptGpioPin;
    GPIO_TypeDef* enableGpio;
    uint16_t enableGpioPin;
    GPIO_TypeDef* csnGpio;
    uint16_t csnGpioPin;
    CRC_HandleTypeDef * hcrc;
    TIM_HandleTypeDef * usTimer;
    SPI_HandleTypeDef * spi;
} WirelessDriver;

static WirelessDriver nrf24 = {};



// ======================== //

// TODO: should be called - not just declared here
bool spiReady = 1;
void HAL_SPI_TxRxCpltCallback(SPI_HandleTypeDef *hspi) {
    HAL_GPIO_WritePin(nrf24.csnGpio, nrf24.csnGpioPin, GPIO_PIN_SET); // CSN
    spiReady = 1;
}


// ======================== //


// Needs to be called every 65ms!!
uint64_t timeUs = 0;
uint64_t wGetUs() { // TODO: put back to static
    static uint32_t lastCounter = 0;
    uint32_t counter = __HAL_TIM_GET_COUNTER(nrf24.usTimer);
    if (counter < lastCounter) {
        timeUs += (/*4294967296*/ 65535 - lastCounter) + counter;
    } else {
        timeUs += (counter - lastCounter);
    }
    lastCounter = counter;
    return timeUs;
}

static void resetCeTxRxModePin() {
    HAL_GPIO_WritePin(nrf24.enableGpio, nrf24.enableGpioPin, GPIO_PIN_RESET);
}

static void setCeTxRxModePin() {
    HAL_GPIO_WritePin(nrf24.enableGpio, nrf24.enableGpioPin, GPIO_PIN_SET);
}

static void llissueCommandWithReply(uint8_t *buffer, uint8_t *reply, uint16_t length, bool useDma) {
    if (!spiReady) {
        uint32_t timeout = wGetUs() + 1000000;
        while (!spiReady && (wGetUs() < timeout)) {}
        softAssert(wGetUs() < timeout, "");
    }

    static uint8_t tmpreply[50] = {0}; // TODO
    static uint8_t tmpcmd[50] = {0}; // TODO
    myAssert(length < 50, "");
    // TODO: do we really need this memcpy? - costly
    memcpy(tmpcmd, buffer, length);
    uint8_t * replyPtr = reply;
    if (reply == NULL) {
        replyPtr = tmpreply;
    }
    
    HAL_StatusTypeDef status;
    spiReady = 0;
    HAL_GPIO_WritePin(nrf24.csnGpio, nrf24.csnGpioPin, GPIO_PIN_RESET); // CSN
    if (!useDma) {
        status = HAL_SPI_TransmitReceive(nrf24.spi, tmpcmd, replyPtr, length, 1000);
        spiReady = 1;
        HAL_GPIO_WritePin(nrf24.csnGpio, nrf24.csnGpioPin, GPIO_PIN_SET); // CSN
         
    } else {
        status = HAL_SPI_TransmitReceive_DMA(nrf24.spi, buffer, replyPtr, length);
    }
    // HAL_GPIO_WritePin(nrf24.csnGpio, nrf24.csnGpioPin, GPIO_PIN_SET); // CSN
    softAssert(status == HAL_OK, "SPI command failed");
}

// =================================== //

// Fill in buffer with the reply
static void nrf24IssueCommandWithReplyDma(uint8_t *buffer, uint8_t *reply, uint16_t length) {
    llissueCommandWithReply(buffer, reply, length, 1);
}

// Fill in buffer with the reply
static void nrf24IssueCommandWithReply(uint8_t *buffer, uint8_t *reply, uint16_t length) {
    llissueCommandWithReply(buffer, reply, length, 0);
}

void nrf24CommandByte(uint8_t cmd) {
    issueCommandWithReply(&cmd, NULL, 1);
}

#define ISSUE_COMMAND(buffer) issueCommandWithReply(buffer, NULL, sizeof(buffer))
#define ISSUE_COMMAND_WITH_REPLY(buffer, reply) issueCommandWithReply(buffer, reply, sizeof(buffer))

// =================================== //

void nrf24GetRegisterByte(uint8_t address, uint8_t *value) {
    uint8_t buffer[] = { READ_REGISTER | address, 0x00 };
    uint8_t reply[sizeof(buffer)] = {0};
    ISSUE_COMMAND_WITH_REPLY(buffer, reply);
    *value = reply[1];
}

void nrf24SetRegisterByte(uint8_t address, uint8_t value) {
    uint8_t buffer[] = { WRITE_REGISTER | address, value };
    ISSUE_COMMAND(buffer);
}

void nrf24SetRegister5Bytes(uint8_t address, uint64_t value) {
    uint8_t buffer[] = { WRITE_REGISTER | address,
        (value >> 0) & 0xFF,
        (value >> 8) & 0xFF,
        (value >> 16) & 0xFF,
        (value >> 24) & 0xFF,
        (value >> 32) & 0xFF
    };
    ISSUE_COMMAND(buffer);
}

uint8_t nrf24CheckStatus() {
    uint8_t buffer[] = { NO_OPERATION };
    uint8_t reply[sizeof(buffer)];
    ISSUE_COMMAND_WITH_REPLY(buffer, reply);
    return reply[0];
}

// void readRxPowerDetected() {
// 	nrf24GetRegisterByte(RX_POWER_DETECTOR_REGISTER, &nrf24.state.rxPowerDetected);
// }

void nrf24SetChannel(uint8_t value) {
    value = MIN(value, 128);
    nrf24.channel = value;
    // TODO: needs to write it to flash
    nrf24SetRegisterByte(RF_CHANNEL_REGISTER, nrf24.channel); // 2.402 GHz
    memset(&nrf24.state, 0, sizeof(nrf24.state)); // Reset all the state
}

// ======================================//
// Initialise

void nrf24Initialise(
        GPIO_TypeDef* enableGpio, uint16_t enableGpioPin,
        GPIO_TypeDef* interruptGpio, uint16_t interruptGpioPin, 
        GPIO_TypeDef* csnGpio, uint16_t csnGpioPin,
        CRC_HandleTypeDef * hcrc,
        TIM_HandleTypeDef * usTimer,
        SPI_HandleTypeDef * spi) {
    nrf24.spi = spi;
    nrf24.interruptGpio = interruptGpio;
    nrf24.interruptGpioPin = interruptGpioPin;
    nrf24.enableGpio = enableGpio;
    nrf24.enableGpioPin = enableGpioPin;
    nrf24.csnGpio = csnGpio;
    nrf24.csnGpioPin = csnGpioPin;
    nrf24.usTimer = usTimer;
    nrf24.hcrc = hcrc;
    
    HAL_Delay(100);

    HAL_GPIO_WritePin(nrf24.csnGpio, nrf24.csnGpioPin, GPIO_PIN_SET); // CSN
    resetCeTxRxModePin();
    HAL_Delay(100);
    
    // Just send a dummy command to get everything in a good state
    uint8_t buffer[] = { WRITE_REGISTER | CONFIG_REGISTER, 0xD };
    ISSUE_COMMAND(buffer);
    
    nrf24.channel = 0;
    
    nrf24SetRegisterByte(CONFIG_REGISTER, 0x0D); // Power-down (2-byte CRC, RX mode, enable interrupts)
    HAL_Delay(5); // Give time to power down
    nrf24SetRegisterByte(AUTO_ACKNOWLEDGE_REGISTER, 0x00); // Disable
    nrf24SetRegisterByte(ENABLE_RX_PIPE_REGISTER, 0x01); // Enable pipe 0
    nrf24SetRegisterByte(ADDRESS_WIDTH_REGISTER, 0x03); // 5-byte addresses
    nrf24SetRegisterByte(AUTO_RETRANSMIT_REGISTER, 0x00); // Disable
    nrf24SetRegisterByte(RF_CHANNEL_REGISTER, nrf24.channel); // 2.402 GHz
    // Frequency - 0x0F for 2Mbps, 0x07 for 1Mbps, 0x27 for 250Kbps - issues with tx failures at 1Mbps so keep at 2
    nrf24SetRegisterByte(RF_SETUP_REGISTER, 0x0F); // RF_PWR bits 1 and 2, 0b11 is max
    nrf24SetRegister5Bytes(RX_PIPE_0_ADDRESS_REGISTER, 0xF0CCA5330F);
    nrf24SetRegister5Bytes(TX_ADDRESS_REGISTER, 0xF0CCA5330F);
    nrf24SetRegisterByte(RX_PIPE_0_WIDTH_REGISTER, 0x20); // 32-byte messages
    nrf24SetRegisterByte(CONFIG_REGISTER, 0x1F); // Power-up (2-byte CRC, RX mode, enable tx interrupts, enable rx interrupt)
    HAL_Delay(5); // Wait for crystal to stabilise
    
    nrf24CommandByte(FLUSH_TX);
    nrf24CommandByte(FLUSH_RX);

    nrf24SetRegisterByte(STATUS_REGISTER, (1 << RX_DR)  | (1 << TX_DS)  | (1 << MAX_RT));

    uint8_t fifoStatus;
    nrf24GetRegisterByte(FIFO_STATUS_REGISTER, &fifoStatus);
    softAssert(fifoStatus == 0x11, "");

    uint8_t tmp = 0;
    nrf24GetRegisterByte(CONFIG_REGISTER, &tmp);
    softAssert(tmp == 0x1F, "");
    
    setCeTxRxModePin();

    nrf24CommandByte(FLUSH_RX);
}

// ======================================//

static void nrf24StartTxMode() {
    // Go into Standby-I mode
    resetCeTxRxModePin();
    // Set TX mode
    nrf24SetRegisterByte(CONFIG_REGISTER, 0x0E);
    // Transition from Standby-I to Tx mode
    setCeTxRxModePin();
}

static void nrf24StopTxMode() {
    // Check FIFO is empty so it's safe to transition back
    while (1) {
        uint8_t status;
        nrf24GetRegisterByte(FIFO_STATUS_REGISTER, &status);
        if (((status >> 4) & 1) == 1) {
            break;
        }
    };
    
    // Go into Standby-I mode once FIFO is empty
    resetCeTxRxModePin();

    // Set RX mode
    nrf24SetRegisterByte(CONFIG_REGISTER, 0x0F);
    
    // Transition from Standby-I to Rx mode
    setCeTxRxModePin();
    
    nrf24SetRegisterByte(STATUS_REGISTER, 1 << TX_DS);

    // Wait 100us for transition to occur
    uint64_t waitTime2 = wGetUs() + 100;
    while (wGetUs() < waitTime2) {
        ;
    }
}


static bool writeTxPacketToFifo(getTxDataFn getTxPacket, bool firstTxOfSlot) {
    // Wait for any previous write to finish - as it might still be using 
    // the shared packet memory
    while (!spiReady) {
        ;
    }
    tWirelessPacket * packet = getTxPacket(firstTxOfSlot);
    if (packet) {
        packet->command = WRITE_PAYLOAD;
        // Write tx packet into NRF24 fifo
        nrf24IssueCommandWithReplyDma(&packet[0], NULL, 33);
        wdrv.state.txNumPackets++;
    }
    return valid;
}


void radioTransmit(getTxDataFn getTxPacket) {
    uint64_t endTimeUs = wGetUs() + SLOT_DATA_TRANSFER_US;

    nrf24StartTxMode();

    // Fill up the FIFO to max of 3 entries
    for (uint16_t i=0; i<3; i++) {
        if (writeTxPacketToFifo(getTxPacket, i==0) == false) {
            goto exit;
        }
    }

    // Add more as the fifo empties
    while (1) {
        // Wait until there is a FIFO entry free
        while (1) {
            if (wGetUs() + 220 > endTimeUs) {
                goto exit;
            }

            uint8_t status;
            checkStatus(&status);
            if ((status & 1) == 0) {
                break;
            }
        };
        // Send the next packet
        if (writeTxPacketToFifo(getTxPacket) == false) {
            goto exit;
        }
    }
exit:
    nrf24StopTxMode();
}


// ======================================//


// TODO: record the average and max offset in timings
int64_t diff[50] = {0};
uint32_t sbcount = 0;

// TODO: interruptGpio - maybe make this an actual interrupt that has priority over every but just records the timestamp if first rx burst packet

static bool rxPacketDetected(uint64_t startTimeUs) {
    if (HAL_GPIO_ReadPin(wdrv.interruptGpio, wdrv.interruptGpioPin) == GPIO_PIN_RESET) {
        // Adjust the slot timings if packet from master (and is first of the burst)
        if (wdrv.state.firstRxBurstPacket) {
            wdrv.state.firstRxBurstPacket = 0;
            // Update when we expect the next slot to occur
            uint64_t predNextSlotTimeUs = startTimeUs - RX_START_TIME_ADJUSTMENT_US + SLOT_CYCLE_US;
            if (sbcount < 50) {
                diff[sbcount] = wdrv.state.nextSlotTimeUs - predNextSlotTimeUs;
                sbcount++;
            }
            if (wdrv.mode == W_NODE) {
                wdrv.state.nextSlotTimeUs = predNextSlotTimeUs;
            }
        }
        return true;
    }

    // Every 100ms read the register to check just in case we've missed an interrupt
    static uint32_t nextCheckRxTime = 0;
    uint32_t ticks = HAL_GetTick();
    if (ticks >= nextCheckRxTime) { 
        bool checkTimeout = ticks >= nextCheckRxTime;
        nextCheckRxTime = ticks + 10; // TODO: reduce
        if (checkTimeout) {
            // Check status that it's an Rx data ready interrupt
            uint8_t status;
            checkStatus(&status);
            if ((status & 0x0E) != 0) {
                return false;
            } else {
                wdrv.state.rxMissedInterrupt++;
                return true;
            }
        }
    }
    return false;
}

const uint8_t rxReadCommand[33] = {
    READ_PAYLOAD, 
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0
};

static void startRxReadRxPacket(uint8_t packetEnd) {
    softAssert(numInBuffer < WIRELESS_RX_PACKET_BUFFER_SIZE, "");
    uint8_t * packet = &wdrv.rxPacketData[packetsEnd * 33];
    nrf24IssueCommandWithReplyDma((uint8_t *) rxReadCommand, &packet[0], 33);
    wdrv.state.rxNumPackets++;
    return packet;
}


// Typically takes 141us per packet - 1.8Mbps
// Rx has 1-3 x 32 byte FIFOs - 32 bytes at 2Mbps means needs to be serviced every 256us
// Read all rx packets in fifo and process them
void radioReceive(processRxPacketFn processRxPacket) {
    // Rx sequence according to footnote on datasheet:
    // 1. Check the interrupt/flag for rx_dr
    // 2. Read data
    // 3. Clear flag
    // 4. Check Rx FIFO for data and if so repeat from step 2

    // This needs to be called at least twice every 65ms
    uint64_t startTimeUs = wGetUs();

    if (!rxPacketDetected(startTimeUs)) {
        return;
    }
    
    uint8_t packetsStart = 0;
    uint8_t packetsEnd = 0;
    uint32_t rxPacketsProcessed = 0;
    
    // Read until FIFO empty
    while (1) {
        rxPacketsProcessed++;
        wdrv.state.rxMaxContinuousPackets = MAX(wdrv.state.rxMaxContinuousPackets, rxPacketsProcessed);
        
        // Read data into buffer
        uint8_t * packet = startRxReadRxPacket();
        
        // When we have at least one stored then process it whilst we wait on 
        // the next packet to be read
        if (packetsEnd - packetsStart > 0) {
            // Process whilst spi cmd is being transferred
            processRxPacket(&wdrv.rxPacketData[packetsStart * 33]);

            wAgentProcessWirelessRxPacket(tWirelessMicrobusItfState * state, tWirelessPacket * wPacket, uint8_t wirelessSlotId)

            INCR_AND_WRAP(packetsStart, WIRELESS_RX_PACKET_BUFFER_SIZE);
        }
        
        // Wait for read to finish
        while (!spiReady) {
            ;
        }
        INCR_AND_WRAP(packetsEnd, WIRELESS_RX_PACKET_BUFFER_SIZE);

        // Clear status register ready for next read
        nrf24SetRegisterByte(STATUS_REGISTER, 1 << RX_DR); 
        
        // Check if data still in fifo
        uint8_t fifoStatus;
        getRegisterByte(FIFO_STATUS_REGISTER, &fifoStatus);
        if ((fifoStatus & 0x1)) {
            break;
        }
    }
    
    // Now the rx burst is finished process any stored frames
    while (packetsEnd > packetsStart) {
        processRxPacket(&wdrv.rxPacketData[packetsStart * 33]);
        INCR_AND_WRAP(packetsStart, WIRELESS_RX_PACKET_BUFFER_SIZE);
    }
}


#endif


