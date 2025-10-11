/**
 * @中介节点，负责处理客户端的请求，向节点发起状态请求
 *功能包括：
 * 1. 读取状态块（本地和远程）
 * 2. 存储状态分布信息（状态存在的节点和BMT）
 * 3. 通过EC恢复缺失的数据
 * 
 * @file Mediator.h
 * @author qqf
 * @date 2024-10-30
 */

// #include <libblockchain/BlockChainImp.h>
// #include <libledger/DBInitializer.h>
#pragma once

#include "SimpleIni.h"
#include "Eurasure.h"
#include "MPTState.h"
#include <libdevcore/RLP.h>
#include <libethereum/Client.h>
#include <tbb/tbb.h>
#include <tbb/parallel_for.h>
#include <tbb/concurrent_unordered_map.h>
#include <memory>
// #include <tbb/global_control.h>
// #include <tbb/task_scheduler_init.h>
#include <atomic>
#include <boost/optional.hpp>

class Mediator : public std::enable_shared_from_this<Mediator>{
public:
    dev::mptstate::MPTState* mpt_ptr;
    OverlayDB* m_db = NULL;
    rocksdb::DB* ec_db = NULL;
    dev::eth::Client* clt = NULL;
    std::shared_ptr<dev::p2p::ErasureCapability> era_;
    vector<string> Nodes;
    string id;
    uint* nodeArry;
    
    using PromiseStr = std::promise<std::string>;
    using PromiseVec = std::promise<vector<pair<uint32_t, string>>>;
    tbb::concurrent_unordered_map<dev::h256, PromiseStr> pending_state;
    tbb::concurrent_unordered_map<uint32_t, PromiseVec> pending_chunk;

    atomic<int> malicious_nodes{0};
    // std::unordered_map<dev::h256, StateLocation>* location_ptr;

    Mediator(dev::mptstate::MPTState &mptstate, OverlayDB& db, rocksdb::DB& _db){
        mpt_ptr = &mptstate;
        m_db = &db;
        ec_db = &_db;
        // location_ptr = mptstate.stateHashToInfoMap;
    }

    void setNodeid(string self_id){
        id = self_id;
    }

    void setEra(shared_ptr<Mediator> self, std::shared_ptr<dev::p2p::ErasureCapability> const& era) {
        // 如果之前已经有绑定，先解绑（可选）
        cout << "__entry set Era!" << endl;
        if (era_) {
            era_->onECRequest = nullptr;
            era_->onStateRequest = nullptr;
            era_->onStateResponse = nullptr;
            era_->onECResponse = nullptr;
        }
        cout << "de binding!" << endl;
        era_ = era; // 只存 weak_ptr，不转移所有权

        if (era_) {
            // 把回调绑到 Mediator；用 weak_ptr 避免循环引用
            cout << "binding Mediator success!" << endl;
            std::weak_ptr<Mediator> wself = self;
            era_->onECRequest = [wself](uint32_t epoch, uint32_t chunkID) -> std::string {
                if(auto self = wself.lock()) return self->onECRequest(epoch, chunkID);
            };
            era_->onStateRequest = [wself](h256 hash) -> string {
                if(auto self = wself.lock()) return self->onStateRequest(hash);
            };
            era_->onStateResponse = [wself](const h256& hash) {
                if(auto self = wself.lock()) return self->onStateResponse(hash);
            };
            era_->onECResponse = [wself] (uint32_t epoch) {
                if(auto self = wself.lock()) return self->onECResponse(epoch);
            };
            cout << "binding oncall func success!" << endl;
        }
    }

    // 收到"Chunk Request"消息时候的回调函数
    string onECRequest(uint32_t epoch, uint32_t chunkID) {
        std::cout << "Mediator has got Request" << std::endl;
        string chunk = localreadChunk(uint16_t(epoch), (uint8_t)chunkID);
        return chunk;
    }

    // 收到"state Request"消息时候的回调函数
    string onStateRequest(h256 hash) {
        std::cout << "Mediator has got state Request" << hash << std::endl;
        string state = remoteFetchState(hash);
        return state;
    }

