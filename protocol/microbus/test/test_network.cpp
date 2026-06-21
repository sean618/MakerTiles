// Copyright (c) 2025 Sean Bremner
// Licensed under the MIT License. See LICENSE file for details.
//
// NetworkManager join handshake test.

#include <array>
#include <cassert>
#include <cstdint>

#include "microbus/src/microbus.hpp"

namespace microbus::test {

static void test_new_node_given_id() noexcept {
    uint32_t statsNodeJoined = 0;
    uint32_t networkFullCount = 0;

    // Master side.
    Packet masterPacket{};
    NetworkManager nwManager{};
    std::array<uint8_t, kMaxNodes> nodeTtl{};

    // Node side.
    Packet nodePacket{};
    const uint64_t uniqueId = 7;
    NodeId nodeId = kInvalidNode;
    int32_t timeToLive = 0;

    NodeQueue activeNodes{};
    nwManager.init(&activeNodes);

    // Node asks for an id; master allocates one and broadcasts it; node adopts it.
    makeNewNodeRequest(&nodePacket, uniqueId);
    nwManager.rxNewNodeRequest(nodeTtl.data(), &nodePacket, &networkFullCount);
    nwManager.txNewNodeResponse(&masterPacket);
    rxNewNodeResponse(&masterPacket, uniqueId, &nodeId, &timeToLive, &statsNodeJoined);

    assert(nodeId == kFirstNode);
}

void testNetworkManager() noexcept {
    test_new_node_given_id();
}

}  // namespace microbus::test
