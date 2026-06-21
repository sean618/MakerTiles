/*
 * Copyright © 2024 - 2024 Sean Bremner. All rights reserved.>>
 */

#pragma once

#include <cstdint>
#include <cstring>

#include "fpCommon.hpp"
#include "fpProcessor.hpp"

namespace fp {

class Node {
public:

    void init(FpInterface& interface, BoardInfo& boardInfo, const FieldTable* const* tables, uint8_t numTables) {
        itf = &interface;
        processor.init(boardInfo, tables, numTables, false);
    }

    // Accessor for the node's board state (used by the tests).
    FpBoard& board() { return processor.getBoard(); }
    const FpBoard& board() const { return processor.getBoard(); }

    // Publish a range of fields to the master without waiting for a request
    // (used for streaming/continuous sends from the application thread).
    void sendFields(FieldIndex startField, FieldIndex numFields) {
        processor.sendFields(startField, numFields);
    }

    void processRxTx() {
        uint8_t nodeId = itf->getNodeId();
        processor.setNodeId(nodeId);
        if (nodeId == kInvalidNodeId || nodeId == kUnallocatedNodeId) {
            return;
        }

        while (true) {
            // Process all outgoing responses (tx)
            while (processor.outstandingOutgoingResponses()) {
                BusFieldPacket txPacket;
                txPacket.p = reinterpret_cast<RawBusFieldPacket*>(itf->allocateTxPacket());
                txPacket.dataSize = 0;
                if (txPacket.p == nullptr) {
                    return;
                }
                processor.processOutgoingResponses(*txPacket.p, txPacket.dataSize);
                itf->submitTxPacket(kMasterNodeId, txPacket.dataSize + kFpBusHeaderBytes);
            }

            // Process the next incoming packet (rx)
            fpAssert(processor.outstandingOutgoingResponses() == false, "");
            BusFieldPacket rxPacket;
            uint8_t srcNodeId = 0;
            uint16_t numBytes = 0;
            rxPacket.p = reinterpret_cast<RawBusFieldPacket*>(itf->peekRxPacket(numBytes, srcNodeId));
            if (rxPacket.p == nullptr) {
                return;
            }
            rxPacket.dataSize = numBytes - kFpBusHeaderBytes;
            if (processor.processRxPacket(*rxPacket.p, rxPacket.dataSize)) {
                // Remove the packet if it was successfully processed - else try again next time.
                itf->popRxPacket();
            } else {
                return;
            }
        }
    }

private:

    FpInterface* itf = nullptr;
    FpProcessor processor;
};

}  // namespace fp