    // 收到state response的回调函数
    void onStateResponse(const h256& hash) {
        std::cout << "Mediator has got onStateResponse " << hash << std::endl;
        string payload;
        if(era_) {
            if(!era_->tryGetState(hash, payload)){
                return;
            }
        }
        else{
            return;
        }
        if (auto it = pending_state.find(hash); it != pending_state.end()) {
            it->second.set_value(payload); // 交给线程
            pending_state.unsafe_erase(it);
        }
    }

    // 收到 Chunk response的回调函数
    void onECResponse(uint32_t number) {
        std::cout << "Mediator has got on Chunk Response in " << number << std::endl;
        vector<pair<uint32_t, string>> chunks;
        vector<pair<uint32_t, uint32_t>> del;
        if(era_) {
            auto & epochChunkProgress_ = era_->epochChunkProgress;
            for(auto it = epochChunkProgress_.begin(); it != epochChunkProgress_.end(); ++it){
                if(it->first.first == number){
                    chunks.push_back(make_pair(it->first.second, it->second));
                    del.push_back(make_pair(it->first.first, it->first.second));
                    // cout << "Fetch " << it->first.second << " chunkID, " 
                    // << "chunk len: "<< (it->second).size() << endl;
                    // epochChunkProgress_.unsafe_erase(it);
                }
            }
            for(auto& key: del){
                epochChunkProgress_.unsafe_erase(key);
            }
        }
        else{
            return;
        }

        if (auto it = pending_chunk.find(number); it != pending_chunk.end()) {
            it->second.set_value(chunks); // 交给原线程（这里是sendingstaterequest
            pending_chunk.unsafe_erase(it);
        }
    }
 
    // 补0并且生成新的字符串
    string padWithNullBytes(const string& str, size_t offset){
        return string(offset, '\0') + str;
    }

    // 解除chunk中某一段字符串
    string getSubstring(const string input, size_t offset, size_t len){
        if(offset >= input.size()){
            auto s = string(len + offset, '\0');
            // cout << "OverCut String " << offset << "-" << input.size() 
            //     << "+" << len
            //     << "New str len "<< s.size() << endl;
            return s;
        }
        auto str = input.substr(offset, len);
        // cout << "After cutting in get substring len:" << str.size() << " len " << len << endl;
        return padWithNullBytes(str, offset);
    }

    NodeMetadata readStateNodeMeta(dev::h256 target) {
        // 从目标节点读取 节点id 区块编号
        NodeMetadata rlt; // 返回值放入其中 若为空则找不到
        auto& dataSet = mpt_ptr->versionManager.dataSet;
        if(dataSet.find(target) != dataSet.end()){
            auto it = dataSet.find(target);
            return it->second.second;
        }
        else{
            // cout << "Can't not find NodeMetaData." << endl;
        }
        // std::string ret; // 返回值放入其中 若为空则找不到
        return rlt;
    }

    int locationChunk(dev::h256& target, int location){
        int n = 0;
        for(auto& mTree: (mpt_ptr->BMT_map[location]).MerkleTrees){
            if(mTree.root->hash == target){
                return n;
            }
            else{
                n++;
            }
        }
        return -1;
    }

    inline std::string readChild(const std::string& packed, int idx) {
        if (idx < 0 || idx >= 16) cout << "idx must be 0..15";
        if (packed.empty())       cout << "packed empty";

        size_t data_start = 0;
        std::vector<int> order;

        // 有 "{...}" 前缀：按列出的槽顺序定位
        if (packed[0] == '{') {
            cout << "Read branch node." << endl;
            size_t rb = packed.find('}');
            if (rb == std::string::npos) {
                throw std::runtime_error("Malformed prefix: missing '}'");
            }
            order.reserve(rb - 1);
            for (size_t i = 1; i < rb; ++i) {
                unsigned char c = packed[i];
                if (!std::isxdigit(c)) throw std::runtime_error("Malformed prefix: non-hex char");
                int v = (c <= '9') ? (c - '0')
                    : (c <= 'F' ? 10 + (c - 'A') : 10 + (c - 'a'));
                if (v < 0 || v > 15) throw std::runtime_error("slot out of range in prefix");
                order.push_back(v);
            }
            data_start = rb + 1;

            const size_t need = order.size() * static_cast<size_t>(kChildLen);
            if (packed.size() < data_start + need) {
                throw std::runtime_error("packed too short for prefixed children");
            }

            // 找 idx 在前缀顺序中的位置
            ssize_t pos = -1;
            for (size_t i = 0; i < order.size(); ++i) {
                if (order[i] == idx) { pos = static_cast<ssize_t>(i); break; }
            }
            if (pos < 0) {
                throw std::runtime_error("idx not present in prefix list");
            }

            const size_t off = data_start + static_cast<size_t>(pos) * kChildLen;
            return packed.substr(off, kChildLen); // 返回对应 11B
        }

        // 无前缀：就只有一个 11 字节，直接返回
        if (packed.size() < static_cast<size_t>(kChildLen)) {
            throw std::runtime_error("packed too short: " + std::to_string(packed.size()));
        }

    return packed.substr(0, kChildLen);
}

