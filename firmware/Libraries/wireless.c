/*
* Copyright © 2024 - 2024 Sean Bremner. All rights reserved.>>
*/

#include "project.h"
#ifdef ENABLE_WIRELESS_MODULE


#include <stdio.h>
#include <math.h>
#include <string.h>
#include <stdint.h>

#include "useful.h"
#include "microbus.h"
#include "nrf24.h"
#include "wirelessProtocol.h"


TODO:
- add CRC to microbus packet! and check before adding to microbus





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

#define NUM_NODES_SCHEDULED 4


#define BUFFER_EMPTY(buffer) (QUEUE_EMPTY((buffer).start, (buffer).end, (buffer).size))

#define INCR_AND_WRAP(val, max_val) \
    (val)++; \
    if ((val) == (max_val)) {\
        (val) = 0; \
    }


// #define USE_CRC

#define WIRELESS_RX_PACKET_BUFFER_SIZE 3 // They should be processed pretty much straight away
#define WIRELESS_TX_PACKET_BUFFER_SIZE 50 // (* 34 bytes) => 1.6kB

#define INVALID_SLOT_AGENT_ID 0
#define MASTER_SLOT_AGENT_ID 1
#define UNALLOCATED_SLOT_AGENT_ID 255

#define SLOT_CYCLE_US 1500 // (1350/1500 = 90%)
#define SLOT_MARGIN_US 150 // Dead time between the end of one module's slot and the start of the next - to allow for overrunning and phase differences


#define SLOT_DATA_TRANSFER_US (SLOT_CYCLE_US - SLOT_MARGIN_US)
#define RX_START_TIME_ADJUSTMENT_US (0 + 64 + 256 + 785) // It takes 256us to transmit 32 bytes at 2MBps then 64us to tranfer it via SPI at 8Mbps plus 785 from other factors


#define MAX_AGENTS 32

typedef enum { W_UNASSIGNED, W_MASTER, W_NODE } eWirelessMode;

typedef struct {
    uint8_t numAgents;
    uint8_t agentTTL[MAX_AGENTS]; // 32 bytes
} tWirelessMasterInfo;

typedef struct {
    // TODO: review
    uint32_t txNumPackets;
    uint32_t txMissedInterrupt;
    uint32_t txDropped;
    uint64_t txPacketBufferOverflows;
    
    uint32_t rxNumTimesOverSlotPeriod;
    uint32_t rxEmptyPackets;
    uint32_t rxNumPackets;
    uint32_t rxLostBursts;
    uint32_t rxBadCrcPackets;
    uint32_t numLostSync;
    uint32_t rxMissedInterrupt;
    uint16_t rxMaxContinuousPackets;
} tWirelessStats;

typedef struct {
    tNode mbNode;
    tNetworkManager wNwManager;
    tSchedulerState wScheduler;
} tWirelessMasterState;

typedef struct {
    tMaster mbMaster;

} tWirelessNodeState;

typedef struct sWirelessDriver {
    bool isWController;
    tWirelessState state;
    uint8_t channel;
    uint64_t nextSlotTimeUs;
    bool firstRxBurstPacket;

    // uint32_t cycle;
    // uint64_t uuid;
    // uint8_t slotsTimeNextSchedule;
    // tNodeIndex nextSlotAgentIds[8];
    // uint8_t packetIndex;



    uint32_t mbSpiIndex;
    SPI_HandleTypeDef * mbSpi;
    GPIO_TypeDef* misoGpioX;
    uint16_t misoGpioPin;
    uint16_t psGpioPin;
    TIM_HandleTypeDef *tim200us;
    TIM_HandleTypeDef *tim1ms;
} WirelessDriver;

static WirelessDriver wdrv = {};
const size_t tmpSize = sizeof(wdrv);

// ======================= //
// Field table

// // TODO: make sure this doesn't end in a deadlock as it tries to send an SPI command in the middle of others
// static void nrf24SetChannelField(tFieldEntry * field, tFieldIndex fieldIndex, tFieldIndex numFields, uint8_t * data) {
//     uint8_t value = data[0];
//     value = MIN(value, 128);
//     wdrv.channel = value;
//     // TODO: needs to write it to flash
//     nrf24SetRegisterByte(RF_CHANNEL_REGISTER, wdrv.channel); // 2.402 GHz
//     memset(&nrf24.state, 0, sizeof(nrf24.state)); // Reset all the state
// }

const tFieldEntry wirelessFields[] = {
    {&wdrv.channel,                  "channel",                 1, FIELD_DATA_TYPE_UINT,   1,  GETTABLE | SETTABLE, nrf24SetChannelField, NULL, NULL},
    {&wdrv.stats.rxLostPackets,      "rx_lost_packets",         1, FIELD_DATA_TYPE_UINT,   4,  GETTABLE | SETTABLE, NULL, NULL, NULL},
}

