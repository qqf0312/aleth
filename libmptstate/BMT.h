/**
 * @BMT树，主要负责组织由状态构成的数据块和编码块
 *功能包括：
 * 1. 读取状态块（本地和远程）
 * 2. 存储状态分布信息（状态存在的节点和BMT）
 * 3. 通过EC恢复缺失的数据
 * 
 * @file Mediator.h
 * @author qqf
 * @date 2024-10-30
 */
#pragma once

#include <libethereum/Account.h>
#include "CodeSizeCache.h"
#include <libdevcore/Common.h>
#include <libdevcore/OverlayDB.h>
#include <libdevcore/RLP.h>
#include <libdevcore/TrieDB.h>
#include <libethcore/Exceptions.h>
#include <array>
#include <unordered_map>

#include <libdevcore/Assertions.h>
#include <libdevcore/LevelDB.h>
#include <libdevcore/TrieHash.h>
//modified 2020/8
#include <fstream>

#include "VersionManager.h"
// #include "RLP.h"
// #include <MPTState.h>
using namespace std;


namespace dev{
    
    struct Node{
        uint _index; // 索引
        h256 _hash; // 哈希
        std::vector<h256> p; // 校验块
        std::shared_ptr<Node> left_child;  // 子节点（二叉） 如果是空的那么就是
        std::shared_ptr<Node> right_child;

        Node(h256 h, uint i): _index(i), _hash(h), left_child(nullptr), right_child(nullptr) {
            // std::cout << "叶子节点hash:"<< _hash << "#" << _index << std::endl;
        }
        Node(uint i, std::shared_ptr<Node>& l, std::shared_ptr<Node>& r): _index(i) {
            _hash = sha3(dev::toString(l->_hash) + dev::toString(r->_hash));
            left_child = l;
            right_child = r;
            // std::cout << "节点hash:"<< _hash << "#" << _index << std::endl;
        }

    }; 

    class _MerkleTree{
    public:

        struct MerkleNode
        {
            /* data */
            h256 hash;
            MerkleNode* left;
            MerkleNode* right;

            MerkleNode(h256 hash_val) : hash(hash_val), left(nullptr), right(nullptr) {}

        };

        MerkleNode* generateMerkleTree(const vector<string> data){
            vector<MerkleNode*> nodes;
             // 创建叶子节点
            for (const string& item : data) {
                nodes.push_back(new MerkleNode(sha3(item)));
            }

            // 逐层构建Merkle树
            while (nodes.size() > 1) {
                vector<MerkleNode*> parentNodes;

                // 配对节点并生成父节点
                for (size_t i = 0; i < nodes.size(); i += 2) {
                    MerkleNode* left = nodes[i];
                    MerkleNode* right = (i + 1 < nodes.size()) ? nodes[i + 1] : left; // 如果节点数为奇数，重复最后一个节点

                    auto combinedHash = sha3(dev::toString(left->hash) + dev::toString(right->hash)); // 合并左右节点的哈希值
                    MerkleNode* parent = new MerkleNode(combinedHash);
                    parent->left = left;
                    parent->right = right;

                    parentNodes.push_back(parent);
                }

                // 更新当前层次节点
                nodes = parentNodes;
            }

            // 返回根节点
            return nodes.empty() ? nullptr : nodes[0];
        }

        void printMerkleTree(MerkleNode* node, int depth = 0) {
            if (node == nullptr) return;
            // print hash current

            cout<< string (depth * 2, ' ') << "Hash:" << node->hash <<endl;

            printMerkleTree(node->left, depth + 1);
            printMerkleTree(node->right, depth + 1);
        }

        void printTree(){
            printMerkleTree(root);
        }

        // h256 root_hash(){
        //     root->hash;
        // }

        MerkleNode* root;
        
        _MerkleTree(const vector<string>& data) {
            root = generateMerkleTree(data);
        }
        
        _MerkleTree(){}
    };
        
    static vector<std::string> splitStr(const string& str, size_t n){
        vector<string> rlt;
        for(size_t i = 0; i < str.size(); i += n) {
            rlt.emplace_back(str.begin() + i, str.begin() + min(i+n, str.size()));
        }
        return rlt;
    }
    
