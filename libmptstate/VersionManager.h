#pragma once

#include <libdevcore/TrieDB.h>
#include <libdevcore/OverlayDB.h>
#include <iostream>
#include <iomanip>
#include "Vtools.h"
// #include <tbb/tbb.h>
#include <queue>

using namespace std;
using namespace dev;

// 一些计算内存大小的函数
// inline void printMemorySize(size_t bytes) {
//     const double KB = 1024.0;
//     const double MB = KB * 1024;
//     const double GB = MB * 1024;

//     cout << fixed << setprecision(2);

//     if(bytes < KB){
//         cout << bytes << "Bytes";
//     }
//     else if(bytes < MB){
//         cout << bytes / KB << "KB";
//     }
//     else if(bytes < GB){
//         cout << bytes / MB << "MB";
//     }
//     else{
//         cout << bytes / GB << "GB";
//     }
// }
static constexpr size_t kChildLen = 11; // Metadata length + ver length
struct NodeMetadata {
    uint32_t m_offset; // value 在 chunk 中的偏移量
    uint32_t m_lengh; // 前三个字节表示 value 长度，后一个字节表示 metadata长度
    // vector<uint16_t> m_versionDiffs; // 版本存储差集合
    uint8_t m_node; // 所属节点编号

    NodeMetadata(): m_offset(0), m_lengh(0), m_node(0) {}
    
    uint8_t Size(){
        int metadataSize = sizeof(m_offset) + sizeof(m_lengh) + sizeof(m_node);
        // cout << "Size : " << metadataSize << endl;
        return uint16_t(metadataSize);
    }

    uint32_t getDataLength() const{
        return m_lengh & 0xFFFFFF; // 低 24 位
    }

    uint16_t getMetaSize() const{
        return (m_lengh >> 24) & 0xFF; // 高 8 位
    }

    uint16_t getNodeNum() const{
        return (uint16_t)m_node; // 高 8 位
    }

    void printNodeMetadata(int ver = -1) {
        if(ver < 0){
            cout << "Offset[" << m_offset << "]" 
                << " DataLengh[" << getDataLength() << "] " 
                << " MetaSize[" << getMetaSize() << "]" 
                << " NodeNum[" << getNodeNum() << "]" << endl;
        }
        else{
            cout << "Offset[" << m_offset << "]" 
                << " DataLengh[" << getDataLength() << "] " 
                << " MetaSize[" << getMetaSize() << "]" 
                << " VersionDiff[" << ver << "]" 
                << " NodeNum[" << getNodeNum() << "]" << endl;
        }
        
    }
};

class StatePartition{
public:
    // 初始化
    vector<unordered_set<h256>> parts; // 每组节点集合
    unordered_map<h256, uint16_t> Mapparts; // 每组节点集合( unorder_map 形态)
    vector<uint> partSize;           // 每组当前大小
    unordered_map<h256, size_t> nodeSize;    // 每个元素的大小
    unordered_map<h256, vector<h256>> tree; // 模拟树结构：节点哈希 -> 子节点列表
    uint totalNodes = 0;       // 节点总数
    uint targetSize = 0;      // 每组目标大小（近似值）
    uint targetMod = 0;       // 每组数量的余数
    uint currentPart = 0;                  // 当前正在填充的组编号
    uint allocatedNodes = 0;               // 已分配节点计数
    uint m_groups = 0; // 网络节点个数

    StatePartition() = default;
    
    void init(uint nodeNumber){
        m_groups = nodeNumber; // The number of nodes 
        totalNodes = tree.size();
        targetSize = targetSize / m_groups;
        // targetMod = totalNodes % m_groups;
        parts.resize(m_groups);
        partSize.resize(m_groups, 0); 
        cout << "TotalNodes: " << totalNodes
             << " TargetSize: " << targetSize << endl;
    }
    
    void addTreeNode(h256 nodeHash, h256 childHash){
        tree[nodeHash].push_back(childHash);
    }

    vector<unordered_set<h256>> getPartitionResult(){
        return parts;
    }

    unordered_map<h256, uint16_t> getPartitionMapResult(){
        return Mapparts;
    }

    void processBatch(const std::unordered_map<h256, std::string>& batch) {

        vector<h256> empty_children;
        // 2. 解析每个字符串 RLP，处理指向的节点
        for (const auto& pair : batch) {
            auto hash = pair.first;
            auto rlpStr = pair.second;
            // 记录 node 大小
            nodeSize[hash] = rlpStr.size();
            targetSize += rlpStr.size();
            try {
                // 将字符串 RLP 转换为 RLP 对象
                RLP rlp(rlpStr);

                if(rlp.itemCount() == 2){
                    // 如果是叶子节点 则因其没有子节点直接跳过
                    if(isLeaf(rlp)){
                        tree[hash] = empty_children;
                        continue;
                    }
                    auto childHash = rlp[1].toHash<h256>();
                    addTreeNode(hash, childHash);
                }
                else{
                    // 遍历 RLP 的所有子节点哈希
                    for (size_t i = 0; i < rlp.itemCount(); ++i) {
                        if (rlp[i].isEmpty()) continue; // 跳过空节点

                        h256 childHash = rlp[i].toHash<h256>(); // 获取子节点哈希
                        addTreeNode(hash, childHash);
                        
                    }
                }
            }catch (const std::exception& ex) {
                // 捕获 RLP 解析错误
                std::cerr << "Error processing RLP for hash " << hash << ": " << ex.what() << std::endl;
            }
        }
    }