// ======================= //


void SPI_SetMode(SPI_HandleTypeDef *hspi, uint32_t mode) {
    // 1. Deinit the SPI peripheral
    HAL_SPI_DeInit(hspi);

    // 2. Change the mode
    hspi->Init.Mode = mode;

    // 3. Reinit the SPI peripheral
    HAL_SPI_Init(hspi);
}

// ======================= //


void initialiseWireless(GPIO_TypeDef* enableGpio, uint16_t enableGpioPin,
                            GPIO_TypeDef* interruptGpio, uint16_t interruptGpioPin, 
                            GPIO_TypeDef* csnGpio, uint16_t csnGpioPin,
                            CRC_HandleTypeDef * hcrc,
                            TIM_HandleTypeDef * usTimer,
                            SPI_HandleTypeDef * wspi) {
    

    nrf24Initialise(
        enableGpio, enableGpioPin,
        interruptGpio, interruptGpioPin, 
        csnGpio, csnGpioPin,
        hcrc,
        usTimer,
        wspi
    );
    
    myAssert(HAL_TIM_Base_Start(wdrv.usTimer) == HAL_OK, "Failed to start timer");

    // myAssert(HAL_TIM_Base_Start(&htim1) == HAL_OK, "Failed to start timer");
    // myAssert(HAL_TIM_Base_Start(&htim3) == HAL_OK, "Failed to start timer");

    initialiseWireless();
}

// TODO: callbacks

void setupMicrobusSpiMode(bool master) {
    // Zero the state
    memset(&wdrv.masterState, 0, sizeof(wdrv.masterState));
    memset(&wdrv.nodeState, 0, sizeof(wdrv.nodeState));

    // TODO: deinit protocol

    if (master) {
        // Change the SPI configuration
        SPI_SetMode(&hspi1, SPI_MODE_MASTER);
        // Initialise the field protocol/microbus
        void hwMasterInit(
            tMaster * master,
            SPI_HandleTypeDef *hspi,
            uint32_t spiIndex,
            GPIO_TypeDef * psGpioGroup,
            uint16_t psGpioPin,
            TIM_HandleTypeDef * usTimer);
    } else {
        // Change the SPI configuration
        SPI_SetMode(&hspi1, SPI_MODE_SLAVE);
        // Initialise the field protocol/microbus
        protocolInit(
            const tFieldTable * fieldTables[],
            uint8_t numTables,
            tBoardInfo * boardInfo,
            SPI_HandleTypeDef *hspi, 
            uint32_t spiIndex,
            GPIO_TypeDef* misoGpioX,
            uint16_t misoGpioPin,
            TIM_HandleTypeDef *htim,
            uint16_t psGpioPin);
        )
    }
}

void setupWirelessMode(bool isWController) {
    wirelessProtocolInit(&wdrv.state, isWController);
}


static void determineWirelessMode() {
    // Listen on microbus
    setupMicrobusSpiMode(false); // slave

    while (wdrv.mode == W_UNASSIGNED) {
        // Listen on radio
        radioReceive();

        // If wireless packets we must a w_node and mb_master
        if () {
            wdrv.mode = W_NODE;
            setupWirelessMode(false); // slave
            setupMicrobusSpiMode(true); // master

            TODO: change PS pin to GPIO output!

        }

        // If spi packets we must a w_master and mb_node
        if () {
            wdrv.mode = W_MASTER;
            setupWirelessMode(true); // master
            // Should already in slave mode
            // setupMicrobusSpiMode(false); // slave

            TODO: change PS pin to interrupt!
        }
    }
}

// Called every 1ms
void loopWireless(uint32_t ticks) {
    // Never exit
    while (1) {
        // TODO: we need to check when switching to receive that we don't
        // receive any packets on the first attempt! That way we know we haven't 
        // started too late and missed the slot start

        if (wdrv.mode == W_UNASSIGNED) {
            determineWirelessMode();
        }

        // Update every slot - we might have missed slots
        // because receiving packets took too long - hopefully 
        // shouldn't happen but just in case it
        bool newSlot = false;
        while (wGetUs() > wdrv.state.nextSlotTimeUs) {
            // Start of a new slot
            newSlot = true;
            wdrv.state.nextSlotTimeUs += SLOT_CYCLE_US;
            wdrv.state.firstRxBurstPacket = 1;
            wirelessUpdateSchedule();
        }

        if (newSlot) {
            tNodeIndex ourNodeId = wdrv.isWController ? MASTER_NODE_ID : wdrv.state.agent.wnodeId; 
            if (nextWNodeId == ourNodeId) {
                radioTransmit();
            } else {
                radioReceive();
            }
        } else {
            // Continuation of slot
            radioReceive();
        }
    }


}








#endif