    // 远端根据元数据取回 ver -> chunk!
    string remoteFetch(string& meta, uint16_t ver){
        // sending meta to replica with version
        string str;
        auto MetaAndVer = ChunkBuilder::deserializeMetadata(meta);
        MetaAndVer.first.printNodeMetadata(); // print meta menber one by one
        uint8_t replicaID = MetaAndVer.first.m_node;
        // fetch in loacl
        scan_epoch_chunk(ec_db, (uint32_t)ver, replicaID);
        return str;
    }

    string rebuildChunk(int block_number, size_t idx){
        // get chunk order
        string k = "O|";
        k.append(dev::toString(block_number));
        k.push_back('|');
        k.append(dev::toString(idx));
        string order_rlt;
        if(ec_db != NULL){
            ec_db->Get(rocksdb::ReadOptions(), k, &order_rlt);
            if(order_rlt.empty()){
                cout << "\x1b[32m[rebuildChunk]\x1b[0m Read data chunk failed" << endl;
                return order_rlt; // 返回空字符串
            }
            else{
                // cout << "\x1b[32m[rebuildChunk]\x1b[0m Read chunk order successful!" << endl;
            }
        }
        auto order_vec_h256 = deserialize_h256_list_raw(order_rlt);
        
        // fetch state and meta from dbs
        auto& state_db = m_db;
        auto& meta_db = ec_db;

        string chunk;
        for(size_t i=0; i<order_vec_h256.size(); i++){
            auto& hash = order_vec_h256[i];
            auto value = state_db -> lookup(hash);
            chunk.append(value);

            string meta; // its children metadata
            meta_db -> Get(rocksdb::ReadOptions(), rocksdb::Slice(reinterpret_cast<const char*>(hash.data()), dev::h256::size), &meta);
            // cout << "Meta:" << meta << endl;
            auto cut = meta.find('}');
            if(cut == string::npos){
                // cout << "Can't found its children meta, maybe it is leaves?" << endl;
            }
            else{
                chunk.append(meta.substr(cut+1));
            }
        }

        // check the correctness
        _MerkleTree mTree(dev::splitStr(chunk, 100));
        h256 hash = mTree.root->hash;
        cout << "\x1b[32m[rebuildChunk]\x1b[0m rebuild chunk hash[" << idx << "]:" << hash << endl; 

        return chunk;
    }
    
    //Fetch remote state
    string remoteFetchState(const h256& childHash){
        // remote read state, insert request sending 
        string str;
        if(m_db == NULL){
            cout << "Open DB failed!" << endl;
        }
        else{
            str = m_db->lookup(childHash);  
        }
        return str;
    }

