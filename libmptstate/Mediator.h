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

#include <libmptstate/Eurasure.h>
#include <libmptstate/MPTState.h>
#include <libdevcore/RLP.h>
#include <tbb/tbb.h>
#include <tbb/parallel_for.h>
// #include <tbb/global_control.h>
#include <tbb/task_scheduler_init.h>
#include <atomic>

class Mediator{
public:
    dev::mptstate::MPTState* mpt_ptr;
    atomic<int> malicious_nodes{0};
    // std::unordered_map<dev::h256, StateLocation>* location_ptr;

    Mediator(dev::mptstate::MPTState &mptstate){
        mpt_ptr = &mptstate;
        // location_ptr = mptstate.stateHashToInfoMap;
    }
    
    // string at(h256 m_root, u160 _key) const
    // {
    //     bytesConstRef _k((byte const*)&_key, sizeof(u160));
    //     std::cout<<"到达根节点,其下一个节点的hash/key为:"<< m_root <<
    //     "||" << NibbleSlice(_k) <<std::endl;
    //     return atAux(RLP(m_root), _k);
    // }

    // string atAux(RLP const& _here, NibbleSlice _key) const
    // {
    //     if (_here.isEmpty() || _here.isNull())
    //         // not found.
    //         return std::string();
    //     unsigned itemCount = _here.itemCount();
    //     assert(_here.isList() && (itemCount == 2 || itemCount == 17));
    //     // 2024/10/12 _here[n]: 节点的hash值  _key:字典树的值
    //     // itemcount=2时节点为 叶子节点 或 扩展节点
    //     // 正常情况下 node(_hash) 是用来寻找 _hash 对应的节点函数(?)
    //     // else则为 itemcount=17的情况，即为 分支节点
    //     // 可知他需要根据key的第一个值(key[0])来判断他应该走哪个分支，然后根据hash寻找下一个节点
        
    //     /* 打印出一些中间值查看效果 */
    //     // std::cout<<"---Entry func atAux---Finding key word: " << _key <<std::endl;

    //     if (itemCount == 2)
    //     {
    //         auto k = keyOf(_here);
    //         if (_key == k && isLeaf(_here))
    //             // reached leaf and it's us
    //             return _here[1].toString();
    //         else if (_key.contains(k) && !isLeaf(_here))
    //             // not yet at leaf and it might yet be us. onwards...
    //             return atAux(_here[1].isList() ? _here[1] : RLP(_here[1].toHash<h256>()),
    //                 _key.mid(k.size()));
    //         else
    //             // not us.
    //             return std::string();
    //     }
    //     else
    //     {
    //         if (_key.size() == 0)
    //             return _here[16].toString();
    //         auto n = _here[_key[0]];
    //         if (n.isEmpty())
    //             return std::string();
    //         else
    //             // return atAux(n.isList() ? n : RLP(node(n.toHash<h256>())), _key.mid(1));
    //             return atAux(n.isList() ? n : RLP(n.toHash<h256>()), _key.mid(1));
    //     }
    // }

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

    std::string readChunk(dev::h256 target, int location = 0, int nodeId = -1) {
        // auto state_location = mpt_ptr->stateHashToInfoMap[target];
        // 从目标节点读取 节点id 区块编号
        std::string ret = ""; // 返回值放入其中 若为空则找不到

        // 从本地读
        if(location){

            auto chunk_location = locationChunk(target, location);
            // ×伪造节点沉默现象
            int f = 32 * 0.3;
            // cout << "chunk location :" << nodeId 
            //     << " ,f :" << f << endl;
            if(nodeId % 32 < f && (nodeId!=-1)){
                return ret;
            }

            ret = mpt_ptr->getState().db().lookup(target);
            if(ret == ""){
                // cout<< "state size : " << (mpt_ptr->BMT_map[1]).state_cache << endl;
                auto tt = (mpt_ptr->BMT_map[location]).state_cache;
                // for(auto it = tt.begin(); it!=tt.end(); it++){
                //     cout << "ele:" << it->first << endl;
                // }
                if(tt.find(target)!=tt.end()){
                    ret = tt.find(target) -> second;
                    // cout << "fids" <<endl;
                }
                else{
                    // cout << "??fu" <<endl;
                }
            }
            // std::cout << "Target : "<< target << ", value : "<< ret <<std::endl;
        }
        else{
            // 从远程读取
        }

        // std::string ret; // 返回值放入其中 若为空则找不到
        return ret;
    }