    void processElement(const h256& hash, const std::string& rlpStr) {

        // 记录 node 大小
        nodeSize[hash] = rlpStr.size();
        targetSize += rlpStr.size();
        try {
            // 将字符串 RLP 转换为 RLP 对象
            RLP rlp(rlpStr);

            if(rlp.itemCount() == 2){
                // 如果是叶子节点 则因其没有子节点直接跳过
                if(isLeaf(rlp)){
                    tree[hash] = {};
                    return;
                    // continue;
                }
                auto childHash = rlp[1].toHash<h256>();
                addTreeNode(hash, childHash);
            }
            else{
                // 遍历 RLP 的所有子节点哈希
                for (size_t i = 0; i < rlp.itemCount(); ++i) {
                    if (rlp[i].isEmpty()) continue; // 跳过空节点

                    h256 childHash = rlp[i].toHash<h256>(); // 获取子节点哈希
                    addTreeNode(hash, childHash);
                    
                }
            }
        }catch (const std::exception& ex) {
            // 捕获 RLP 解析错误
            std::cerr << "Error processing RLP for hash " << hash << ": " << ex.what() << std::endl;
        }
    }
    
    int maxdepth = 0;
    void dfs(h256 nodeHash, int depth = 0) {
        
        // 如果当前节点不存在本次更新的节点中，则跳过
        if(tree.find(nodeHash) == tree.end()){
            return ;
        }   
        
        maxdepth = max(maxdepth, depth);
        // 如果当前组已满，切换到下一组
        if (partSize[currentPart] >= targetSize && currentPart < m_groups - 1) {
            currentPart++;
            // if(targetMod > 0){
            //     targetMod--;
            // }
        }

        // 将当前节点加入当前组
        // cout << "!!Node hash -> " << nodeHash << endl;
        parts[currentPart].insert(nodeHash);
        Mapparts[nodeHash] = currentPart;
        partSize[currentPart] += nodeSize[nodeHash];
        allocatedNodes += nodeSize[nodeHash];

        // 遍历子节点
        for (h256 childHash : tree[nodeHash]) {
            if (parts[currentPart].count(childHash) == 0) { // 子节点未被分配
                
                dfs(childHash, depth + 1);
            }
        }
    }
    
    void partitionMPT(h256 rootHash) {

        // 混合 BFS 和 DFS 划分
        bool useDFS = false; // 是否切换到深度优先
        int dep = 0;
        // BFS 阶段：按层划分
        queue<h256> bfsQueue;
        bfsQueue.push(rootHash);
        while (!bfsQueue.empty() && allocatedNodes < targetSize / 2) {
            int levelSize = bfsQueue.size(); // 当前层的节点数
            for (int i = 0; i < levelSize; ++i) {
                h256 nodeHash = bfsQueue.front();
                bfsQueue.pop();
                // cout << "Processing hash -> " << nodeHash << endl;

                // 分配当前节点到当前组
                parts[currentPart].insert(nodeHash);
                Mapparts[nodeHash] = currentPart;
                partSize[currentPart] += nodeSize[nodeHash];
                allocatedNodes += nodeSize[nodeHash];

                // 如果当前组已满，切换到下一组
                if (partSize[currentPart] >= targetSize && currentPart < m_groups - 1) {
                    currentPart++;
                    cout << "Current dep = "<< dep << endl;
                }

                // 将子节点加入下一层
                for (h256 childHash : tree[nodeHash]) {
                    // cout << "---Push children hash -> " << childHash << endl;
                    // 如果遍历到非本次更新的节点，则跳过
                    if(tree.find(childHash) != tree.end()){
                        bfsQueue.push(childHash);
                    }   
                }
            }
            dep++;
        }
        cout << "Switch to DFS dep = "<< dep << endl;
        // 切换到 DFS 阶段
        useDFS = true;
        while(!bfsQueue.empty() && useDFS){
            h256 nodeHash = bfsQueue.front();
            bfsQueue.pop();
            dfs(nodeHash, dep);
            // cout << "Processing hash -> " << nodeHash << endl;
        }
        cout << "Switch to DFS maxdep = "<< maxdepth << endl;
        // // 输出划分结果
        // for (int i = 0; i < m_groups; ++i) {
        //     cout << "Part " << i << ": ";
        //     for (const h256& hash : parts[i]) {
        //         cout << hash << "[" << nodeSize[hash] << "]" << " ";
        //     }
        //     cout << endl;
        // }
    }

    void partitionMPTWithBaseline(h256 rootHash) {

        // 混合 BFS 和 DFS 划分
        // bool useDFS = false; // 是否切换到深度优先

        // BFS 阶段：按层划分
        queue<h256> bfsQueue;
        bfsQueue.push(rootHash);
        while (!bfsQueue.empty()) {
            int levelSize = bfsQueue.size(); // 当前层的节点数
            for (int i = 0; i < levelSize; ++i) {
                h256 nodeHash = bfsQueue.front();
                bfsQueue.pop();
                // cout << "Processing hash -> " << nodeHash << endl;

                // 分配当前节点到当前组
                currentPart = rand() % m_groups;
                parts[currentPart].insert(nodeHash);
                Mapparts[nodeHash] = currentPart;
                partSize[currentPart] += nodeSize[nodeHash];
                allocatedNodes += nodeSize[nodeHash];

                // // 如果当前组已满，切换到下一组
                // if (partSize[currentPart] >= targetSize && currentPart < m_groups - 1) {
                //     currentPart++;
                //     // if(targetMod > 0){
                //     //     targetMod--;
                //     // }
                // }

                // 将子节点加入下一层
                for (h256 childHash : tree[nodeHash]) {
                    // cout << "---Push children hash -> " << childHash << endl;
                    // 如果遍历到非本次更新的节点，则跳过
                    if(tree.find(childHash) != tree.end()){
                        bfsQueue.push(childHash);
                    }   
                }
            }
        }
        // cout << "Switch to DFS " << endl;
        // // 切换到 DFS 阶段
        // useDFS = true;
        // while(!bfsQueue.empty()){
        //     h256 nodeHash = bfsQueue.front();
        //     bfsQueue.pop();
        //     dfs(nodeHash);
        //     // cout << "Processing hash -> " << nodeHash << endl;
        // }

        // // 输出划分结果
        // for (int i = 0; i < m_groups; ++i) {
        //     cout << "Part " << i << ": ";
        //     for (const h256& hash : parts[i]) {
        //         cout << hash << "[" << nodeSize[hash] << "]" << " ";
        //     }
        //     cout << endl;
        // }
    }

