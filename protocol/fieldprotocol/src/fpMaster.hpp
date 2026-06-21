/*
 * Copyright © 2024 - 2024 Sean Bremner. All rights reserved.>>
 */

#pragma once

#include <cstdint>
#include <cstring>
#include <functional>

#include "fpCommon.hpp"
#include "fpNode.hpp"

namespace fp {

using ConnectedNodesFn = std::function<void(uint8_t (&connectedNodesBitfield)[kMaxNumNodes / 8])>;
using NumTxInUsbBufferFn = std::function<uint32_t()>;

// TODO: come back to this
#ifndef RETURN_CREDITS_BURST_SIZE
inline constexpr uint32_t kReturnCreditsBurstSize = 5;
#else
inline constexpr uint32_t kReturnCreditsBurstSize = RETURN_CREDITS_BURST_SIZE;
#endif

class Master {
public:

    void init(FpInterface& daemonItf, FpInterface& microbusItf, BoardInfo& boardInfo, 
              const FieldTable* const* tables, uint8_t numTables,
              ConnectedNodesFn getConnectedNodes, NumTxInUsbBufferFn getNumTx,
              uint32_t maxRx, uint32_t maxTx) {

        daemonItf_ = &daemonItf;
        microbusItf_ = &microbusItf;
        // TODO: fix
        getNumTxInUsbBuffer_ = std::move(getNumTx);
        getConnectedNodesBitField_ = std::move(getConnectedNodes);
        maxRxCredits_ = maxRx;
        maxTxCredits_ = maxTx;

        // The master hosts its own node-like board, so it must honour the
        // settable flag like a node (isDaemon = false); only the daemon mirrors
        // read-only fields unconditionally.
        processor_.init(boardInfo, tables, numTables, false);
    }

    // Accessor for the master's own board state (used by the tests).
    FpBoard& board() { return processor_.getBoard(); }
    const FpBoard& board() const { return processor_.getBoard(); }

    // Publish a range of the master's own fields to the daemon without waiting
    // for a request (used for streaming/continuous sends).
    void sendFields(FieldIndex startField, FieldIndex numFields) {
        processor_.sendFields(startField, numFields);
    }

    void processAllRx(uint8_t numTxPacketsFreed) {
        if (numTxPacketsFreed > 0) {
            returnTxCredits_ += numTxPacketsFreed;
        }
        processDaemonRx();
        processMicrobusRx();
    }

    // All packets received from the microbus interface just get passed straight to the daemon
    void processMicrobusRx() {
        while (true) {
            // Get an rx packet.
            BusFieldPacket packet;
            uint8_t srcNodeId = 0;
            uint16_t numBytes = 0;
            packet.p = reinterpret_cast<RawBusFieldPacket*>(microbusItf_->peekRxPacket(numBytes, srcNodeId));
            if (packet.p == nullptr) {
                return;
            }
            packet.dataSize = numBytes - kFpBusHeaderBytes;
            if (packet.dataSize > kMaxFpPacketDataSize) {
                fpAssert(false, "Peek data size over max!");
                microbusItf_->popRxPacket();
            }

            // Copy the incoming mb packet (over microbus) to the outgoing daemon packet (over USB).
            DaemonFieldPacket daemonTxPacket;
            if (!daemonAllocateTxPacket(daemonTxPacket)) {
                fpAssert(false, "Master to Daemon Tx buffer full");
                return;
            }
            daemonTxPacket.dataSize = packet.dataSize;
            std::memcpy(&daemonTxPacket.p->bp, packet.p, packet.dataSize + kFpBusHeaderBytes);
            daemonSubmitTxPacket(daemonTxPacket, srcNodeId);
            microbusItf_->popRxPacket();
        }
    }

