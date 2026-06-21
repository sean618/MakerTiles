/*
 * Copyright © 2024 - 2024 Sean Bremner. All rights reserved.>>
 */

#pragma once

#include <cstdint>
#include <cstring>

#include "fpCommon.hpp"



namespace fp {

// An state machine for processing both responses to requests and internal publishes
class FpProcessor {
public:

    void init(BoardInfo& boardInfo, const FieldTable* const* tables, uint8_t numTables, bool isDaemon) {
        boardInfo.numFields = 0;
        for (uint8_t t = 0; t < numTables; t++) {
            boardInfo.numFields += tables[t]->numFields;
        }
        board.boardInfo = boardInfo;
        board.tables = const_cast<FieldTable**>(tables);
        board.numTables = numTables;
        this->isDaemon = isDaemon;
    }

    void setNodeId(uint8_t nodeId) {
        board.nodeId = nodeId;
    }

    // Accessor for the owned board state (used by the master and the tests).
    FpBoard& getBoard() { return board; }
    const FpBoard& getBoard() const { return board; }

    // From Node -> Master - internal call 
    void sendFields(FieldIndex startField, FieldIndex numFields) {
        publishFields.command = Command::GetFields;  // Pretend it's a request for fields.
        publishFields.startField = startField;
        publishFields.numFields = numFields;
    }

    // Process a single received packet. Returns false if the packet could not
    // be handled yet and should be retried. Used by the master as well.
    bool processRxPacket(RawBusFieldPacket& rxPacket, uint16_t dataSize) {
        const Command command = static_cast<Command>(rxPacket.command);
        // Check we've finished the previous command.
        if (rxRequestSendFields.command != Command::None) {
            return false;
        }
        switch (command) {
            case Command::GetBoardInfo:
            case Command::GetFieldInfo:
            case Command::GetFields: {
                if (command == Command::GetBoardInfo) {
                    fpAssert(rxPacket.numFields == 4, "");
                } else if (command == Command::GetFieldInfo) {
                    fpAssert(rxPacket.numFields == 1, "");
                }
                rxRequestSendFields.command = command;
                rxRequestSendFields.requestId = rxPacket.requestId;
                rxRequestSendFields.numFields = rxPacket.numFields;
                rxRequestSendFields.startField = rxPacket.fieldIndex;
                break;
            }
            case Command::SetBoardInfo:
                rxSetBoardInfoCustomName();
                break;
            case Command::SendingFields:
                // We need to update our fields with the field data sent.
                return updateFields(isDaemon, &board, rxPacket, dataSize, nullptr);
            default:
                fpAssert(false, "Invalid Rx command");
                break;
        }
        return true;
    }

    bool outstandingOutgoingResponses() {
        return (rxRequestSendFields.command != Command::None || publishFields.command != Command::None);
    }

    // Process the outgoing responses
    void processOutgoingResponses(RawBusFieldPacket& txPacket, uint16_t& dataSize) {
        // Process rx requests before internal send commands
        
        OutgoingResponseState& state =
            (rxRequestSendFields.command != Command::None) ? rxRequestSendFields : publishFields;
        fpAssert((state.command != Command::None), "");
        switch (state.command) {
            case Command::GetFields:
                sendFieldsTxPacket(isDaemon, board, state, txPacket, dataSize, nullptr);
                break;
            case Command::GetFieldInfo:
                txFieldInfoResponse(board, state, txPacket, dataSize);
                break;
            case Command::GetBoardInfo:
                txBoardInfoResponse(board.boardInfo, state, txPacket, dataSize);
                break;
            default:
                fpAssert(false, "Invalid send command");
        }
        fpPrintf("Node %u: %s, requestId:%u, startField:%u, numFields:%u, size:%u\n", board.nodeId,
                commandName(static_cast<Command>(txPacket.command)), txPacket.requestId, txPacket.fieldIndex,
                txPacket.numFields, dataSize);
    }


    void rxSetBoardInfoCustomName() {
        // TODO:
    }


private:

    FpBoard board{};
    bool isDaemon{};
    // Command to send fields originating from the main thread.
    OutgoingResponseState publishFields{};
    // Command to send fields originating from an rx get request.
    OutgoingResponseState rxRequestSendFields{};
};

}  // namespace fp