    string sendingStateRequest(const h256& hash, uint32_t* nodeid = NULL){      
        cout << "[sending State Request]" << endl;
        std::chrono::milliseconds timeout = std::chrono::milliseconds(2000);
        PromiseStr p; auto fut = p.get_future();
        if (auto it = pending_state.find(hash); it != pending_state.end()) pending_state.unsafe_erase(it);
        pending_state.insert({hash, PromiseStr{}}).first->second = std::move(p);
        // era_->broadcastStateRequest(hash);
        if (era_){
            if(nodeid == NULL) { // 就是不知道目的的节点，所以广播看看谁有
                era_->broadcastStateRequest(hash);
            }
            else{ // 定点投送目标的 node
                NodeID target_node = NodeID(Nodes[*nodeid].substr(0, 128));
                era_->sendStateRequest(target_node, hash);
            }
        }
        else { 
            pending_state.unsafe_erase(hash); 
            cout << "Can not find " << endl;
            return {}; 
        }

        if (fut.wait_for(timeout) == std::future_status::ready) {
            auto s = fut.get(); 
            pending_state.unsafe_erase(hash); 
            cout << "Find State " << hash << endl;
            return s;
        } 
        else { 
            pending_state.unsafe_erase(hash); 
            cout << "Time out" << endl;
            return {}; 
        }  
    }

    static uint32_t nodeChunkMapping(uint32_t chunkId) { return chunkId; };

    using Fn = uint32_t(*)(uint32_t);

    vector<pair<uint32_t, string>> sendingChunkRequest(uint32_t number, const deque<uint8_t>& chunkIDqueue, Fn fn = NULL){      
        cout << "[sending Chunk Request]" << endl;
        std::chrono::milliseconds timeout = std::chrono::milliseconds(5000);
        PromiseVec p; auto fut = p.get_future();
        if (auto it = pending_chunk.find(number); it != pending_chunk.end()) pending_chunk.unsafe_erase(it);
        pending_chunk.insert({number, PromiseVec{}}).first->second = std::move(p);
        
        if (era_) {
            for(auto chunkID: chunkIDqueue){
                if(fn == NULL){
                    era_->broadcastChunkRequest(number, (uint32_t)chunkID);
                }
                else {
                    NodeID target_node = NodeID(Nodes[fn((uint32_t)chunkID) % Nodes.size()].substr(0, 128));
                    era_->sendChunkRequest(target_node, number, (uint32_t)chunkID);
                    cout << "[sending Chunk Request] sending chunk " << (uint32_t)chunkID
                        << " to " << target_node << endl;
                }
            }
        }
        else { 
            pending_chunk.unsafe_erase(number); cout << "Can not find Era" << endl;
            return {}; 
        }

        if (fut.wait_for(timeout) == std::future_status::ready) {
            auto s = fut.get(); 
            pending_chunk.unsafe_erase(number); 
            cout << "Find chunks" << endl;
            return s;
        } 
        else { 
            pending_chunk.unsafe_erase(number); 
            cout << "Time out" << endl;
            return {}; 
        }  
    }

    string node(const h256& childHash, const h256& parentHash, int childIdx){
        if (!m_db) return {};
        
        string str = m_db->lookup(childHash);
        
        // test the case of lost node
        bool lost_test = false;
        if(parentHash != h256{}) // 根节点没有父亲节点
            lost_test = true;

        if(!str.empty() && !lost_test){
            // return str;
        }
        else{
            // entre remote read phase 
            // cout << childHash << " is lost." << endl;
            string metas;
            ec_db->Get(rocksdb::ReadOptions(),
                rocksdb::Slice(reinterpret_cast<const char*>(parentHash.data()), h256::size),
                &metas);
            auto s =  readChild(metas, childIdx);
            auto MetaAndVer = ChunkBuilder::deserializeMetadata(s); // get lost target meta, prepare to remote fetching or recovering
            auto nodeid = (uint32_t)MetaAndVer.first.m_node;
            auto ver = mpt_ptr->block_height - MetaAndVer.second;
            if(nodeid == *nodeArry){
                return str; // 本地读取
            }
            // 尝试从远端拿数据
            bool test_chunk_recover = true; // test 即使有字符串还是会进入恢复
            string remote_str = sendingStateRequest(childHash, &nodeid);
            if(remote_str.empty() || test_chunk_recover){
                cout << "Remote read failed, start state recover." << endl;
                stateRecover(MetaAndVer.first, ver);
            }
            else{
                cout << "Success read remote State." << endl;
                str = remote_str; // 赋值给最后结果
            }
        }
        return str;
    }