    class BMT{
    public:
        std::shared_ptr<Node> bmt_root; // 根哈希
        // std::unordered_map<h256, uint> account_to_num; // 状态数据到编号的映射
        std::unordered_map<h256, std::string> state_cache; // MPT节点的KV表现形式 其中string为编码过的数据 需要调用RLP解码
        std::map<h256, std::vector<h256>> ancestors_leaves;
        vector<_MerkleTree> MerkleTrees;
        int l = 0; // 树的高度

        BMT(const std::unordered_map<h256, uint> data_list) {
            buildTree(data_list);
            traverseNonLeafNodes(bmt_root);
        } 
        // 真实系统返回的存储在系统的 cache 为 std::unordered_map<h256, std::string>
        BMT(std::unordered_map<h256, std::string>& get_cache) {
            state_cache = get_cache;
            buildTree(assignIndices(state_cache));
            traverseNonLeafNodes(bmt_root);
            std::cout<<"BMTRoot make from each state = "<< bmt_root -> _hash <<std::endl;
        }
        // 已经制作好的 chunks
        BMT(vector<string> chunks) {
            std::unordered_map<h256, uint> chunk_to_node;
            int cnt = 0;
            // 计算耗时
            auto t1 = std::chrono::steady_clock::now();
            auto _max = t1 - t1;

            for(auto& chunk : chunks){

                _MerkleTree mTree(splitStr(chunk, 100)); // chunk 切分为n块
                auto chunk_hash = mTree.root->hash;
                state_cache[chunk_hash] = chunk;
                
                chunk_to_node[chunk_hash] = cnt++;
                MerkleTrees.push_back(mTree);

                // 记录耗时最高的 build chunk
                auto t_2 = std::chrono::steady_clock::now();
                _max = std::max(_max, t_2 - t1);
                t1 = std::chrono::steady_clock::now();

                cout << "chunks hash = " << chunk_hash << endl;
            }
            // 记录构建 chunk Merkle树的耗时
            auto t2 = std::chrono::steady_clock::now();

            buildTree(chunk_to_node);

            // 记录构建BMT的耗时
            auto t3 = std::chrono::steady_clock::now();
            auto MPT_time = std::chrono::duration_cast<std::chrono::microseconds>(_max).count() / 1000.0;
            auto BMT_time = std::chrono::duration_cast<std::chrono::microseconds>(t3 - t2).count() / 1000.0;
            auto logStr = "Build Chunk Merkle Tree: " + dev::toString(MPT_time) + "ms. "
                + "Build BMT Tree: " + dev::toString(BMT_time) + "ms. "
                + "Sum:" + dev::toString(BMT_time + MPT_time);
            writeToLog(logStr,"output_log.txt");

            traverseNonLeafNodes(bmt_root);
            std::cout<<"BMTRoot make from chunks = "<< bmt_root -> _hash <<std::endl;
        }
        BMT(){}   

        std::unordered_map<h256, uint> assignIndices(const std::unordered_map<h256, std::string>& inputMap){
            std::unordered_map<h256, uint> indexedMap;
            uint index = 0;

            for(const auto& ele : inputMap){
                cout << "building hash -> " << ele.first
                    << " index -> " << index << endl;
                indexedMap[ele.first] = index++;
            }
            return indexedMap;
        }
        
        /**
        * @brief 构造BMT树
        * 
        * 此函数构通过 状态数据地址 及其 编号 来生成BMT树
        * 
        * @param data_list 状态数据的 map
        */
        void buildTree(const std::unordered_map<h256, uint>& data_list){
            std::vector<std::shared_ptr<Node>> current_level;
            for(const auto& data : data_list){
                current_level.push_back(std::make_shared<Node>(data.first, data.second));
            }
            std::sort(current_level.begin(),current_level.end(), [](const std::shared_ptr<Node>& a, const std::shared_ptr<Node>& b){
                return a->_index < b->_index; // 按照int升序排序
            });

            // 构建树的各个层级
            while (current_level.size()>1){
                std::vector<std::shared_ptr<Node>> next_level;
                for(size_t i=0; i<current_level.size(); i+=2){
                    if(i+1<current_level.size()){
                        auto tmp = (current_level[i]->_index + current_level[i+1]-> _index)/2;
                        next_level.push_back(std::make_shared<Node>(tmp, current_level[i], current_level[i+1]));
                    }
                    else
                    {
                        /* 奇数的时候，自身与前一个配对 */
                        auto tmp = (current_level[i-1]->_index + current_level[i]-> _index)/2;
                        next_level.push_back(std::make_shared<Node>(tmp, current_level[i-1], current_level[i]));
                    }
                }
                current_level = next_level;
                l++;
            }
            if(!current_level.empty()){
                bmt_root = current_level[0];
            }
        }
        
