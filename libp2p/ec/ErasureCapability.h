#pragma once

#include <libp2p/Capability.h>
#include <libp2p/CapabilityHost.h>
#include <libdevcore/RLP.h>
#include <memory>
#include <tbb/tbb.h>

using namespace std;
using namespace dev;

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
        ECRequestChunkPacket = 2, /// 请求Chunk消息
        ECResponseChunkPacket = 3, /// 响应Chunk消息
        RequestState = 4,
        ResponseState = 5,
        kMessageCount = 6
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
    // sending chunk Request
    void sendChunkRequest(NodeID const& peer, size_t block_number, size_t chunkID);
    void broadcastChunkRequest(size_t block_number, size_t chunkID);
    // sending chunk Response
    void sendChunkMessage(NodeID const& peer, size_t block_number, size_t chunkID, string chunk);
    void broadcastChunkMessage(size_t block_number, size_t chunkID, string chunk);
    // sending state Request
    void sendStateRequest(NodeID const& peer, dev::h256 hash);
    void broadcastStateRequest(dev::h256 hash);
    // sending state Response
    void sendStateResponse(NodeID const& peer, dev::h256 hash, string state);
    void broadcastStateResponse(dev::h256 hash, string state);

    using CountPair = pair<uint32_t, uint32_t>; // <block_number(epoch), chunkID>
    struct PairHash {
        std::size_t operator()(const CountPair& p) const noexcept {
            // 简单合并：32 位拼接；或用更好的混合函数
            return (static_cast<std::size_t>(p.first) << 32) ^ static_cast<std::size_t>(p.second);
        }
    };
    struct PairEq {
        bool operator()(const CountPair& a, const CountPair& b) const noexcept {
            return a.first == b.first && a.second == b.second;
        }
    };
    using ChunkCounter = pair<atomic<uint32_t>, uint32_t>; // <recive chunks number, k>
    tbb::concurrent_unordered_map<CountPair, string, PairHash, PairEq> epochChunkProgress;
    tbb::concurrent_unordered_map<uint32_t/*block_number*/, ChunkCounter> waitingChunk;
    function<std::string(uint32_t/*block_number*/, uint32_t/*chunkID*/)> onECRequest; // 回调函数
    function<void(uint32_t/*block_number*/)> onECResponse; // 回调函数
    bool tryGetChunk(const h256& hash, string& out);
    void initECgroup(const uint32_t& number, const uint32_t& k, const vector<uint8_t>& group);

    tbb::concurrent_unordered_map<h256, string> StateProgress;
    function<std::string(h256/*状态hash*/)> onStateRequest; // 请求状态 回调函数
    function<void(h256)> onStateResponse; // 回应状态 回调函数
    bool tryGetState(const h256& hash, string& out);

private:
    std::shared_ptr<CapabilityHostFace> m_host;
};

} // namespace p2p
} // namespace dev
