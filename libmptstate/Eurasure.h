#pragma once
// #include "VCGroup.h"
#include <erasure-codes/liberasure.h>
// #include <libblockchain/BlockChainInterface.h>
// #include <libbloomfilter/bf/all.hpp>
#include <libdevcore/Common.h>
#include <libdevcore/FixedHash.h>
// #include <libethcore/Block.h>
// #include <libnetwork/Common.h>
// #include <libp2p/P2PInterface.h>

#include <algorithm>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

#include "rocksdb/db.h"
#include "rocksdb/options.h"
#include "rocksdb/slice.h"
#include "BMT.h"

#define BLOCKS_SIZE_BYTE 3 //默认记录区块大小的字节数
#define blockchainManager std::shared_ptr<dev::blockchain::BlockChainInterface>
#define StatePoint std::pair<std::string, std::string>
#define NodeAddr dev::h512
namespace ec {

class Eurasure : public std::enable_shared_from_this<Eurasure> {
  public:
    // typedef std::shared_ptr<Eurasure> Ptr;
    Eurasure(
        int64_t k, int64_t m, int64_t c, NodeAddr const &nodeid,
        std::vector<NodeAddr> sealers, std::string path, int _group_size)
        : ec_k(k), ec_m(m), ec_c(c), ec_nodeid(nodeid), ec_sealers(sealers),
          ec_DBPath(path), group_len(_group_size) {}

    Eurasure(){
      // 测试
    }
    //初始化EC模块
    bool InitEurasure();

    //生成Merkle root
    // h256 constructMerkleRoot();

    //并行ec线程
    void thread_ec_task(std::pair<uint8_t *, int64_t> state_bytes_data);

    //编解码模块

    std::pair<uint8_t *, int64_t>
    preprocess(int block_number, int groupid,
               std::map<int, std::map<int, std::string>> &states);
    std::pair<uint8_t *, int64_t>
    preprocess_2D(int block_number, int groupid,
               std::map<int, std::map<int, std::string>> &states);
    // std::pair<uint8_t*, int64_t> preprocess(
    //         int block_number, int groupid, std::map<int, std::string> const&
    //         states);

    std::pair<uint8_t **, int64_t>
    encode(std::pair<uint8_t *, int64_t> blocks_rlp_data);
    std::string decode(unsigned int coding_epoch, std::string key);
    std::string decode_2D(unsigned int coding_epoch, std::string key);
    std::string decode_2D_columns(unsigned int coding_epoch, std::string key);

    bool checkState(std::string const &key, std::string const &data,
                    std::string const &maincommit,
                    std::string const &maincommit_proof, int main_position,
                    std::string const &subcommit,
                    std::string const &subcommit_proof, int sub_position,
                    std::string &value);
    bool checkChunk();
    void generate_ptrs(size_t data_size, uint8_t *data, erasure_bool *present,
                       uint8_t **ptrs);
    std::string GetChunkDataKey(unsigned int coding_epoch, unsigned group_id,
                                unsigned chunk_pos);
    //写chunk 模块
    void saveChunk(std::map<int, std::map<int, std::string>> &states,
                   int block_number, int groupid,
                   std::map<int, std::map<int, std::string>> &cs);
    void saveChunk_2D(std::map<int, std::map<int, std::string>> &states,
                   int block_number, int groupid,
                   std::map<int, std::map<int, std::string>> &cs);
    bool writeDB(unsigned int coding_epoch, unsigned int groupid,
                 std::pair<uint8_t **, int64_t> const &chunk);
    bool writeDB_columns(unsigned int coding_epoch, unsigned int groupid,
                 std::pair<uint8_t **, int64_t> const &chunk);

