/*
 * Copyright © 2024 - 2024 Sean Bremner. All rights reserved.>>
 */

#pragma once

#include <cstdint>

#include "../src/fpCommon.hpp"
#include "testCommon.hpp"

extern TestInterface microbusMockMasterNodeBusItf;
extern TestInterface microbusMockNodeBusItf[fp::kMaxNumNodes];

void microbusMockGetConnectedNodesBitField(uint8_t connectedNodesBitfield[fp::kMaxNumNodes / 8]);
uint32_t microbusMockGetMaxTxCredits();
void microbusMockInitNetwork(bool nodeActive[]);
void microbusMockTransferAllTxToRx(bool ignoreNode[fp::kMaxNumNodes], uint8_t* numTxPacketsFreed);