    // Packets from the daemon are either for the master or get passed onto the microbus interface
    void processDaemonRx() {
        // NOTE!!
        // This is a little state machine that is constantly reading responses and then sending out packets until 
        // the daemon tx buffer is full, the microbus tx buffer is full or the rx buffer is empty
        while (true) {
            // Process the next rx packet from the daemon if all the outgoing responses have been handled
            if (processor_.outstandingOutgoingResponses() == false && masterResponseState_.command == Command::None) {
                DaemonFieldPacket daemonRxPacket;
                if (!daemonPeekRxPacket(daemonRxPacket)) {
                    break;
                }
                const uint8_t dstNodeId = daemonRxPacket.p->srcOrDstNodeId;
                if (dstNodeId == kMasterNodeId) {
                    processDaemonToMasterPacket(daemonRxPacket);
                } else {
                    // If it's not for the master send it over the microbus
                    if (sendDaemonRxPacketOverMicrobus(dstNodeId, daemonRxPacket) == false) {
                        break;
                    }
                }
                daemonPopRxPacket();
                returnRxCredits_++;
            }

            // Handle any outgoing responses to the daemon
            while (processor_.outstandingOutgoingResponses() || masterResponseState_.command != Command::None) {
                DaemonFieldPacket daemonTxPacket;
                if (!daemonAllocateTxPacket(daemonTxPacket)) {
                    return;
                }
                processOutgoingResponsesToDeamon(daemonTxPacket);
                daemonSubmitTxPacket(daemonTxPacket, 0);
            }
        }

        // If we've got credits to return and no tx packets in the buffer to return them with then
        // send a special packet just to return them.
        if (getNumTxInUsbBuffer_() == 0 && (returnRxCredits_ + returnTxCredits_) > kReturnCreditsBurstSize) {
            DaemonFieldPacket daemonTxPacket;
            if (daemonAllocateTxPacket(daemonTxPacket)) {
                daemonTxPacket.p->bp.command = static_cast<uint8_t>(Command::ReturningCredits);
                daemonTxPacket.p->bp.requestId = 0;
                daemonTxPacket.dataSize = 0;
                daemonSubmitTxPacket(daemonTxPacket, kMasterNodeId);
            }
        }
    }


private:


    void processDaemonToMasterPacket(DaemonFieldPacket& daemonRxPacket) {
        const Command command = static_cast<Command>(daemonRxPacket.p->bp.command);
        switch (command) {
            case Command::GetConnectedNodes:
            case Command::ResetAndGetMaxBufferCredits:
                // For master specific requests - store the state to be processed when tx packets are available
                fpAssert(masterResponseState_.command == Command::None, "");
                masterResponseState_.command = command;
                masterResponseState_.requestId = daemonRxPacket.p->bp.requestId;
                masterResponseState_.numFields = daemonRxPacket.p->bp.numFields;
                masterResponseState_.startField = daemonRxPacket.p->bp.fieldIndex;
                break;
            default:
                // For general node like requests the processor_ will handle it
                fpAssert(processor_.processRxPacket(daemonRxPacket.p->bp, daemonRxPacket.dataSize) == true, "");
        }
        returnTxCredits_++;

    }

    void processOutgoingResponsesToDeamon(DaemonFieldPacket& daemonTxPacket) {
        if (processor_.outstandingOutgoingResponses()) {
            processor_.processOutgoingResponses(daemonTxPacket.p->bp, daemonTxPacket.dataSize);
        } else {
            switch (masterResponseState_.command) {
                case Command::GetConnectedNodes:
                    sendGetConnectedNodesResponse(daemonTxPacket, masterResponseState_.requestId);
                    masterResponseState_ = OutgoingResponseState{};
                    break;
                case Command::ResetAndGetMaxBufferCredits:
                    returnRxCredits_ = 0;
                    returnTxCredits_ = 0;
                    daemonTxPacket.p->bp.command = static_cast<uint8_t>(Command::SendingMaxBufferCredits);
                    daemonTxPacket.p->bp.requestId = masterResponseState_.requestId;
                    daemonTxPacket.p->bp.data[0] = static_cast<uint8_t>(maxRxCredits_);
                    daemonTxPacket.p->bp.data[1] = static_cast<uint8_t>(maxTxCredits_);
                    daemonTxPacket.dataSize = 2;
                    masterResponseState_ = OutgoingResponseState{};
                    break;
                default:
                    fpAssert(false, "");
            }
        }
    }