    void recoverState(dev::h256& target_state, int idx, int location = 0){
        
        // 记录时间和状态大小
        auto t1 = std::chrono::steady_clock::now();

        // 获取该 target_state 的 Nodemeta
        NodeMetadata d = readStateNodeMeta(target_state);
        auto _offset = d.m_offset; // 对应数据 偏移量
        auto len = d.getDataLength() + d.getMetaSize(); // 对应数据 总长度
        auto nodeId = d.getNodeNum();
        if(idx != nodeId){
            idx = nodeId;
        }
        cout << " Target State :" << target_state << " Node:" << idx << " Location:" << location << endl;

        // 记录该状态BMT的下标 
        int bmt_index;
        if(location == 0){
            auto state_location = mpt_ptr->stateHashToInfoMap[target_state];
            bmt_index = state_location.block_number;
        }
        else{
            bmt_index = location;
        }

        // 获取对应BMT指针，并且获得 target 状态的所有编码组（其顺序为从底层到根
        auto tree = mpt_ptr->BMT_map[bmt_index];
        cout<< tree.MerkleTrees.size() << " " << tree.state_cache.size() << endl;
        auto target = (tree.MerkleTrees[idx].root)->hash;
        cout << " Target Chunk :" << target <<endl;
        
        // std::cout<< tree.bmt_root <<std::endl;
        
        auto encoded_sets = tree.findAncestorsAndLeaves(target);

        std::unordered_map<dev::h256, std::string> chunks_pool;

        for(const auto& set: encoded_sets){
            auto ancestor = tree.search(set.first);
            std::cout << "ancestor hash :" << ancestor->_hash << std::endl;
            if(ancestor->p.empty()){
                std::cout << "This ancestor has no EC :" << std::endl;
                continue;
            }

            // if(test_coded == 0){
            //     test_coded = 1;
            //     continue;
            // }

            int cnt = 0;
            std::vector<std::string> raw_data;
            // 测试选项
            bool Is_Test_Coding = true; // 解码完成后仍要继续往根编码组恢复
            bool Is_Substr_Coding = true; // 是否将整个 chunk 切成 small_chunk 进行恢复

            for(const auto& _target: set.second){
                std::cout<<"---The Target of This round---\n" << _target <<std::endl;
                // 后期可以改成并行请求  
                // 之前已经读取过对应的 chunk
                if(chunks_pool.count(_target)) {
                    std::cout<<"The chunk is already in Pool" << std::endl;
                    auto ret = chunks_pool[_target];
                    
                    // 切成 小块
                    if(Is_Substr_Coding){
                        ret = getSubstring(ret, _offset, len);
                    }

                    raw_data.push_back(ret);
                    cnt++;
                }
                else{
                    std::string ret;
                    
                    if(_target != target){
                        
                        // 从本地磁盘或者其他节点获取
                        ret = readChunk(_target, bmt_index);
                        if(!ret.empty()){
                            std::cout<<"The Size of " << "dev::RLP(ret)" << " is " << ret.size() 
                                << ":" <<_target <<std::endl;
                            chunks_pool[_target] = ret;
                            cnt++;
                        }
                        // 切成 小块
                        if(Is_Substr_Coding){
                            ret = getSubstring(ret, _offset, len);
                        }
                        else
                            std::cout << "Not Found!" << std::endl;
                    }
                    raw_data.push_back(ret);
                }
                std::cout<<"-------------------------------" << std::endl;
            }
            // 插入冗余块，但是他不需要插入内存中
            for(const auto _p: ancestor->p){
                auto _ret = readChunk(_p, bmt_index);

                if(!_ret.empty()){
                    cnt++;
                    std::cout<<"encoded chunk insert!"<<std::endl;
                } 
                else{
                    std::cout << "Not Found!" << std::endl;
                }
                // 切成 小块
                if(Is_Substr_Coding){
                    _ret = getSubstring(_ret, _offset, len);
                }
                raw_data.push_back(_ret);
            }
            // 如果收到的 chunks 数量满足该编码组的恢复阈值（总数：set.second.size()， 容错：(ancestor->p).size()
            if(cnt >= raw_data.size() - ancestor->p.size()){
                // 开始针对编码组来构造编码结构（如数据所在的位置）
                std::cout << "It is ready to decoding!"<< std::endl;
                std::cout << "Raw_data lengh is "<< raw_data.size() << ", p number is " << ancestor->p.size() << std::endl;
                auto _str = mpt_ptr->state_erasure->decodeFromMPT(raw_data, ancestor->p.size());
                // cout << _offset << " " << d.getDataLength() << " " << _str.size() << endl;
                // cout << " Decode result :"<< RLP(_str.substr(_offset, d.getDataLength())) << endl;
                
                // 计算时间并输出日志
                auto t2 = std::chrono::steady_clock::now();
                auto dncoding_time = std::chrono::duration_cast<std::chrono::microseconds>(t2 - t1).count() / 1000.0;
                t2 = t1;
                auto logStr = "Decoding " + dev::toString(raw_data.size()-ancestor->p.size()) + "DC and " + dev::toString(ancestor->p.size()) + " PC, each " 
                    + printMemorySize(raw_data.back().size()) + ", costing " + dev::toString(dncoding_time) + "ms";
                writeToLog(logStr,"output_decode_log.txt");

                // 判断是否在testing; 若是，每一个编码组都会恢复一次
                if(Is_Test_Coding){
                    cout << "Repeating Decoding!" << endl;
                    raw_data.clear();
                    continue;
                }
                else{
                    break;
                }

            }
            else{
                raw_data.clear();
                std::cout << "No ready to decoding, turn to next round." << std::endl;
                writeToLog("No ready to decoding, turn to next round. " + toString(target),"output_decode_log.txt");
            }
        }
    }