    void partitionMPTWithDHT(h256 rootHash) {
        cout<< "Start partitionMPTWithDHT" << endl;
        queue<h256> bfsQueue;
        bfsQueue.push(rootHash);
        while (!bfsQueue.empty()) {
            int levelSize = bfsQueue.size(); // 当前层的节点数
            for (int i = 0; i < levelSize; ++i) {
                h256 nodeHash = bfsQueue.front();
                bfsQueue.pop();
                // cout << "Processing hash -> " << nodeHash << endl;

                // caculate the assign node 
                size_t sum = 0;
                for(uint8_t byte : nodeHash) sum = (sum*131+byte) % 1000000007;

                // 分配当前节点到当前组
                // currentPart = rand() % m_groups;
                currentPart = sum % m_groups;
                parts[currentPart].insert(nodeHash);
                Mapparts[nodeHash] = currentPart;
                partSize[currentPart] += nodeSize[nodeHash];
                allocatedNodes += nodeSize[nodeHash];

                // 将子节点加入下一层
                for (h256 childHash : tree[nodeHash]) {
                    // cout << "---Push children hash -> " << childHash << endl;
                    // 如果遍历到非本次更新的节点，则跳过
                    if(tree.find(childHash) != tree.end()){
                        bfsQueue.push(childHash);
                    }   
                }
            }
        }
    }
    
    void partitionMPTWithLocal(h256 rootHash) {
        cout<< "Start partitionMPTWithDHT" << endl;
        queue<h256> bfsQueue;
        bfsQueue.push(rootHash);
        while (!bfsQueue.empty()) {
            int levelSize = bfsQueue.size(); // 当前层的节点数
            for (int i = 0; i < levelSize; ++i) {
                h256 nodeHash = bfsQueue.front();
                bfsQueue.pop();
                // cout << "Processing hash -> " << nodeHash << endl;

                // caculate the assign node 
                size_t sum = 0;
                // for(uint8_t byte : nodeHash) sum = (sum*131+byte) % 1000000007;

                // 分配当前节点到当前组
                // currentPart = rand() % m_groups;
                currentPart = sum % m_groups;
                parts[currentPart].insert(nodeHash);
                Mapparts[nodeHash] = currentPart;
                partSize[currentPart] += nodeSize[nodeHash];
                allocatedNodes += nodeSize[nodeHash];

                // 将子节点加入下一层
                for (h256 childHash : tree[nodeHash]) {
                    // cout << "---Push children hash -> " << childHash << endl;
                    // 如果遍历到非本次更新的节点，则跳过
                    if(tree.find(childHash) != tree.end()){
                        bfsQueue.push(childHash);
                    }   
                }
            }
        }
    }
};

class VersionManager {
        public:
            void recordCache(h256 const& nodeHash, int version){
                m_cache[nodeHash] = version;
            }
            
            void recordVersion(h256 const& nodeHash, int version){
                m_nodeVersions[nodeHash] = version;
            }
            int getVersion(h256 const& nodeHash) const {
                auto it = m_nodeVersions.find(nodeHash);
                return (it != m_nodeVersions.end() ? it->second : -1);
            }
            int getVersionDifference(h256 parentHash, h256 const& childHash) const{
                int parentVersion = getVersion(parentHash);
                int childVersion =  getVersion(childHash);
                return (parentVersion != -1 && childVersion != -1) ? parentVersion - childVersion : -1;
            }

            void updataVersion(){
                m_nodeVersions.insert(m_cache.begin(), m_cache.end());
            }

            void setVersion(int v){
                m_currentVersion = v;
            }

            void processBatch(const std::unordered_map<h256, std::string>& batch) {
                // 1. 将 <h256, 0> 插入 m_cache
                // for (const auto& pair : batch) {
                //     auto hash = pair.first;
                //     // auto rlpStr = pair.second;
                //     m_cache[hash] = 0; // 插入到 m_cache，版本差为 0
                // }

                // 1. 将 <h256, 0> 插入 m_cache
                // 2. 解析每个字符串 RLP，处理指向的节点
                for (const auto& pair : batch) {
                    auto hash = pair.first;
                
                    m_cache[hash] = 0; // 插入到 m_cache，版本差为 0
                    auto rlpStr = pair.second;
                    try {
                        // 将字符串 RLP 转换为 RLP 对象
                        RLP rlp(rlpStr);

                        if(rlp.itemCount() == 2){
                            // 如果是叶子节点 则因其没有子节点直接跳过
                            if(isLeaf(rlp)){
                                continue;
                            }
                            auto childHash = rlp[1].toHash<h256>();
                            if(m_nodeVersions.find(childHash) != m_nodeVersions.end()){
                                ++m_nodeVersions[childHash];
                            }
                        }
                        else{
                            // 遍历 RLP 的所有子节点哈希
                            for (size_t i = 0; i < rlp.itemCount(); ++i) {
                                if (rlp[i].isEmpty()) continue; // 跳过空节点

                                h256 childHash = rlp[i].toHash<h256>(); // 获取子节点哈希

                                // 如果子节点哈希存在于 m_nodeVersions 中，增加版本差值
                                if (m_nodeVersions.find(childHash) != m_nodeVersions.end()) {
                                    ++m_nodeVersions[childHash];
                                }
                            }
                        }
                    }catch (const std::exception& ex) {
                        // 捕获 RLP 解析错误
                        std::cerr << "Error processing RLP for hash " << hash << ": " << ex.what() << std::endl;
                    }
                }
                // 将 cache 的数据合并
                updataVersion();
            }