    std::string getKVAndProof(std::string &data, std::string const &key,
                              int block_number);
    std::string
    constructMerkleRoot(int pos, int group_num, int block_number,
                        std::map<int, std::map<int, std::string>> &data);
    void makeMerkleRoot(int block_number,
                        std::map<int, std::map<int, std::string>> &data);
    void makeMerkleTree(int block_number,
                        std::map<int, std::map<int, std::string>> &data);
    // bool verifyChunkMerkleRoot(
    //     std::map<int, std::map<int, std::string>>& level_to_chunk, int
    //     block_number, int chunk_pos);
    bool verifyChunkMerkleRoot(std::vector<std::string> &level_to_chunk,
                               int block_number, int chunk_pos, int group_id);
    void convertVectorToMap(
        std::vector<std::string> input,
        std::map<int, std::map<int, std::string>> &level_to_chunk);
    void convertChunkToMap(std::string chunkdata,
                           std::map<std::string, std::string> &kvmap);
    //读chunk模块
    // void readChunkFromDB(int block_number, int group_num, int pos,
    // std::queue<std::string>& q);
    void readChunkFromDB(int block_number, int group_num, int pos,
                         std::vector<std::string> &v);
    void readChunk(unsigned int coding_epoch, std::string key,
                   std::string &out);              
    void readChunk(unsigned int coding_epoch, unsigned group_id,
                   unsigned chunk_pos, std::string &out);
    std::vector<std::string> readChunkAndComputeMerkleHashs(int block_number,
                                                            int chunk_pos,
                                                            int group_id);
    void findKeyInWhichChunk(int const &block_number, std::string const &key,
                             int &group, int &chunk_num);
    void findKeyInWhichChunk_2D(int const &block_number, std::string const &key,
                             int &group, int &chunk_num);
    std::string readOneChunkFromDB(int block_number, int group_num, int pos);
    //读状态模块
    std::string getState(unsigned int block_num, std::string key);
    std::string localReadState(unsigned int block_num, unsigned int group_id,
                               unsigned int chunk_pos, std::string key);
    std::string remoteReadState(unsigned int block_num, std::string key,
                                NodeAddr const &target_nodeid);

    //计算chunk_set模块
    std::pair<int *, int *> get_my_chunk_set(unsigned int groupid);
    int *get_distinct_chunk_set(unsigned int coding_epoch);
    void maxLen(std::map<int, std::map<int, std::string>> &states,
                int64_t group_id, std::map<int, std::string> &processed_data);
    // 处理二维EC的状态数据
    void maxLen_2D(std::map<int, std::map<int, std::string>> &states, int64_t group_id, std::map<int, std::string> &processed_data);

    void initTestStates(unsigned int block_number);

    void makeEC(int block_number, int thread_number);
    // multi-Ec的队列 
    std::vector<int> block_numbers;
    // 状态数量
    int state_number = 0;
    // 状态数量阈值
    int max_state_number = 1500000;
    // 需要查找账户地址(decode需要)
    std::string _search;
    void makeMultiEC(int block_number, int thread_number);
    void makeECFromKV(int block_number, int _kv_number);
    
    /**
    * 渐进式编码
    * 
    * @author qqf
    * @date 2024/10/30
    */
    std::unordered_map<dev::h256, std::string> makeECFromMPT(int block_number, dev::BMT& bmt, int fault_tolerance, int encoding_level);
    std::unordered_map<dev::h256, std::string> saveChunkFromMPT(std::vector<std::pair<dev::h256, std::string>>& leaves, dev::BMT& bmt, std::shared_ptr<dev::Node>& node);
    std::pair<uint8_t *, int64_t> preprocessFromMPT(std::vector<std::pair<dev::h256, std::string>>& leaves);
    size_t maxLenFromMPT(const std::vector<std::pair<dev::h256, std::string>>& leaves, std::vector<std::string>& processed_data);
    std::pair<uint8_t **, int64_t> encodeFromMPT(std::pair<uint8_t *, int64_t> blocks_rlp_data);
    std::string decodeFromMPT(std::pair<uint8_t**, int64_t> test_data);
    std::string decodeFromMPT(std::vector<std::string>, int p_number, int lost_node = -1);
    bool writeDBFromMPT(unsigned int coding_epoch, std::pair<uint8_t **, int64_t> const &chunks);
    void generatePtrsWithPara(size_t data_size, uint8_t* data, erasure_bool* present, uint8_t** ptrs, int k, int m);
    void readChunkFromMPT(unsigned int coding_epoch, unsigned group_id, unsigned chunk_pos, std::string &out);