        /**
        * @brief 寻找节点指针
        * 
        * 此函数通过状态地址寻找节点指针
        * 
        * @param _target 目标状态地址
        * @return shared_ptr<Node> 返回节点指针
        */
        std::shared_ptr<Node> search(dev::h256 _target) const {
            return searchNode(bmt_root, _target);
        }

        /**
        * @brief 寻找节点指针
        * 
        * 此函数通过状态地址寻找节点指针
        * 
        * @param _target 目标状态地址
        * @return shared_ptr<Node> 返回节点指针
        */
        std::shared_ptr<Node> searchNode(const std::shared_ptr<Node>& node, dev::h256 target) const {
            if(!node) 
                return nullptr;

            if(target == node->_hash)
                return node; // 返回对应 target 的 hash
            
            auto rlt = searchNode(node->left_child, target);
            if(rlt != nullptr){
                return rlt;
            }
            return searchNode(node->right_child, target);
        }

        /**
        * @brief 寻找节点指针(以序号查找)
        * 
        * 此函数通过状态地址寻找节点指针
        * 
        * @param _target 目标状态地址
        * @return shared_ptr<Node> 返回节点指针
        */
        std::shared_ptr<Node> searchWithOrder(int order, int& current_order) const {
            cout << "Order = " << order;
            return searchLeafWithOrder(bmt_root, order, current_order);
        }

        std::shared_ptr<Node> searchLeafWithOrder(const std::shared_ptr<Node>& node, int order, int& current_order) const {
            if(!node) 
                return nullptr;

            if(node->left_child == nullptr && node->right_child == nullptr){
                if(current_order == order){
                    return node;
                }
                cout << "Go ahead " << order << " " << current_order << endl;
                ++current_order;
                return nullptr;
            }
                
            if(node->left_child){
                auto left_rlt = searchLeafWithOrder(node->left_child, order, current_order);
                if(left_rlt){
                    return left_rlt;
                }
            }
            
            if(node->right_child){
                auto right_rlt = searchLeafWithOrder(node->right_child, order, current_order);
                if(right_rlt){
                    return right_rlt;
                }
            }

            return nullptr;
        }

        /**
        * @brief 寻找目标节点祖先
        * 
        * 查找BMT中对应状态的 hash 值，并记录祖先节点（该祖先是从底层往根节点递增
        * 
        * @param node 节点指针
        * @param target_hash 目标状态地址
        * @param ancestor 目标节点所有祖先的集合
        */
        bool findAncestors(const std::shared_ptr<Node>& node, h256 target_hash, std::vector<std::shared_ptr<Node>>& ancestors) const {
            if (!node) return false;

            // 如果找到目标叶子节点
            if(node->_hash == target_hash){
                // ancestors.push_back(node);
                return true;
            }

            if(findAncestors(node->left_child, target_hash, ancestors)){
                ancestors.push_back(node); // 记录祖先
                return true;
            }

            if(findAncestors(node->right_child, target_hash, ancestors)){
                ancestors.push_back(node); // 记录祖先
                return true;
            }

            return false;
        }

        /**
        * @brief 记录以当前节点为根的子树的所有叶子节点
        * 
        * 
        * @param node 当前节点指针
        * @param leaves 当前节点的叶子集合
        */
        void recordLeaves(const std::shared_ptr<Node>& node, std::vector<h256>& leaves) const {
            if(!node) return;

            // 叶子节点
            if(!node->left_child && !node->right_child){
                // std::cout << "Leaf Hash:" << node->_hash << " # " << node->_index << std::endl;
                if(std::find(leaves.begin(), leaves.end(), node->_hash) == leaves.end())
                    leaves.push_back(node->_hash);
            }

            else{
                // 递归遍历左右子树，直到找到叶子节点
                recordLeaves(node->left_child, leaves);
                recordLeaves(node->right_child, leaves);
            }
        }

