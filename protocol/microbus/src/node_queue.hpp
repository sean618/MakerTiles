// Copyright (c) 2025 Sean Bremner
// Licensed under the MIT License. See LICENSE file for details.
//
// node_queue.hpp - a small fixed-capacity set of node ids with a round-robin
// cursor.
//
// The master keeps several of these to answer questions like "which nodes are
// connected?", "which have data waiting?", and "who should I service next?".
// Membership is a set (no duplicates); iteration via getNextNodeAndIncr() walks
// the members in a stable round-robin so no node is starved.

#pragma once

#include <array>
#include <cstdint>

#include "microbus/src/diagnostics.hpp"
#include "microbus/src/packet.hpp"

namespace microbus {

class NodeQueue {
public:
    // Number of ids currently held. Public because some callers (and the timer
    // thread) read it directly; writes happen last and atomically in add().
    NodeId numNodes = 0;

    // Add a node id. No-op if already present. Returns false only if full.
    // May be called from a separate (lower-priority) thread, hence the
    // count-last ordering.
    bool add(NodeId nodeId) noexcept {
        if (numNodes >= kMaxNodes) {
            MB_ASSERT(false, "NodeQueue full");
            return false;
        }
        for (NodeId i = 0; i < numNodes; ++i) {
            if (ids_[i] == nodeId) {
                return true;  // already a member
            }
        }
        ids_[numNodes] = nodeId;
        ++numNodes;  // publish last so a concurrent reader never sees a hole
        return true;
    }

    // Remove a node id if present (shifting the tail down to stay compact).
    void removeIfExists(NodeId nodeId) noexcept {
        for (NodeId i = 0; i < numNodes; ++i) {
            if (ids_[i] == nodeId) {
                for (NodeId j = i; j + 1 < numNodes; ++j) {
                    ids_[j] = ids_[j + 1];
                }
                --numNodes;
                if (cursor_ >= numNodes) {
                    cursor_ = 0;
                }
                return;
            }
        }
    }

    // Remove a node id that is expected to be present (asserts otherwise).
    void remove(NodeId nodeId) noexcept {
        const NodeId before = numNodes;
        MB_ASSERT(numNodes > 0, "remove from empty NodeQueue");
        removeIfExists(nodeId);
        MB_ASSERT(before == numNodes + 1, "remove of absent node");
        (void)before;
    }

    // Return the node under the round-robin cursor and advance it.
    NodeId getNextNodeAndIncr() noexcept {
        NodeId node = ids_[cursor_];
        ++cursor_;
        if (cursor_ >= numNodes) {
            cursor_ = 0;
        }
        return node;
    }

    // True once the cursor has reached the final member.
    bool reachedEnd() const noexcept {
        return (cursor_ + 1) >= numNodes;
    }

    // Read a member by position (no bounds adjustment; caller stays in range).
    NodeId getNode(NodeId index) const noexcept { return ids_[index]; }

private:
    std::array<NodeId, kMaxNodes> ids_{};
    NodeId cursor_ = 0;
};

}  // namespace microbus