    // void getHasherFromDB(bf::hasher &h);
    bool initVC();

    void statistic();
    int64_t findSeqInSealers();
    void getVCCommit(int block_number, int pos, std::string &output);
    erasure_encoder_flags getecmode(){ return ec_mode; }

    int64_t getK() { return ec_k; }
    int64_t getM() { return ec_m; }
    int64_t getC() { return ec_c; }
    void setK(int64_t n) { ec_k = n; }
    void setM(int64_t n) { ec_m = n; }
    void setC(int64_t n) { ec_c = n; }
    int getCurrentBlockNum() { return ec_current_block_num; }
    void setDBHandler(rocksdb::DB *_db) { ec_db = _db; }
    rocksdb::DB *getDBHandler() { return ec_db; }
    unsigned int getPosition() { return ec_position_in_sealers; }
    long getStorageSize() { return ec_storage_size; }
    int64_t getCompleteEpoch() { return complete_coding_epoch; }
    int getGroupLen() { return group_len; }
    void setCompleteCodingEpoch(int epoch) { complete_coding_epoch = epoch; }
    void setCurrentBlockNum(int current_block_num) {
        ec_current_block_num = current_block_num;
    }
    int getECGroupNum() { return ec_group_num; }
    int getInitVCSize() { return init_size; }
    int getNumberOfVCInOneChunk() { return number_of_vc_in_one_chunk; }
    // ec::EurasureP2P *getP2PHandle() { return ec_eurasure_p2p; }
    // blockchainManager getBlockchain() { return ec_blockchain; }
    void setVCDB(rocksdb::DB& _db) { ec_db = &_db; }
    int getRandCount() { return randcount; }
    bool accRandCount() {
        ++randcount;
        return true;
    }
    int tmp_states_size = 0;
    void mpt_test();
    // qqf 建立MPTState中state与Eurasure的state之间的联系
    void setStateStorage(std::unordered_map<int, dev::StringMap> _state_storage){state_storage = _state_storage; }
  private:
    erasure_encoder_flags ec_mode = ERASURE_FORCE_ADV_IMPL; // ec编码模式
    int64_t ec_k;                                           //数据块个数
    int64_t ec_m;                                           //校验块个数
    int64_t ec_c;                                           //副本个数
    NodeAddr ec_nodeid;                                     //节点ID
    std::vector<NodeAddr> ec_sealers;                       //所有节点地址
    unsigned int ec_position_in_sealers; //本节点在所有节点中的相对位置
    rocksdb::DB *ec_db = NULL;           // DB句柄
    rocksdb::DB *vc_db;                  // DB句柄
    std::string ec_DBPath;               // DB路径
    long ec_storage_size = 0;            // EC存储开销
    // ec::EurasureP2P *ec_eurasure_p2p;   // EC网络模块句柄
    int64_t complete_coding_epoch = -1; //已完成ECepoch
    int ec_current_block_num = 0;
    // blockchainManager ec_blockchain; //区块链句柄
    int group_len = 64;
    int number_of_vc_in_one_chunk = 1;
    const int ec_sub_commit_size = 32;
    const int ec_byte_alignment = 64;
    const int ec_group_num = 64;
    const int partition_key_mappinto_vc_bytes_num = 1;
    const int init_size = 65;
    const int proof_and_commit_len = 49;
    const int one_state_with_kv_size = 36;
    const int max_state_size = 4000;
    const double false_positive_rate = 0.01;
    int data_chunk_size = 0;
    int parity_chunk_size = 0;

    int randcount = 1;
    // bf::hasher hashers;
    std::unordered_map<int, dev::StringMap> state_storage;
    
};
} // namespace ec