    string at(h256 _k, h256 root) {    
        // auto n = NibbleSlice(b);
        auto rlt = atAux(RLP(node(root, h256{}, -1)), bytesConstRef((byte const*)&_k, sizeof(_k)), root);
        // cout << "at rlt = " << RLP(rlt) << std::endl;
        // 执行时远程读归零
        // execution_remote_read = 0;
        return rlt;
    }

    string atAux(RLP _here, NibbleSlice _key, h256 selfHash){
        std::cout << "---Entry func atAux---Finding key word: " << _key <<std::endl;
        // std::cout << "isEmpty?" << _here.isEmpty() 
        //     << "isNull?" << _here.isNull() <<std::endl;
        
        if (_here.isEmpty() || _here.isNull())
            // not found.
            return std::string();
        unsigned itemCount = _here.itemCount();
        assert(_here.isList() && (itemCount == 2 || itemCount == 17));

        if (itemCount == 2)
        {
            // std::cout << "2=: " << _key <<std::endl;
            auto k = keyOf(_here);
            // std::cout << "  k   = " << k << endl;
            // std::cout << " _key = " << _key << endl;
            if (_key == k && isLeaf(_here)){
                // reached leaf and it's us
                // cout << "leaf here :" << _here << endl;
                return _here[1].toString();
            }
            else if (_key.contains(k) && !isLeaf(_here))
                // not yet at leaf and it might yet be us. onwards...
                return atAux(_here[1].isList() ? _here[1] : RLP(node(_here[1].toHash<h256>(), selfHash, 0)),
                    _key.mid(k.size()),
                    _here[1].toHash<h256>());
            else{
                // not us.
                // cout << " not us ? yes" <<endl;
                return std::string();
            }
        }
        else
        {
            // std::cout << "17=: " << _key <<std::endl;
            if (_key.size() == 0)
                return _here[16].toString();
            auto n = _here[_key[0]];
            if (n.isEmpty())
                return std::string();
            else
                return atAux(n.isList() ? n : RLP(node(n.toHash<h256>(), selfHash, _key[0])), // _key-[0] is idx of tire
                    _key.mid(1),
                    n.toHash<h256>());
        }
    }
    
    string localreadChunk(uint16_t ver, uint8_t replicaID, bool Spliting = 0){
        
        // read data chunk 
        string chunk = rebuildChunk(ver, static_cast<size_t>(replicaID));
        // read parity chunk 
        if(chunk.empty()){
            chunk = scan_epoch_chunk(ec_db, (uint32_t)ver, replicaID);
            if(chunk.size() >= 32)
                chunk = chunk.substr(32); // 切除key的32bytes
        }
        // cout << "[localreadChunk] return chunk size = " << chunk.size() << endl;
        return chunk;
    }