    void recoverStateInParallel(dev::h256& target_state, int idx, int location = 0){
        
        // tbb::global_control control(tbb::global_control::max_allowed_parallelism, 12);
        // tbb::task_scheduler_init(6);

        // 记录时间和状态大小
        auto t1 = std::chrono::steady_clock::now();

        // 获取该 target_state 的 Nodemeta
        NodeMetadata d = readStateNodeMeta(target_state);

        auto t1_1 = std::chrono::steady_clock::now();
        
        auto _offset = d.m_offset; // 对应数据 偏移量
        auto len = d.getDataLength() + d.getMetaSize(); // 对应数据 总长度
        auto nodeId = d.getNodeNum();
        if(idx != nodeId){
            idx = nodeId;
        }

        // 测试选项
        bool Is_Test_Coding = false; // 解码完成后仍要继续往根编码组恢复
        bool Is_Substr_Coding = false; // 是否将整个 chunk 切成 small_chunk 进行恢复

        // 因为我们采用在前面的补0的策略，但是在实际中0依然消耗编码时间
        // 为了测试切小块后的效果，我们将其转化成仅仅测试 offset = 0 的状态，以达到相同效果
        if(Is_Substr_Coding){
            if(_offset != 0){
                return;
            }
        }

        // cout << " Target State :" << target_state << " Node:" << idx << " Location:" << location << endl;
        // cout << " offset :" << _offset << " len:" << len << endl;


        // 记录该状态BMT的下标 
        int bmt_index;
        if(location == 0){
            auto state_location = mpt_ptr->stateHashToInfoMap[target_state];
            bmt_index = state_location.block_number;
        }
        else{
            bmt_index = location;
        }

        auto t1_2 = std::chrono::steady_clock::now();

        // 获取对应BMT指针，并且获得 target 状态的所有编码组（其顺序为从底层到根
        auto tree = mpt_ptr->BMT_map[bmt_index];
        // cout<< " Tree size:" <<tree.MerkleTrees.size() << " " << tree.state_cache.size() << endl;
        auto target = (tree.MerkleTrees[idx].root)->hash;
        // cout << " Target Chunk :" << target <<endl;
        
        // std::cout<< tree.bmt_root <<std::endl;
        
        auto encoded_sets = tree.findAncestorsAndLeaves(target);

        std::unordered_map<dev::h256, std::string> chunks_pool;

        auto t1_3 = std::chrono::steady_clock::now();

        for(const auto& set: encoded_sets){
            auto ancestor = tree.search(set.first);
            // std::cout << "ancestor hash :" << ancestor->_hash << std::endl;
            if(ancestor->p.empty()){
                // std::cout << "This ancestor has no EC :" << std::endl;
                continue;
            }

            // if(test_coded == 0){
            //     test_coded = 1;
            //     continue;
            // }
            
            atomic<int> cnt(0);
            std::vector<std::string> raw_data(set.second.size() + ancestor->p.size());
            // cout<< "lengh dc:" << set.second.size() << "and and pc " <<  ancestor->p.size() << endl; 
            
            // NodeMetadata tmp = readStateNodeMeta(set.second[0]);
            auto e = set.second[0];
            auto nodeId_start = locationChunk(e, location);
            // cout<< "start id:" << nodeId_start << endl; 

            auto t1_4 = std::chrono::steady_clock::now();

            // 并行请求
            tbb::parallel_for(size_t(0), set.second.size() + ancestor->p.size(), [&](size_t i){
                
                // 插入冗余块，但是他不需要插入内存中
                if(i >= set.second.size()){
                    
                    auto _p = (ancestor->p)[i - set.second.size()];
                    auto _ret = readChunk(_p, bmt_index, nodeId_start + i);
                    // cout<< " 校验块大小 before" << _ret.size() <<endl;
                    if(!_ret.empty()){
                        cnt++;
                        // std::cout<<"encoded chunk insert!"<<std::endl;
                    } 
                    else{
                        // std::cout << "Not Found!" << std::endl;
                    }
                    // 切成 小块
                    if(Is_Substr_Coding){
                        _ret = getSubstring(_ret, _offset, len);
                        // cout << "After cutting len:" << _ret.size() << endl;
                    }
                    std::this_thread::sleep_for(std::chrono::microseconds(10000 * 2));
                    raw_data[i] = _ret;
                    // cout<< " 校验块大小 " << _ret.size() <<endl;
                }
                else{
                    auto _target = (set.second)[i];
                    // std::cout<<"---The Target of This round---" << _target <<std::endl;

                    // 之前已经读取过对应的 chunk
                    if(chunks_pool.find(_target) != chunks_pool.end()) {
                        // std::cout<<"The chunk is already in Pool" << std::endl;
                        auto ret = chunks_pool[_target];
                        
                        // 切成 小块
                        if(Is_Substr_Coding){
                            ret = getSubstring(ret, _offset, len);
                            // cout << "After cutting len:" << ret.size() << endl;
                        }

                        raw_data[i] = ret;
                        cnt++;
                    }
                    else{
                        std::string ret;
                        
                        if(_target != target){
                            
                            // 从本地磁盘或者其他节点获取
                            ret = readChunk(_target, bmt_index, nodeId_start + i);
                            if(!ret.empty()){
                                // std::cout<<"The Size of " << "dev::RLP(ret)" << " is " << ret.size() 
                                //     << ":" <<_target <<std::endl;
                                chunks_pool[_target] = ret;
                                cnt++;
                            }
                            // 切成 小块
                            if(Is_Substr_Coding){
                                ret = getSubstring(ret, _offset, len);
                                // cout << "After cutting len:" << ret.size() << endl;
                            }
                            else{
                                // std::cout << "Not Found!" << std::endl;
                            }
                        }
                        std::this_thread::sleep_for(std::chrono::microseconds(10000 * 2));
                        raw_data[i] = ret;
                    }
                }
                // std::cout<<"-------------------------------" << std::endl;
            });

            auto t1_5 = std::chrono::steady_clock::now();

            // 如果收到的 chunks 数量满足该编码组的恢复阈值（总数：set.second.size()， 容错：(ancestor->p).size()
            if(cnt >= raw_data.size() - ancestor->p.size()){
                // 开始针对编码组来构造编码结构（如数据所在的位置）
                // std::cout << "It is ready to decoding!"<< std::endl;
                // std::cout << "Raw_data lengh is "<< raw_data.size() << ", p number is " << ancestor->p.size() << std::endl;
                auto _str = mpt_ptr->state_erasure->decodeFromMPT(raw_data, ancestor->p.size(), idx);
                // cout << _offset << " " << d.getDataLength() << " " << _str.size() << endl;
                // cout << " Decode result :"<< RLP(_str.substr(_offset, d.getDataLength())) << endl;
                
                // 计算时间并输出日志
                auto t2 = std::chrono::steady_clock::now();
                auto handle_meta_time = std::chrono::duration_cast<std::chrono::microseconds>(t1_1 - t1).count() / 1000.0;
                auto handle_BMT_time = std::chrono::duration_cast<std::chrono::microseconds>(t1_2 - t1_1).count() / 1000.0;
                auto handle_time = std::chrono::duration_cast<std::chrono::microseconds>(t1_3 - t1_2).count() / 1000.0;
                auto collect_chunk_time = std::chrono::duration_cast<std::chrono::microseconds>(t1_5 - t1_3).count() / 1000.0;
                auto dncoding_round_time = std::chrono::duration_cast<std::chrono::microseconds>(t2 - t1_4).count() / 1000.0;
                auto dncoding_time = std::chrono::duration_cast<std::chrono::microseconds>(t2 - t1).count() / 1000.0;
                t2 = t1;
                auto logStr = "Decoding " + dev::toString(raw_data.size()-ancestor->p.size()) + "DC and " + dev::toString(ancestor->p.size()) + " PC, each " 
                    + printMemorySize(raw_data.back().size()) + ", costing " + dev::toString(dncoding_time) + "/(" 
                    + dev::toString(handle_meta_time) + " + " + dev::toString(handle_BMT_time) + " + " + dev::toString(handle_time) + " + " 
                    + dev::toString(collect_chunk_time) + ") ms " + "Round:" + dev::toString(dncoding_round_time) + "ms";
                writeToLog(logStr,"output_decode_log.txt");
                auto logName = "output_decode_log" +  dev::toString(ancestor->p.size()) + ".txt";
                writeToLog(logStr, logName);

                // 判断是否在testing; 若是，每一个编码组都会恢复一次
                if(Is_Test_Coding){
                    // cout << "Repeating Decoding!" << endl;
                    raw_data.clear();
                    continue;
                }
                else{
                    break;
                }

            }
            else{
                raw_data.clear();
                malicious_nodes = 0;
                // std::cout << "No ready to decoding, turn to next round." << std::endl;
                // writeToLog("No ready to decoding, turn to next round. " + toString(target),"output_decode_log.txt");
            }
        }
    }
};