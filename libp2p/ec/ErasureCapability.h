#pragma once

#include <libp2p/Capability.h>
#include <libp2p/CapabilityHost.h>
#include <libdevcore/RLP.h>
#include <memory>

namespace dev {
namespace p2p {

/// 自定义子协议：Erasure
/// 用于演示如何扩展 devp2p 协议
class ErasureCapability : public CapabilityFace
{
public:
    enum : unsigned {
        kMsgCommit = 0,   ///< 示例：commit 消息
        kMsgAck    = 1,   ///< 示例：ack 消息
        kMessageCount = 2
    };

    explicit ErasureCapability(std::shared_ptr<CapabilityHostFace> host);

    // === CapabilityFace 接口 ===
    std::string name() const override { return "era"; }   // 协议名
    unsigned version() const override { return 1; }       // 协议版本
    CapDesc descriptor() const override { return {name(), version()}; }
    unsigned messageCount() const override { return kMessageCount; }
    char const* packetTypeToString(unsigned packetType) const override;
    std::chrono::milliseconds backgroundWorkInterval() const override {
        return std::chrono::milliseconds{0};
    }

    void onConnect(NodeID const& node, u256 const& peerCapVer) override;
    void onDisconnect(NodeID const& node) override;
    bool interpretCapabilityPacket(NodeID const& node, unsigned id, RLP const& r) override;
    void doBackgroundWork() override {}

    // === 对外 API ===
    void sendCommit(NodeID const& peer, int value);
    void broadcastCommit(int value);

private:
    std::shared_ptr<CapabilityHostFace> m_host;
};

} // namespace p2p
} // namespace dev
