#include "ErasureCapability.h"
#include <libdevcore/Common.h>
#include <iostream>

namespace dev {
namespace p2p {

ErasureCapability::ErasureCapability(std::shared_ptr<CapabilityHostFace> host)
  : m_host(std::move(host)) {
    std::cout << "[Erasure] Capability constructed\n";
}

char const* ErasureCapability::packetTypeToString(unsigned packetType) const {
    switch (packetType) {
    case kMsgCommit: return "Commit";
    case kMsgAck:    return "Ack";
    default:         return "Unknown";
    }
}

void ErasureCapability::onConnect(NodeID const& node, u256 const& /*peerCapVer*/) {
    std::cout << "[Erasure] Connected peer " << node << std::endl;
}

void ErasureCapability::onDisconnect(NodeID const& node) {
    std::cout << "[Erasure] Disconnected peer " << node << std::endl;
}

bool ErasureCapability::interpretCapabilityPacket(NodeID const& node, unsigned id, RLP const& r) {
    switch (id) {
    case kMsgCommit: {
        int v = r[0].toInt<int>();
        std::cout << "[Erasure] recv Commit(" << v << ") from " << node << std::endl;
        return true;
    }
    case kMsgAck: {
        int v = r[0].toInt<int>();
        std::cout << "[Erasure] recv Ack(" << v << ") from " << node << std::endl;
        return true;
    }
    default:
        return false;
    }
}

void ErasureCapability::sendCommit(NodeID const& peer, int value) {
    if (!m_host) return;
    RLPStream s;
    m_host->prep(peer, name(), s, kMsgCommit, 1);
    s << value;
    m_host->sealAndSend(peer, s);
}

void ErasureCapability::broadcastCommit(int value) {
    if (!m_host) return;
    m_host->foreachPeer(name(), [&](NodeID const& pid) {
        sendCommit(pid, value);
        return true;
    });
}

} // namespace p2p
} // namespace dev