            void processElement(const h256& hash, const std::string& rlpStr) {

                try {
                    // 将字符串 RLP 转换为 RLP 对象
                    RLP rlp(rlpStr);

                    if(rlp.itemCount() == 2){
                        // 如果是叶子节点 则因其没有子节点直接跳过
                        if(isLeaf(rlp)){
                            return;
                            // continue;
                        }
                        auto childHash = rlp[1].toHash<h256>();
                        if(m_nodeVersions.find(childHash) != m_nodeVersions.end()){
                            ++m_nodeVersions[childHash];
                        }
                    }
                    else{
                        // 遍历 RLP 的所有子节点哈希
                        for (size_t i = 0; i < rlp.itemCount(); ++i) {
                            if (rlp[i].isEmpty()) continue; // 跳过空节点

                            h256 childHash = rlp[i].toHash<h256>(); // 获取子节点哈希

                            // 如果子节点哈希存在于 m_nodeVersions 中，增加版本差值
                            if (m_nodeVersions.find(childHash) != m_nodeVersions.end()) {
                                ++m_nodeVersions[childHash];
                            }
                        }
                    }
                }catch (const std::exception& ex) {
                    // 捕获 RLP 解析错误
                    std::cerr << "Error processing RLP for hash " << hash << ": " << ex.what() << std::endl;
                }
            
                // 将 cache 的数据合并
                // updataVersion();
            }

            void printManager(){
                for(const auto& pair : m_nodeVersions){
                    cout << "Node Hash [" << pair.first << "] VersionDiff [" << pair.second << endl;
                }
            }

            void setStatePartition(StatePartition sp){
                this->sp = sp;
            }
            // 定位状态信息
            string node(h256 hash){
                auto it = dataSet.find(hash);
                string str;
                // Is history read?
                // if(least_state != nullptr){
                //     auto it = std::find(least_state->begin(), least_state->end(), hash);
                //     if(it == least_state->end()){
                //         // can not find in least mpt
                //         // cout << "Can not find: " << hash << endl;
                //         execution_remote_read++;
                //     }
                // }
                // find in cache
                if(it != dataSet.end()){
                    str = (it -> second).first;
                    auto meta = (it -> second).second;
                    auto num = meta.getNodeNum();
                    // remote read times
                    if(current_read != num){
                        if(current_read != -1){
                            // std::this_thread::sleep_for(std::chrono::microseconds(10000 * 2));
                            // cout << "Cross node reading ...... sleep 0.06ms "; microseconds(60 * 2)
                        }
                        current_read = num;
                        read_count++;
                    }
                }
                else{ // find in disk
                    // cout <<"Entry DB Lookup" << endl;
                    str = m_db->lookup(hash);
                    // cout << str << endl;
                    auto meta = (it -> second).second;
                    auto num = meta.getNodeNum();
                    // remote read times
                    if(current_read != num){
                        if(current_read != -1){
                            // std::this_thread::sleep_for(std::chrono::microseconds(10000 * 2));
                            // cout << "Cross node reading ...... sleep 0.06ms "; microseconds(60 * 2)
                        }
                        current_read = num;
                        read_count++;
                    }
                }
                return str;
            }

            void at(h256 _k, h256 root) {    
                // auto n = NibbleSlice(b);
                auto rlt = atAux(RLP(node(root)), bytesConstRef((byte const*)&_k, sizeof(_k)));
                // cout << "at rlt = " << RLP(rlt) << std::endl;
                // 执行时远程读归零
                execution_remote_read = 0;
            }

            string atAux(RLP _here, NibbleSlice _key){
                // std::cout << "---Entry func atAux---Finding key word: " << _key <<std::endl;
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
                        return atAux(_here[1].isList() ? _here[1] : RLP(node(_here[1].toHash<h256>())),
                            _key.mid(k.size()));
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
                        return atAux(n.isList() ? n : RLP(node(n.toHash<h256>())), _key.mid(1));
                }
            }

            // h256 recoverData(h256 target, NodeMetadata meta, BMT bmt){
            //     auto offset = meta.m_offset;
            //     auto len = meta.getDataLength() + meta.getMetaSize();
            //     auto nodeNum = meta.getNodeNum();

            //     // 
            //     h256 rlt;
            //     int current = 0;
            //     auto ptr = bmt.searchWithOrder(nodeNum. current);
            //     if(ptr){
            //         return ptr->_hash;
            //     }

            //     return rlt;
            // }

            void printDataSet(){
                for(auto t : dataSet){
                    cout<< "hash:" << t.first
                        << "value:" << RLP(t.second.first) << endl;
                }
            }

            StatePartition getStatePartition(){ return sp; }

            VersionManager() = default;
        
            unordered_map<h256, int> m_nodeVersions;
            unordered_map<h256, int> m_cache;
            int m_currentVersion;
            StatePartition sp;
            unordered_map<h256, pair<std::string, NodeMetadata>> dataSet; // hash 值其对应的 value 和 metadata
            unordered_map<h256, bool> dataSet_init;
            size_t execution_remote_read = 0;

            vector<h256>* least_state = nullptr;
            void setLeastState(vector<h256>* lt){
                least_state = lt;
            }
            // void initDataMap(vector<h256>& data_map){
            //     m_data_map = data_map;
            // }
            // void checkDataMap(h256& target){
            //     auto it = m_data_map.find(target);
            //     if(it == m_data_map.end()){
            //         // can not found, no exist in this MPT
            //         cout << "can not found, no exist in this MPT" << endl;
            //     }
            // }

            unordered_map<h256, string> dataWithchildsNodeMetadata;

            int current_read = -1; // 记录现在正在遍历MPT节点属于的节点
            int read_count = 0; // 记录遍历过程访问了多少个节点
            size_t not_in_cache = 0; // 记录访问了多少个 不再当前状态树 的节点
            OverlayDB *m_db = nullptr; // state DB for versionmanager
            void initDB(OverlayDB& db){
                m_db = &db;
            }

};

class ChunkBuilder {
private:
    vector<string> m_data;
    unordered_map<h256, pair<std::string, NodeMetadata>>& m_dataSet; // <key, <value, NodeMetaData>>
    unordered_map<h256, bool>& m_dataSet_init;
    unordered_map<h256, int>& m_nodeVersions;
    unordered_map<h256, string>& m_dataWithchildsNodeMetadata;
    unordered_map<h256, h256> fatherMap;
    queue<pair<h256, uint16_t>> ready_node;
    unordered_map<h256, uint16_t> partitions;
    int block_number;
    // queue<h256> m_order; // the nodes order in a chunk 
    rocksdb::DB *_db = NULL;
    OverlayDB *overlay_db = NULL;
    int TotalMetaSize = 0;
public:

    void setDB(rocksdb::DB& db) { _db = &db; }
    void setStateDB(OverlayDB &db){ overlay_db = &db; }

    void writeDB(h256& hash, string str){
        if(_db != NULL){
            auto s = _db->Put(rocksdb::WriteOptions(), 
            rocksdb::Slice(reinterpret_cast<const char*>(hash.data()), dev::h256::size), 
            rocksdb::Slice(str));
        if(!s.ok()){
            cout << "Write failed" << endl;
        }
        else{
            // cout << "Write already "<< str <<endl;
        }
    }
}

    string serializeMetadata(const NodeMetadata& metadata) {
        ostringstream oss;

        // 序列化 offset
        oss.write(reinterpret_cast<const char*>(&metadata.m_offset), sizeof(metadata.m_offset));

        // 序列化 length
        oss.write(reinterpret_cast<const char*>(&metadata.m_lengh), sizeof(metadata.m_lengh));

        // 序列化版本差的数量
        // uint16_t numDiffs = metadata.m_versionDiffs.size();
        // oss.write(reinterpret_cast<const char*>(&numDiffs), sizeof(numDiffs));

        // 序列化版本差数组
        // for (uint16_t diff : metadata.m_versionDiffs) {
        //     oss.write(reinterpret_cast<const char*>(&diff), sizeof(diff));
        // }

        return oss.str(); // 返回序列化后的字符串
    }

    string serializeMetadataAndVersionDiff(const NodeMetadata& metadata, uint16_t ver) {
        ostringstream oss;

        // 序列化 offset
        oss.write(reinterpret_cast<const char*>(&metadata.m_offset), sizeof(metadata.m_offset));

        // 序列化 length
        oss.write(reinterpret_cast<const char*>(&metadata.m_lengh), sizeof(metadata.m_lengh));

        // 序列化 versiondiff
        oss.write(reinterpret_cast<const char*>(&ver), sizeof(ver));

        // 序列化 node
        oss.write(reinterpret_cast<const char*>(&metadata.m_node), sizeof(metadata.m_node));
        // 序列化版本差的数量
        // uint16_t numDiffs = metadata.m_versionDiffs.size();
        // oss.write(reinterpret_cast<const char*>(&numDiffs), sizeof(numDiffs));

        // 序列化版本差数组
        // for (uint16_t diff : metadata.m_versionDiffs) {
        //     oss.write(reinterpret_cast<const char*>(&diff), sizeof(diff));
        // }

        // 计算附加信息的大小
        auto s =  oss.str();
        TotalMetaSize = TotalMetaSize + s.size();
        
        return s; // 返回序列化后的字符串
    }

    static pair<NodeMetadata,uint16_t> deserializeMetadata(const string& data) {
        NodeMetadata metadata;
        uint16_t ver;
        istringstream iss(data);

        // 反序列化 offset
        iss.read(reinterpret_cast<char*>(&metadata.m_offset), sizeof(metadata.m_offset));

        // 反序列化 length
        iss.read(reinterpret_cast<char*>(&metadata.m_lengh), sizeof(metadata.m_lengh));

        // 反序列化 versiondiff
        iss.read(reinterpret_cast<char*>(&ver), sizeof(ver));
        
        // 反序列化 node
        iss.read(reinterpret_cast<char*>(&metadata.m_node), sizeof(metadata.m_node));

        // 反序列化版本差的数量
        // uint16_t numDiffs;
        // iss.read(reinterpret_cast<char*>(&numDiffs), sizeof(numDiffs));

        // 反序列化版本差数组
        // metadata.m_versionDiffs.resize(numDiffs);
        // for (uint16_t& diff : metadata.m_versionDiffs) {
        //     iss.read(reinterpret_cast<char*>(&diff), sizeof(diff));
        // }

        return {metadata, ver}; // 返回反序列化后的结构
    }
    
    void recordChunkOrder(queue<h256> q, size_t idx){
        // string str = pack_hashes_8B2B(q);
        // for(auto tmp=q; !tmp.empty(); tmp.pop()){
        //     cout << tmp.front() << endl;
        // }

        cout << "[recordChunkOrder]" << endl;
        string str = serialize_h256_queue_raw(q);
        string k = "O|";
        k.append(dev::toString(block_number));
        k.push_back('|');
        k.append(dev::toString(idx));
        if(_db != NULL){
            auto s = _db->Put(rocksdb::WriteOptions(), 
            rocksdb::Slice(k), 
            rocksdb::Slice(str));
            if(!s.ok()){
                cout << "Write failed" << endl;
            }
            else{
                cout << "Write chunk order successful!" << endl;
            }
        }
        // local read and unpacked test;
        string test_rlt;
        if(_db != NULL){
            _db->Get(rocksdb::ReadOptions(), k, &test_rlt);
            if(test_rlt.empty()){
                cout << "Read failed" << endl;
            }
            else{
                cout << "Read chunk order successful!" << endl;
            }
        }
        auto test_vec_h256 = deserialize_h256_list_raw(test_rlt);
        // auto prefixes = unpack_prefix_tags(test_rlt);
        // vector<pair<string, string>> vec;
        // for(size_t i; i<prefixes.size(); i++){
        //     auto& prefix = prefixes[i];
        //     vec = scan_by_u64prefix_check_u16tag_simple(overlay_db, prefix.prefix8, prefix.tag16);
        // }
        for(size_t i = 0; i < test_vec_h256.size(); ++i){
            if(test_vec_h256[i] == q.front()){
                // cout << "fetch:" << test_vec_h256[i] << endl;
                // cout << "origin:" <<q.front() << endl;
                q.pop();
            }
            else{
                cout << "Read a wrong state!" <<endl;
                return ;
            }
        }
        cout << "Good! every hash has been read successful!" << endl;
    }