    bool sendDaemonRxPacketOverMicrobus(uint8_t dstNodeId, DaemonFieldPacket& daemonRxPacket) {
        uint8_t connectedNodesBitfield[kMaxNumNodes / 8] = {0};
        getConnectedNodesBitField_(connectedNodesBitfield);
        const uint8_t connected =
            (connectedNodesBitfield[dstNodeId / 8] >> (7 - (dstNodeId % 8))) & 0x1;
        if (connected) {
            // Copy the incoming daemon packet (over USB) to the outgoing node packet (over microbus).
            BusFieldPacket nodeTxPacket;
            nodeTxPacket.p = reinterpret_cast<RawBusFieldPacket*>(microbusItf_->allocateTxPacket());
            if (nodeTxPacket.p == nullptr) {
                return false;
            }
            nodeTxPacket.dataSize = daemonRxPacket.dataSize;
            const uint16_t packetSize = daemonRxPacket.dataSize + kFpBusHeaderBytes;
            std::memcpy(nodeTxPacket.p, &daemonRxPacket.p->bp, packetSize);
            microbusItf_->submitTxPacket(dstNodeId, packetSize);
        }
        return true;
    }

    bool daemonAllocateTxPacket(DaemonFieldPacket& txPacket) {
        txPacket.p = reinterpret_cast<RawDaemonFieldPacket*>(daemonItf_->allocateTxPacket());
        txPacket.dataSize = 0;
        return txPacket.p != nullptr;
    }

    void daemonSubmitTxPacket(DaemonFieldPacket& txPacket, uint8_t srcNodeId) {
        txPacket.p->packetSize = txPacket.dataSize + kFpDaemonHeaderBytes;
        txPacket.p->srcOrDstNodeId = srcNodeId;
        txPacket.p->returnRxCredits = static_cast<uint8_t>(returnRxCredits_);
        txPacket.p->returnTxCredits = static_cast<uint8_t>(returnTxCredits_);

        returnRxCredits_ = 0;
        returnTxCredits_ = 0;
        if (txPacket.dataSize <= kMaxFpPacketDataSize) {
            daemonItf_->submitTxPacket(0, txPacket.dataSize + kFpDaemonHeaderBytes);
        } else {
            fpAssert(false, "");
            txPacket.dataSize = 0;
        }
    }

    bool daemonPeekRxPacket(DaemonFieldPacket& rxPacket) {
        uint16_t numBytes = 0;
        uint8_t unused = 0;
        rxPacket.p = reinterpret_cast<RawDaemonFieldPacket*>(daemonItf_->peekRxPacket(numBytes, unused));
        if (rxPacket.p != nullptr) {
            fpAssert(numBytes == rxPacket.p->packetSize, "");
            rxPacket.dataSize = numBytes - kFpDaemonHeaderBytes;
            fpAssert(rxPacket.dataSize < kMaxFpPacketDataSize, "");
            return true;
        }
        return false;
    }

    void daemonPopRxPacket() {
        daemonItf_->popRxPacket();
    }

    // TODO: Need to send requestAcked packets.
    // Need to handle any requests to the master node fields itself.

    void sendGetConnectedNodesResponse(DaemonFieldPacket& txPacket, RequestId requestId) {
        uint8_t connectedNodesBitfield[kMaxNumNodes / 8] = {0};
        getConnectedNodesBitField_(connectedNodesBitfield);
        txPacket.p->bp.command = static_cast<uint8_t>(Command::SendingConnectedNodes);
        txPacket.p->bp.requestId = requestId;
        std::memcpy(&txPacket.p->bp.data[0], connectedNodesBitfield, kMaxNumNodes / 8);
        txPacket.dataSize += kMaxNumNodes / 8;
        if (txPacket.dataSize >= kMaxFpPacketDataSize) {
            fpAssert(false ,"");
            txPacket.dataSize = kMaxFpPacketDataSize;
        }
    }

    OutgoingResponseState masterResponseState_;
    FpProcessor processor_;
    FpInterface* daemonItf_ = nullptr;
    FpInterface* microbusItf_ = nullptr;
    ConnectedNodesFn getConnectedNodesBitField_;
    NumTxInUsbBufferFn getNumTxInUsbBuffer_;
    uint32_t maxRxCredits_ = 0;
    uint32_t maxTxCredits_ = 0;
    uint32_t returnRxCredits_ = 0;
    uint32_t returnTxCredits_ = 0;
    
};

}  // namespace fp