        /**
        * @brief 遍历BMT
        * 
        * 将每一个子树的根节点和叶子节点记录在 this.ancestors_leaves 中
        * 
        * @param node 当前节点指针
        */
        void traverseNonLeafNodes(const std::shared_ptr<Node>& node){
            
            // 如果节点为空
            if(node == nullptr){
                return;
            }
            
            if(node->left_child != nullptr || node->right_child != nullptr){
                std::vector<h256> leaves;
                // std::cout << "Ancestors Node Hash:" << node->_hash << " # " << node->_index <<std::endl;
                // std::cout << "Leaves in this subtree:\n";
                recordLeaves(node, leaves);
                ancestors_leaves.insert(make_pair(node->_hash, leaves));
            }

            traverseNonLeafNodes(node->left_child);
            traverseNonLeafNodes(node->right_child);
        }

        /**
        * @brief 寻找目标节点的祖先和他的叶子们
        * 
        * 寻找目标节点，并列出其每一个祖先节点的所有子节点，以支持渐进式编码
        * 
        * @param target 目标状态的地址
        * @return vector<pair<h256, vector<h256>>> 目标状态所在的 子树根节点 和 子树叶子 的集合
        */
        std::vector<std::pair<h256, std::vector<h256>>> findAncestorsAndLeaves(h256 target){
            
            std::vector<std::pair<h256, std::vector<h256>>> rlt;

            std::vector<std::shared_ptr<Node>> ancestors;
            
            // std::cout<<"12"<<std::endl;

            // std::cout << "bmt_root "<< bmt_root->_hash 
            //     << "\n target " << target << std::endl;

            // std::cout<<"34"<<std::endl;

            if(findAncestors(bmt_root, target, ancestors)){
                for(const auto& ancestor: ancestors){
                    // std::cout << "Ancestors Node Hash:" << ancestor->_hash << " # " << ancestor->_index <<std::endl;
                    // std::cout << "Leaves in this subtree:\n";
                    std::vector<h256> leaves;
                    recordLeaves(ancestor, leaves);
                    rlt.push_back(make_pair(ancestor->_hash, leaves));
                    // std::cout<< leaves <<std::endl;
                }
            }
            else{
                std::cout << "Target Not Found" << std::endl;
            }
            return rlt;
        }

        using Edge = std::pair<h256,h256>;   // src -> dst

        // 原有：简单 DFS，不去重、不去环 + 计数/编号/打印
        void buildEdges(const std::shared_ptr<Node>& cur,
                        const std::shared_ptr<Node>& parent,
                        const std::shared_ptr<Node>& nearestP,
                        vector<Edge>& out,
                        vector<h256>& datachunk, // 数据块顺序
                        vector<h256>& paritychunk,  // 校验块顺序
                        uint64_t& leaf_no,   // ★ 叶子计数器
                        uint64_t& p_no)      // ★ p 元素计数器
        {
            if (!cur) return;

            const bool isLeaf = (!cur->left_child && !cur->right_child);
            const bool curP   = !cur->p.empty();

            // --- 编号/打印：叶子 ---
            if (isLeaf) {
                std::cout << "[LEAF #" << leaf_no << "] hash=" << cur->_hash << "\n";
                datachunk.emplace_back(cur->_hash);
                ++leaf_no;
            }

            // 原规则1：叶子 → 最近 p 非空祖先
            if (isLeaf && nearestP) {
                out.emplace_back(cur->_hash, nearestP->_hash);
            }

            // 原规则2：非叶且自身 p 非空 → 直接父亲
            if (!isLeaf && curP && parent) {
                out.emplace_back(cur->_hash, parent->_hash);
            }

            // 原规则3：节点 → p 中每个成员
            // 同时对每个 p 元素编号/打印（按遍历顺序）
            for (const auto& ph : cur->p) {
                std::cout << "  [P    #" << p_no << "] ph=" << ph << "\n";
                paritychunk.emplace_back(ph);
                ++p_no;

                out.emplace_back(ph, cur->_hash);
            }

            // 更新“最近 p 非空祖先”
            auto nextNearest = curP ? cur : nearestP;

            buildEdges(cur->left_child,  cur, nextNearest, out, datachunk, paritychunk, leaf_no, p_no);
            buildEdges(cur->right_child, cur, nextNearest, out, datachunk, paritychunk, leaf_no, p_no);
        }