    // 正版的recover
    void stateRecover(NodeMetadata& meta, uint16_t ver, bool Spliting = 0){
        
        // 记录时间和状态大小
        auto t1 = std::chrono::steady_clock::now();

        // 1. 解析 Nodemeta 
        auto& d = meta;
        auto _offset = d.m_offset; // 对应数据 偏移量
        auto len = d.getDataLength() + d.getMetaSize(); // 对应数据 总长度
        auto nodeId = d.getNodeNum();
        size_t lost_node_idx;
    
        // 2. 根据编码组信息获取chunks
        vector<vector<uint8_t>> recover_vec = scan_encoding_groups(*ec_db, ver, nodeId);

        unordered_map<uint, string> chunk_pool;
        for(auto& group: recover_vec){ // 取出一组编码组
            
            print_group(group);

            // 2.1 删除分隔符'|'，并且计算k和m
            auto it = find(group.begin(), group.end(), 0xFF);
            const size_t ec_k = it - group.begin();
            const size_t ec_m = group.end() - (it + 1);
            group.erase(it);
            if (era_) era_->initECgroup((uint32_t)ver, (uint32_t)ec_k, group);
            // print_group(group);
            // cout <<"\x1b[34m[stateRecover]\x1b[0m k= " << ec_k
            //     <<" m=" << ec_m << endl;

            deque<uint8_t> dq(group.begin(), group.end());
            deque<uint8_t> wait_queue; 
            size_t ready_chunk = 0;
            vector<string> raw_data(ec_k + ec_m); // 准备进行编码的chunk数组
            auto lost_pos = find(group.begin(), group.end(), (uint8_t)nodeId);
            lost_node_idx = lost_pos - group.begin();
            cout << "Lost chunk "<< nodeId <<" idx in this group:" << lost_node_idx << endl;
            dq.erase(dq.begin() + static_cast<std::ptrdiff_t>(lost_node_idx)); // 丢失的chunk不应该存在该请求队列中

            // 2.3 在本地尝试读取
            while(!dq.empty()){
                uint8_t replicaID = dq.front();
                dq.pop_front();
                string chunk;
                if(nodeArry && *nodeArry == (uint32_t)replicaID % Nodes.size()) // 检测是否属于本地
                    chunk = localreadChunk(ver, replicaID);

                if(chunk.empty()){
                    wait_queue.push_back(replicaID); // 读取失败，则远程读
                    continue;
                }
                else{
                    ready_chunk++;
                    auto pos = find(group.begin(), group.end(), replicaID);
                    raw_data[(size_t)(pos - group.begin())] = chunk; // 加入编码组
                    chunk_pool[(uint)replicaID] = chunk;             // 加入缓存池，给下次循环使用
                    
                    cout << "\x1b[34m[stateRecover]\x1b[0m Read chunk " 
                    << (uint)replicaID << " successful!" 
                    << "insert idx " << (size_t)(pos - group.begin()) << endl;
                }
            }
            // 远端读取 
            if(!wait_queue.empty()){
                /* 分发消息，处理远程传输的数据 …… */ 
                vector<uint8_t> v(wait_queue.begin(), wait_queue.end());
                if (era_) era_->initECgroup((uint32_t)ver, (uint32_t)ec_k - ready_chunk, v); // 
                auto IDValuePairs = sendingChunkRequest((uint32_t)ver, wait_queue, &Mediator::nodeChunkMapping); // 收到vector<chunkid, chunkvalue >
                // 将收到的信息插入 raw data
                for(auto& p: IDValuePairs){
                    uint32_t replicaID = p.first; 
                    string chunk = p.second;
                    ready_chunk++;
                    auto pos = find(group.begin(), group.end(), replicaID);
                    raw_data[(size_t)(pos - group.begin())] = chunk; // 加入编码组
                    chunk_pool[(uint)replicaID] = chunk;             // 加入缓存池，给下次循环使用
                    cout << "\x1b[34m[stateRecover]\x1b[0m remote Read chunk " 
                    << (uint)replicaID << " successful!" 
                    << "insert idx " << (size_t)(pos - group.begin()) << endl;
                }
            }

            // 2.4 检测是否收到了足够的chunk
            // while(ready_chunk < ec_k){
                /* 等待远程传输的数据 …… */
            // }
            if(ready_chunk < ec_k){
                continue;
            }

            // 3. 收集足够的chunk，开始恢复操作
            // raw_data[lost_node_idx].clear(); // ！仅测试 把丢失的数据块清空
            string lost_chunk = mpt_ptr->state_erasure->decodeFromMPT(raw_data, ec_m, lost_node_idx);
            // if(lost_chunk.empty()){
            //     cout << " lost chunk is empty" << endl;
            // }
            _MerkleTree mTree(dev::splitStr(lost_chunk, 100));
            h256 hash = mTree.root->hash;
            cout << "\x1b[34m[stateRecover]\x1b[0m recover chunk " << (uint)nodeId << " hash:" << hash << endl; 

        }
        
    }
    