    vector<string> handleDataSet(unordered_map<h256, uint16_t> partitions, int cnt){
        
        // 所有元素和其分组压入 partitions 
        // unordered_map<h256, uint16_t> partitions;
        // auto cnt = 0;
        // for(auto& set : partition_sets){
        //     for(auto& ele : set){
        //         partitions[ele] = cnt;
        //         cout << "INSert hash: " << ele << endl;
        //     }
        //     ++cnt;
        // }
        // 初始化每个分组对应的 chunk & order
        vector<string> parts(cnt);
        vector<queue<h256>> order(cnt);
        // 完成情况 
        // unordered_set<h256> done_set; 
        while(!partitions.empty()){
            auto check = partitions.size();
            for(auto it = partitions.begin(); it != partitions.end();){
                
                auto hash = it->first; 
                auto group = it->second; // 所属节点编号 
                auto value = m_dataSet[hash].first;
                auto& meta = m_dataSet[hash].second;
                
                
                // cout << "Handledataset hash:" << hash 
                //     // << " Offset" <<  meta.m_offset
                //     << " Value" <<  value.size()
                // //     << " MetaStr" <<  metaStr.size() <<endl;
                // // cout << "data size " << data.size() 
                    // << endl;

                RLP rlp(value);
                // 判断是否是叶子节点
                if(rlp.itemCount() == 2 && isLeaf(rlp)){
                    auto& data = parts[group];
                    auto& que = order[group]; 
                    // 更新该节点的 metadata
                    meta.m_offset = data.size();
                    meta.m_lengh = (value.size() & 0xFFFFFF) | (0 << 24);
                    meta.m_node = (uint8_t)group;
                    // cout << "suo shu jie dian :" << meta.getNodeNum() << endl;
                    // 因为是叶子节点，他没有子节点，无需在value后面添加
                    data = data + value;
                    que.push(hash);
                    // 成功插入一个 hash
                    // cout << "succees insert leaf:" << hash;
                    // meta.printNodeMetadata();

                    m_dataSet_init[hash] = true;
                    it = partitions.erase(it);
                    continue;
                }
                else{
                    // 检测是否有还未被初始化的 Metadata
                    bool can_build = true;
                    vector<pair<NodeMetadata, int>> childrenMeta;
                    vector<size_t> childidx;
                    if(rlp.itemCount() == 2){
                        auto childHash = rlp[1].toHash<h256>();
                        // cout << "We are detect(cnt=2) " << childHash << endl;
                        // 如果子节点哈希 不 存在于 doneset 这意味着其 Metadata并没有初始化好，直接中止操作
                        if (m_dataSet_init[childHash] != true) {
                            // cout << "Not in " << childHash << endl;
                            childrenMeta.clear();
                            childidx.clear();
                            can_build = false;
                            // break;
                        }
                        else{
                            // 将对应子节点的 Nodemeta 和 nodeVersion 塞进一个 tmp 中
                            auto tmp = make_pair(m_dataSet[childHash].second, m_nodeVersions[childHash]);
                            childrenMeta.push_back(tmp);
                            childidx.push_back(0); // extension node no need 
                        }
                    }
                    else{
                        for (size_t i = 0; i < rlp.itemCount(); ++i) {
                            if (rlp[i].isEmpty()) continue; // 跳过空节点

                            h256 childHash = rlp[i].toHash<h256>(); // 获取子节点哈希
                            // cout << "We are detect(cnt=n) " << childHash << endl;
                            // 如果子节点哈希 不 存在于 doneset 这意味着其 Metadata并没有初始化好，直接中止操作
                            if (m_dataSet_init[childHash] != true) {
                                // cout << "Not in   " << childHash << endl;
                                childrenMeta.clear();
                                childidx.clear();
                                can_build = false;
                                break;
                            }
                            else{
                                // 将对应子节点的 Nodemeta 和 nodeVersion 塞进一个 tmp 中
                                auto tmp = make_pair(m_dataSet[childHash].second, m_nodeVersions[childHash]);
                                childrenMeta.push_back(tmp);
                                childidx.push_back(i);
                            }
                        }
                    }
                    // 把数据插入 chunk 当中
                    if(can_build){
                        auto& data = parts[group];
                        auto& que = order[group]; 
                        que.push(hash);

                        meta.m_offset = data.size();

                        data.append(value);

                        // 所有的 MetaData 的合计大小
                        uint8_t meta_size = 0;
                        
                        string str;
                        for(auto& child : childrenMeta){
                            // 计算该节点所有孩子节点的 NodeMetaData 大小
                            auto nodemeta = child.first;
                            uint16_t ver(child.second);
                            meta_size += nodemeta.Size() + sizeof(ver);
                            // 计算该节点的 NodeMetaData 和 Versiondiff
                            auto serializedData = serializeMetadataAndVersionDiff(nodemeta, ver);
                            data = data + serializedData;
                            // 设置节点 hash 何其对应的 childrenNode 的 Metadata
                            str = str + serializedData;
                            // cout<< "l = " << str.length();
                            // nodemeta.printNodeMetadata(ver); 
                            // cout << ver << endl;
                            // cout << "deserializedMetaData" << endl;
                            // auto rlt = deserializeMetadata(serializedData);
                            // rlt.first.printNodeMetadata();
                            // cout << "Version[" << rlt.second << "]" << endl;

                        }
                        meta.m_lengh = (value.size() & 0xFFFFFF) | (meta_size << 24);
                        meta.m_node = (uint8_t)group;
                        
                        // 成功插入一个 hash
                        // cout << "succees insert No-leaf:" << hash;
                        m_dataWithchildsNodeMetadata[hash] = str;
                        string prefix = "{";
                        for(auto& idx: childidx) {
                            char c = (idx<10) ? char('0'+idx) : char('a'+(idx-10)) ;
                            prefix.push_back(c);
                        }
                        prefix.push_back('}');
                        writeDB(hash,prefix + str);
                        // meta.printNodeMetadata();
                        

                        // 插入以后 该数据的 metaData也是可以使用了
                        m_dataSet_init[hash] = true;
                        it = partitions.erase(it);
                        continue;
                    }
                }
                // cout << "data  fist size " << data.size() << endl;
                
                // cout << "Handledataset hash:" << hash 
                //     << " Offset" <<  meta.m_offset
                //     << " Value" <<  value.size()
                //     << " MetaStr" <<  metaStr.size() <<endl;
                // cout << "data size " << data.size() << endl;
                ++it;
            }
            assert(check != partitions.size());
            if(check == partitions.size()){
                cout << "数量没有变化，可能出错了！" << endl;
                break;
            }
        }

        // 打印每一个Chunk的大小
        for(size_t i=0; i < parts.size(); i++){
            auto ts = printMemorySize(parts[i].size());
            auto output = "Chunk " + toString(i) + " " + ts;
            recordChunkOrder(order[i], i);
            writeToLog(output, "ouput_log.txt");
            cout << endl;
        }
        // sleep(1);

        return parts;
    }
    
