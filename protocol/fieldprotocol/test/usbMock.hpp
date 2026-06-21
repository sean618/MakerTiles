/*
 * Copyright © 2024 - 2024 Sean Bremner. All rights reserved.>>
 */

#pragma once

#include <cstdint>

#include "../src/fpCommon.hpp"
#include "testCommon.hpp"

extern TestInterface usbMockDaemonItf;
extern TestInterface usbMockMasterDaemonItf;

uint32_t usbMockGetNumTxInUsbBuffer();
uint32_t usbMockGetMaxRxCredits();

void usbMockInitNetwork();
void usbMockTransferAllTxToRx();

// For the Python daemon to interact with the system.
uint32_t daemonUsbSendTxPacket(uint8_t* data, uint16_t packetSize);
uint16_t daemonUsbGetRxPacket(uint8_t* data);
uint32_t daemonNumInUsbTxBuffer();
