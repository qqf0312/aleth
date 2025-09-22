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

            // MerkleNode(const MerkleNode& other): hash(other.hash), left(nullptr), right(nullptr){
            //     if(other.left){
            //         left = new MerkleNode(*other.left);
            //     }
            //     if(other.right){
            //         right = new MerkleNode(*other.right);
            //     }
            // }

            // MerkleNode& operator=(const MerkleNode& other){
            //     if(this == &other){
            //         return *this;
            //     }

            //     delete left;
            //     delete right;

            //     hash = other.hash;
            //     left = other.left ? new MerkleNode(*other.left) : nullptr;
            //     right = other.right ? new MerkleNode(*other.right) : nullptr;

            //     return *this;
            // }
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

        // // 深拷贝构造函数
        // _MerkleTree(const _MerkleTree& other) {
        //     root = copyMerkleTree(other.root);
        // }

        // // 深拷贝赋值运算符
        // _MerkleTree& operator=(const _MerkleTree& other) {
        //     if (this != &other) {
        //         clearMerkleTree(root);
        //         root = copyMerkleTree(other.root);
        //     }
        //     return *this;
        // }

        // // 深拷贝函数：递归复制Merkle树
        // MerkleNode* copyMerkleTree(MerkleNode* node) {
        //     if (!node) return nullptr;
        //     MerkleNode* newNode = new MerkleNode(node->hash);
        //     newNode->left = copyMerkleTree(node->left);
        //     newNode->right = copyMerkleTree(node->right);
        //     return newNode;
        // }

        // // 清理树的内存
        // void clearMerkleTree(MerkleNode* node) {
        //     if (!node) return;
        //     clearMerkleTree(node->left);
        //     clearMerkleTree(node->right);
        //     delete node;
        // }
        
        _MerkleTree(){}
    };
        


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

        // // 拷贝构造函数
        // BMT(const BMT& other){
        //     // sleep(1);
        //     if(other.bmt_root){
        //         bmt_root = deepCopyNode(other.bmt_root);
        //     }
        //     // cout<<"深度拷贝函数" << endl;
        //     state_cache = other.state_cache;
        //     ancestors_leaves = other.ancestors_leaves;
        //     MerkleTrees = other.MerkleTrees;
        // }

        // // 拷贝赋值运算=
        // BMT& operator=(const BMT& other){
        //     // cout<<"拷贝赋值运算" << endl;
        //     // sleep(1);
        //     if(this != &other){
        //         bmt_root = other.bmt_root ? deepCopyNode(other.bmt_root) : nullptr;
        //         state_cache = other.state_cache;
        //         ancestors_leaves = other.ancestors_leaves;
        //         MerkleTrees = other.MerkleTrees;
        //     }
        //     return *this;
        // }

        // // 辅助函数 用于递归拷贝Node
        // shared_ptr<Node> deepCopyNode(const shared_ptr<Node>& node){
        //     if(!node){
        //         return nullptr;
        //     }
        //     auto newNode = make_shared<Node>(node->_hash, node->_index);
        //     newNode->left_child = deepCopyNode(node->left_child);
        //     newNode->right_child = deepCopyNode(node->right_child);
        //     return newNode;
        // }

        vector<std::string> splitStr(const string& str, size_t n){
            vector<string> rlt;
            for(size_t i = 0; i < str.size(); i += n) {
                rlt.emplace_back(str.begin() + i, str.begin() + min(i+n, str.size()));
            }
            return rlt;
        }

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
    };
}