#include "ErasureCapability.h"
#include <libdevcore/Common.h>
#include <iostream>

namespace dev {
namespace p2p {

ErasureCapability::ErasureCapability(std::shared_ptr<CapabilityHostFace> host)
  : m_host(host) {
    std::cout << "[Erasure] Capability constructed\n";
}

char const* ErasureCapability::packetTypeToString(unsigned packetType) const {
    switch (packetType) {
    case kMsgCommit: return "Commit";
    case kMsgAck:    return "Ack";
    case ECRequestChunkPacket: return "ECRequestChunkPacket";
    case ECResponseChunkPacket: return "ECResponseChunkPacket";
    default:         return "Unknown";
    }
}

void ErasureCapability::onConnect(NodeID const& node, u256 const& /*peerCapVer*/) {
    std::cout << "[Erasure] Connected peer " << node << std::endl;
    broadcastCommit(0);
    // broadcastChunkRequest(1,1);
    // broadcastChunkMessage(1, 1, "hello chunk!");
    broadcastStateRequest(h256{});
    // broadcastStateResponse(h256{}, "hellow state!");
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
    case ECRequestChunkPacket: { // 请求 数据块
        auto number  = r[0][0].toInt<dev::u256>();
        auto chunkID = r[0][1].toInt<dev::u256>();
        cout << "[Erasure] recv EC Request Chunk("
            << number << "," << chunkID <<  ")"
            << "size = "<< r.itemCount() << endl;
        string chunk;
        if(onECRequest) {
            chunk = onECRequest((uint32_t)number, (uint32_t)chunkID); // 触发回调函数，mediator读取数据
            cout << "[Erasure] onECRequest is ok." << endl;
        }
        else{
            cout << "[Erasure] onECRequest is empty, init plz." << endl;
        }
        // 发送 chunk 回去
        if(number == 0){ // 测试专用啊。记得删除！！！！！！！！！！！！！！！！！！！！
            if(chunkID == 0){
                chunk = "hello this is chunk 0";
            }
            if(chunkID == 1){
                chunk = "hello this is chunk 1";
            }
        }

        if(!chunk.empty()){
            sendChunkMessage(node, (size_t)number, (size_t)chunkID, chunk);
        }
        else{
            cout << "[Erasure] Can not find chunk" << endl;
        }
        return true;
    }
    case ECResponseChunkPacket: { 
        cout << "[Erasure] recv EC ResponseChunk size =" << r.itemCount() << endl;
        // if (r.itemCount() < 3) return false;
        auto inner = r[0];
        auto number  = inner[0].toInt<dev::u256>();
        auto chunkID = inner[1].toInt<dev::u256>();
        auto data    = inner[2].toBytes();
        string key(reinterpret_cast<const char*>(data.data()), data.size());
        cout << "[Erasure] recv EC ResponseChunk (num=" << number
                << ", cid=" << chunkID
                << ", size=" << data.size() << ") from " << node << std::endl;
        if(epochChunkProgress.find(CountPair{number, chunkID}) != epochChunkProgress.end()) // init 会先把这些东西插一个空值，表示我要哪个块
            epochChunkProgress[CountPair{number, chunkID}] = key; // 插入“缓存池”等待别人获取
        else{
            cout << "Sorry, this chunk[" << chunkID << "] is no need by replica." << endl;
            return true;
        }
        if(auto it = waitingChunk.find((uint32_t)number); it != waitingChunk.end()){
            uint32_t newCount = it->second.first.fetch_add(1, std::memory_order_relaxed) + 1;
            if (newCount >= it->second.second) {
                // … 先把数据放到 ready_ 之类，再触发回调
                if (onECResponse) onECResponse((uint32_t)number);
            }
        }
        return true;
    }
    case RequestState: { // 请求 state
        
        dev::h256 hash = r[0].toHash<dev::h256>();
        // 回调函数
        cout << "[Erasure] recv Request state hash =" << hash 
                << ", size = " << r.itemCount() << endl;
        string state;
        if(onStateRequest) {
            state = onStateRequest(hash); // 触发回调函数，mediator读取数据
            if(hash == h256{}) { // 测试专用啊。记得删除！！！！！！！！！！！！！！！！！！！！
                state = "Im test state.";
            }
            cout << "[Erasure] onStateRequest is ok." << endl;
        }
        else{
            cout << "[Erasure] onStateRequest is empty, init plz." << endl;
        }
        // 发送状态回去
        if(!state.empty()){
            sendStateResponse(node, hash, state);
        }
        else{
            cout << "[Erasure] Can not find " << hash << endl;
        }

        return true;
    }
    case ResponseState: { // 收到其他人来的 state
        cout << "[Erasure] recv ResponseState size =" << r.itemCount() << endl;
        // if (r.itemCount() < 3) return false;
        auto inner = r[0];
        h256 hash  = inner[0].toHash<h256>();
        auto state = inner[1].toBytes();
        string state_string(reinterpret_cast<const char*>(state.data()), state.size());
        cout << "[Erasure] recv  ResponseState (hash=" << hash
                << ", size=" << state.size() << ") from " << node << std::endl;
        StateProgress.emplace(hash, state_string); // 插入“缓存池”等待别人获取
        if (onStateResponse) onStateResponse(hash);
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

void ErasureCapability::initECgroup(const uint32_t& number, const uint32_t& k, const vector<uint8_t>& group){
    // 1. 清空老数据
    vector<CountPair> del;
    for (auto it = epochChunkProgress.begin(); it != epochChunkProgress.end(); it++) {
        if (it->first.first == number) {
            // it = epochChunkProgress.unsafe_erase(it); // 删除并拿到下一个
            del.push_back(it->first);
        }
    }
    for(auto& key: del){
        epochChunkProgress.unsafe_erase(key);
    }
    if(auto it = waitingChunk.find(number); it != waitingChunk.end()){
        waitingChunk.unsafe_erase(it);
    }

    // 2. 插入新数据
    for(auto& chunkID: group){
        epochChunkProgress.emplace(CountPair{number, (uint32_t)chunkID}, string{});
    }
    waitingChunk.emplace(
        std::piecewise_construct,
        std::forward_as_tuple(number),            // key 就地构造
        std::forward_as_tuple(                    // mapped(ChunkCounter) 就地构造
            std::piecewise_construct,
            std::forward_as_tuple(0u),            // std::atomic<uint32_t>(0)
            std::forward_as_tuple(k)              // uint32_t(k)
        )
    );
    
}

void ErasureCapability::sendChunkRequest(NodeID const& peer, size_t block_number, size_t chunkID){
    if (!m_host) return;
    RLPStream s;
    m_host->prep(peer, name(), s, ECRequestChunkPacket, 1);
    s.appendList(2);
    s << (u256)block_number
      << (u256)chunkID;

    m_host->sealAndSend(peer, s);
}

void ErasureCapability::broadcastChunkRequest(size_t block_number, size_t chunkID) {
    if (!m_host) return;
    m_host->foreachPeer(name(), [&](NodeID const& pid) {
        sendChunkRequest(pid, block_number, chunkID);
        return true;
    });
}

void ErasureCapability::sendChunkMessage(NodeID const& peer, size_t block_number, size_t chunkID, string chunk){
    if (!m_host) return;
    RLPStream s;
    m_host->prep(peer, name(), s, ECResponseChunkPacket, 1);
    s.appendList(3);
    s << (u256)block_number
      << (u256)chunkID
      << bytes(chunk.begin(), chunk.end());

    m_host->sealAndSend(peer, s);
}

void ErasureCapability::broadcastChunkMessage(size_t block_number, size_t chunkID, string chunk) {
    if (!m_host) return;
    m_host->foreachPeer(name(), [&](NodeID const& pid) {
        sendChunkMessage(pid, block_number, chunkID, chunk);
        return true;
    });
}

void ErasureCapability::sendStateRequest(NodeID const& peer, dev::h256 hash) {
    if (!m_host) return;
    RLPStream s;
    m_host->prep(peer, name(), s, RequestState, 1);
    s << hash;

    m_host->sealAndSend(peer, s);
}

void ErasureCapability::broadcastStateRequest(dev::h256 hash) {
    if (!m_host) return;
    m_host->foreachPeer(name(), [&](NodeID const& pid) {
        sendStateRequest(pid, hash);
        return true;
    });
}

void ErasureCapability::sendStateResponse(NodeID const& peer, dev::h256 hash, string state) {
    if (!m_host) return;
    RLPStream s;
    m_host->prep(peer, name(), s, ResponseState, 1);
    s.appendList(2);
    s << hash << bytes(state.begin(), state.end());

    m_host->sealAndSend(peer, s);
}

void ErasureCapability::broadcastStateResponse(dev::h256 hash, string state) {
    if (!m_host) return;
    m_host->foreachPeer(name(), [&](NodeID const& pid) {
        sendStateResponse(pid, hash, state);
        return true;
    });
}

bool ErasureCapability::tryGetState(const h256& hash, string& out) {
    auto it = StateProgress.find(hash);
    if(it == StateProgress.end()) return false;
    out = it -> second; 
    StateProgress.unsafe_erase(it);
    return true;
}

} // namespace p2p
} // namespace dev