    bool checkFather(h256& _hash){
        
        // 遍历到根节点了
        if(fatherMap.find(_hash) == fatherMap.end()){
            return false;
        }

        auto hash = fatherMap[_hash];
        auto value = m_dataSet[hash].first;
        auto& meta = m_dataSet[hash].second;
        RLP rlp(value);
        // cout << "Checking Father:" << rlp.itemCount() << endl;
        
        for (size_t i = 0; i < rlp.itemCount() - 1; ++i) {
            if (rlp[i].isEmpty()) continue; // 跳过空节点

            h256 childHash = rlp[i].toHash<h256>(); // 获取子节点哈希
            // cout << "We are detect(cnt=" << i << ") " << childHash << endl;
            // 如果子节点哈希 不 存在于 doneset 这意味着其 Metadata并没有初始化好，直接中止操作
            if (m_dataSet_init[childHash] != true) {
                // cout << "Not in " << childHash << endl;
                // return false;
            }
        }
        ready_node.push(make_pair(hash, partitions[hash]));
        partitions.erase(hash);
        return true;
    }

    vector<string> handleDataSetWithReadyQueue(int cnt){
        
        // 初始化每个分组对应的 chunk 
        vector<string> parts(cnt);
        // 完成情况 
        // unordered_set<h256> done_set; 
        // while(!partitions.empty()){
        //     auto check = partitions.size();
            while(!ready_node.empty()){
                auto it = ready_node.front();
                ready_node.pop();
                auto hash = it.first; 
                auto group = it.second; // 所属节点编号 
                auto value = m_dataSet[hash].first;
                auto& meta = m_dataSet[hash].second;
                
                
                // cout << "+++++++++++++   Handledataset hash:" << hash 
                //     // << " Offset" <<  meta.m_offset
                //     << " Value" <<  value.size() << " Group:" << group
                //     << " MetaStr" <<  metaStr.size() <<endl;
                // cout << "data size " << data.size() 
                    // << endl;

                RLP rlp(value);
                // 判断是否是叶子节点
                if(rlp.itemCount() == 2 && isLeaf(rlp)){
                    auto& data = parts[group];
                    // 更新该节点的 metadata
                    meta.m_offset = data.size();
                    meta.m_lengh = (value.size() & 0xFFFFFF) | (0 << 24);
                    meta.m_node = (uint8_t)group;
                    // 因为是叶子节点，他没有子节点，无需在value后面添加
                    data = data + value;
                    // 成功插入一个 hash
                    // cout << "succees insert leaf:" << hash;
                    // meta.printNodeMetadata();

                    m_dataSet_init[hash] = true;
                    // it = partitions.erase(it);
                    // partitions.erase(hash);
                    // continue;
                }
                else{
                    // 检测是否有还未被初始化的 Metadata
                    bool can_build = true;
                    vector<pair<NodeMetadata, int>> childrenMeta;
                    if(rlp.itemCount() == 2){
                        auto childHash = rlp[1].toHash<h256>();
                        // cout << "We are detect(cnt=2) " << childHash << endl;
                        // 如果子节点哈希 不 存在于 doneset 这意味着其 Metadata并没有初始化好，直接中止操作
                        if (m_dataSet_init[childHash] != true) {
                            // cout << "Not in " << childHash << endl;
                            childrenMeta.clear();
                            can_build = false;
                            // break;
                        }
                        else{
                            // 将对应子节点的 Nodemeta 和 nodeVersion 塞进一个 tmp 中
                            auto tmp = make_pair(m_dataSet[childHash].second, m_nodeVersions[childHash]);
                            childrenMeta.push_back(tmp);
                        }
                    }
                    else{
                        for (size_t i = 0; i < rlp.itemCount(); ++i) {
                            if (rlp[i].isEmpty()) continue; // 跳过空节点

                            h256 childHash = rlp[i].toHash<h256>(); // 获取子节点哈希
                            // cout << "We are detect(cnt=n) " << childHash << endl;
                            // 如果子节点哈希 不 存在于 doneset 这意味着其 Metadata并没有初始化好，直接中止操作
                            if (m_dataSet_init[childHash] != true) {
                                // cout << "Not in   " << childHash << endl;
                                childrenMeta.clear();
                                can_build = false;
                                break;
                            }
                            else{
                                // 将对应子节点的 Nodemeta 和 nodeVersion 塞进一个 tmp 中
                                auto tmp = make_pair(m_dataSet[childHash].second, m_nodeVersions[childHash]);
                                childrenMeta.push_back(tmp);
                            }
                        }
                    }
                    // 把数据插入 chunk 当中
                    if(can_build){
                        auto& data = parts[group];
                        meta.m_offset = data.size();

                        data.append(value);


                        // 所有的 MetaData 的合计大小
                        uint8_t meta_size = 0;
                        
                        string str;
                        for(auto& child : childrenMeta){
                            // 计算该节点所有孩子节点的 NodeMetaData 大小
                            auto nodemeta = child.first;
                            uint16_t ver(child.second);
                            meta_size += nodemeta.Size() + sizeof(ver);
                            // 计算该节点的 NodeMetaData 和 Versiondiff
                            auto serializedData = serializeMetadataAndVersionDiff(nodemeta, ver);
                            data = data + serializedData;
                            // 设置节点 hash 何其对应的 childrenNode 的 Metadata
                            str = str + serializedData;

                        }
                        meta.m_lengh = (value.size() & 0xFFFFFF) | (meta_size << 24);
                        meta.m_node = (uint8_t)group;
                        
                        // 成功插入一个 hash
                        // cout << "succees insert No-leaf:" << hash;
                        m_dataWithchildsNodeMetadata[hash] = str;
                        // meta.printNodeMetadata();
                        

                        // 插入以后 该数据的 metaData也是可以使用了
                        m_dataSet_init[hash] = true;
                        // partitions.erase(hash);
                        // continue; 
                    }
                }
                // 检测自己的父亲节点，若ok则加入 ready
                checkFather(hash);
            // }
            // assert(check != partitions.size());
            // if(check == partitions.size()){
            //     cout << "数量没有变化，可能出错了！" << endl;
            //     break;
            // }
        }

        // 打印每一个Chunk的大小
        for(int i=0; i < parts.size(); i++){
            // cout << "Chunk[" << i << "] Size is ";
            auto ts = printMemorySize(parts[i].size());
            auto output = "Chunk " + toString(i) + " " + ts;
            writeToLog(output, "ouput_log.txt");
            cout << endl;
        }
        // sleep(1);

        return parts;
    }