    void runSyntheticLoadFromIni(const std::string& ini_path) {
        SimpleIni ini;
        ini.load(ini_path);
        auto& mptState = *mpt_ptr; // 获取mptstate的实例
        // ---- 读取参数（带默认值）----
        // [ec]
        const int nodes_number    = ini.getInt("ec", "nodes", 4);
        const int fault_tolerance = ini.getInt("ec", "f", 2);
        const int encoding_level  = ini.getInt("ec", "level", 2);

        // [workload]
        const int block_num       = ini.getInt("workload", "blocks", 1);
        const int account_num     = ini.getInt("workload", "accounts_per_block", 20);
        const double skew         = ini.getDouble("workload", "skew", 0.0);
        const int balance_start   = ini.getInt("workload", "balance_start", 1);
        const int account_size    = ini.getInt("workload", "account_space", 1'000'000);

        // [readback]
        const int do_read_back   = ini.getInt("readback", "enable", 1);
        const int read_back_limit = ini.getInt("readback", "limit", 1);

        // [nodes] 读取 node0, node1, ... 连续到缺失为止（原样保留为 string）
        std::vector<std::string> node_ids;
        for (int i = 0; /*break inside*/ ; ++i) {
            std::string key = "node" + std::to_string(i);
            std::string v = ini.get("nodes", key, "");
            if (v.empty()) break;
            node_ids.push_back(v);
        }
        Nodes = node_ids;

        // 计算 node 的逻辑 id
        uint cnt = 0;
        for(auto& node: Nodes){
            if(node == id) {
                nodeArry = &cnt;
                break;
            }
            cnt++; 
        }
        if(nodeArry) {
            cout << "\x1b[36m[runSyntheticLoadFromIni]\x1b[0m Node arry: " 
                 << *nodeArry << endl;
            if(era_) era_->id = nodeArry;
        }

        // ---- 打印一行配置摘要（可选）----
        std::cout << "\x1b[36m[runSyntheticLoadFromIni]\x1b[0m "
                  << "nodes=" << nodes_number
                  << " f=" << fault_tolerance
                  << " L=" << encoding_level
                  << " blocks=" << block_num
                  << " accounts/block=" << account_num
                  << " skew=" << skew
                  << " balance_start=" << balance_start
                  << " account_space=" << account_size
                  << " read_back=" << do_read_back
                  << " limit=" << read_back_limit
                  << " mapped_node_ids=" << node_ids.size()
                  << std::endl;

        // ---- 生成账户访问序列 ----
        std::vector<u160> processed_data;
        std::vector<std::vector<u160>> block_account_list;
        block_account_list.push_back(std::vector<u160>()); // 1-based

        std::vector<u160> last_account_list;

        for (int i = 1; i <= block_num; ++i) {
            std::vector<u160> account_list;
            if (!last_account_list.empty()) {
                account_list = last_account_list;
            } else {
                for (int j = 0; j < account_num; ++j) {
                    u160 addr;
                    if (skew != 0.0) addr = zipf_rand(account_size, skew);
                    else             addr = u160(rand() % account_size); // 如需复现实验可换成固定种子 RNG
                    account_list.push_back(addr);
                    processed_data.push_back(addr);
                }
            }
            block_account_list.push_back(std::move(account_list));
        }

        // ---- 执行交易 → commit → EC 编码 → DB 落盘 ----
        int cur_balance = balance_start;

        for (int i = 1; i <= block_num; ++i) {
            const auto& account_list = block_account_list[i];

            for (const auto& a : account_list) {
                mptState.addBalance(a, u256(cur_balance++));
            }

            mptState.commit();

            std::vector<int> cfg = {nodes_number, fault_tolerance, encoding_level};
            auto totalEncodedData = mptState.makeECFromMPT(i, cfg);

            mptState.getState().db().commit();

            std::cout << "\x1b[32m[Block " << i << "]\x1b[0m ROOT HASH: "
                      << mptState.rootHash(true) << std::endl;
        }

        // ---- 可选：读取验证 ----
        if (do_read_back && !processed_data.empty() && read_back_limit > 0) {
            int cnt = 0;
            for (auto& id : processed_data) {
                at(sha3(Address(id)), mptState.rootHash()); // 与你原逻辑一致
                ++cnt;
                if (cnt % 1000 == 0) {
                    std::cout << "\x1b[34m[Reading]\x1b[0m ..." << cnt << std::endl;
                }
                if (cnt >= read_back_limit) break;
                std::cout << " \x1b[33m[Next account]\x1b[0m" << std::endl;
            }
        }
        std::cout << "\x1b[36m[runSyntheticLoadFromIni]\x1b[0m done." << std::endl;
    }
};