        // 包装：从根启动，并返回 edges；顺便完成编号打印
        std::vector<Edge> buildIndexFromLeaves(vector<h256>& VCgroup, string& encoding_group)
        {
            std::vector<Edge> edges;
            vector<h256> datachunk;
            vector<h256> paritychunk;
            uint64_t leaf_no = 0;
            uint64_t p_no    = 0;

            buildEdges(bmt_root, /*parent*/nullptr, /*nearestP*/nullptr,
                    edges, datachunk, paritychunk, leaf_no, p_no);

            std::cout << "\n== Summary ==\n"
                    << "Leaf count: " << leaf_no << "\n"
                    << "P items:    " << p_no    << "\n";

            encoding_group = buildEncodingCombo(edges, datachunk, paritychunk);
            
            VCgroup = move(datachunk);
            VCgroup.insert(VCgroup.end(), paritychunk.begin(), paritychunk.end());

            return edges;
        }

        string buildEncodingCombo(
            const std::vector<std::pair<dev::h256,dev::h256>>& edges,
            const std::vector<dev::h256>& datachunk,
            const std::vector<dev::h256>& paritychunk
        ) {
            // NodeComboBytes combo_out;

            const size_t leaf_total = datachunk.size();

            // 1) 建立 hash->下标 映射
            std::unordered_map<dev::h256, size_t> idx_of;
            idx_of.reserve(datachunk.size() + paritychunk.size());

            for (size_t i = 0; i < datachunk.size(); ++i)
                idx_of[datachunk[i]] = i;                       // 叶子编号: 0..leaf_total-1

            for (size_t j = 0; j < paritychunk.size(); ++j)
                idx_of[paritychunk[j]] = leaf_total + j;        // p 全局编号: leaf_total..leaf_total+P-1

            // 2) 收集每个节点拥有哪些 parity（从 p->node 的边看）
            std::unordered_set<dev::h256> parity_set(paritychunk.begin(), paritychunk.end());
            std::unordered_map<dev::h256, std::vector<dev::h256>> node_parities;
            for (const auto& e : edges) {
                const auto& src = e.first;     // 可能是 leaf 或 p
                const auto& dst = e.second;    // 节点
                if (parity_set.find(src) != parity_set.end()) {
                    node_parities[dst].push_back(src);
                }
            }

            // 3) 对每个“有 p 的节点”，把 [叶子id们] + [p全局id们] 打包成一个 combo（vec<uint8_t>）
            string combo;
            for (const auto& [node, p_list] : node_parities) {
                if (p_list.empty()) continue;

                auto it = ancestors_leaves.find(node);
                if (it == ancestors_leaves.end()) {
                    std::cerr << "[buildEncodingCombo] node has p but no leaves in ancestors_leaves\n";
                    continue;
                }

                const auto& leaves = it->second; // 叶子哈希列表

                // std::vector<uint8_t> combo;     // 你的需求：用 uint8_t 装“编号”
                // combo.reserve(leaves.size() + p_list.size());
                combo.append("[");
                cout << "[";

                // 3.1 叶子编号（按 datachunk 下标升序会更稳定；这里按存储次序）
                for (const auto& leaf_h : leaves) {
                    auto itid = idx_of.find(leaf_h);
                    if (itid == idx_of.end()) {
                        std::cerr << "Leaf hash not in datachunk idx map\n";
                        continue;
                    }
                    size_t id = itid->second;
                    if (id > 255) {
                        std::cerr << "Leaf id > 255; truncated to uint8_t\n";
                    }
                    // combo.push_back(static_cast<uint8_t>(id & 0xFF));
                    append_u8(combo, (uint8_t)id);
                    cout << static_cast<unsigned>((uint8_t)id);
                }

                combo.append("|"); // 添加 parity 和 datachunk 的分隔符
                cout << "|";

                // 3.2 parity 全局编号
                for (const auto& ph : p_list) {
                    auto itpid = idx_of.find(ph);
                    if (itpid == idx_of.end()) {
                        std::cerr << "Parity hash not in paritychunk idx map\n";
                        continue;
                    }
                    size_t gid = itpid->second; // 已经是“叶子总数 + 本地”
                    if (gid > 255) {
                        std::cerr << "Parity global id > 255; truncated to uint8_t\n";
                    }
                    // combo.push_back(static_cast<uint8_t>(gid & 0xFF));
                    append_u8(combo, (uint8_t)gid);
                    cout << static_cast<unsigned>((uint8_t)gid);
                }
                combo.append("]");
                cout << "]" << endl;
                // combo_out.emplace(node, std::move(combo));
            }

            return combo;
        }

    };
}