    void initFatherMap(h256 hash, RLP rlp){
        for (size_t i = 0; i < rlp.itemCount()-1; ++i) {
            if (rlp[i].isEmpty()) continue; // 跳过空节点

            h256 childHash = rlp[i].toHash<h256>(); // 获取子节点哈希
            // cout << "We are finding father (cnt=" << rlp.itemCount()-1 << ") " << childHash << endl;
            fatherMap[childHash] = hash;

        }
    }

    void initReadyQueue(h256 hash, RLP rlp){
        if(rlp.itemCount() == 2 && isLeaf(rlp)){
            ready_node.push(make_pair(hash, partitions[hash]));
            partitions.erase(hash);
        }
    }

    void initPartitions(unordered_map<h256, uint16_t> _partitions){
        partitions = _partitions;
    }

    void processBatch(std::unordered_map<h256, std::string> batch, unordered_map<h256, int>& m_nodeVersions) {
        // 2. 解析每个字符串 RLP，处理指向的节点
        for (const auto& pair : batch) {
            auto hash = pair.first;
            auto rlpStr = pair.second;
            try {
                // 将字符串 RLP 转换为 RLP 对象
                RLP rlp(rlpStr);
                NodeMetadata meta;
            
                // 如果是叶子节点 则因其没有子节点直接跳过
                // if(rlp.itemCount() == 2 && isLeaf(rlp)){
                //     meta.m_versionDiffs.push_back(m_nodeVersions[hash]);
                //     m_dataSet[hash] = make_pair(rlpStr, meta);
                //     continue;
                // }

                // auto childHash = rlp[1].toHash<h256>();
                if(m_nodeVersions.find(hash) != m_nodeVersions.end()){
                    // meta.m_versionDiffs.push_back(m_nodeVersions[hash]);
                    m_dataSet[hash] = make_pair(rlpStr, meta);
                    m_dataSet_init[hash] = false;
                }

                // 初始化一个 孩子-父亲 的映射
                initFatherMap(hash, rlp);

                initReadyQueue(hash, rlp);

                // cout << "process batch hash: " << hash 
                //     << " RlP" << rlp
                //     // << "VerDiff " << meta.m_versionDiffs
                //     << endl;

            }catch (const std::exception& ex) {
                // 捕获 RLP 解析错误
                std::cerr << "Error processing RLP for hash " << hash << ": " << ex.what() << std::endl;
            }
        }
    }

    void printDataSet(){
        cout << "= = = Data Set = = =" << endl;
        for(auto& ele : m_dataSet){
            auto hash = ele.first;
            auto pair = ele.second;
            auto value = pair.first;
            auto meta = pair.second;
            cout << "Hash[" << hash << "]" << " Value[" << dev::RLP(value) << "] ";
            meta.printNodeMetadata();
        }
        cout << "= = = = = = = = = =" << endl;
    }

    pair<int, int> StorageForChunks(vector<string>& parts, unordered_map<h256, std::string>& encoded_set, int64_t& s, int64_t& ex, int64_t& e){
        // 打印每一个Chunk的大小
        int64_t state_size = 0;
        int64_t encoded_size = 0;
        int64_t extraInfo_size = TotalMetaSize;
        for(size_t i=0; i < parts.size(); i++){
            // cout << "Chunk[" << i << "] Size is ";
            state_size += parts[i].size();
        }
        for(auto& p: encoded_set){
            encoded_size += p.second.size();
            // cout<<"0000"<<endl;
        }
        s += state_size - extraInfo_size;
        ex += extraInfo_size;
        e += encoded_size;

        auto s1 = printMemorySize(state_size - extraInfo_size);
        auto s2 = printMemorySize(extraInfo_size);
        auto s3 = printMemorySize(encoded_size);
        
        auto output = "State Size: " + s1 + ", ExtraInfo Size: " + s2 + ", Encoded Size: " + s3;
        writeToLog(output, "ouput_log.txt");

        return make_pair(state_size, extraInfo_size);
    }

    ChunkBuilder() = default;

    // ChunkBuilder(unordered_map<h256, pair<std::string, NodeMetadata>>& dataSet, unordered_map<h256, bool>& dataSet_init) : 
    //     m_dataSet(dataSet), m_dataSet_init(dataSet_init) {}

    ChunkBuilder(VersionManager& vm) : m_dataSet(vm.dataSet), m_dataSet_init(vm.dataSet_init), 
        m_nodeVersions(vm.m_nodeVersions), m_dataWithchildsNodeMetadata(vm.dataWithchildsNodeMetadata), block_number(vm.m_currentVersion){}
};