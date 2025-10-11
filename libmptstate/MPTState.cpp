/*
    @CopyRight:
    This file is part of FISCO-BCOS.

    FISCO-BCOS is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    FISCO-BCOS is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with FISCO-BCOS.  If not, see <http://www.gnu.org/licenses/>.
*/
/**
 * @Mpt state interface for EVM
 *
 * @file MPTState.cpp
 * @author jimmyshi
 * @date 2018-09-21
 */

#include "MPTState.h"
#include <string>
#include <unordered_map>
#include <libdevcrypto/Hash.h>

#include <boost/algorithm/string/classification.hpp>
#include <boost/algorithm/string/split.hpp>
#include <boost/archive/binary_iarchive.hpp>
#include <boost/archive/binary_oarchive.hpp>
#include <boost/serialization/vector.hpp>


// #include "VCGroup.h"
// write state format :<block_number + "+" + key, value>
// write chunk format :<block_number + "|" + group_id + '|' + chunk_pos, chunk data>
// write chunk merkleroot format : <block_number + "-" + column_number, root>
// write vccommit format: {
//      maincommit : block_number + "_commit"
//      subcommit  : block_number + "_subcommit" + subcommit_pos
//}
// write bloom filter bitvector format : <block_number+"bf",bitvector>
// write bloom filter hasher format : <"bf_hasher", hasher>

ec::Eurasure* dev::mptstate::MPTState::state_erasure = NULL;

// 辅助函数：将 v1 划分为多个组
std::vector<std::vector<dev::h256>> splitGroups(const std::vector<dev::h256>& v1) {
    std::vector<std::vector<dev::h256>> groups;
    std::vector<dev::h256> currentGroup;
    dev::h256 sign;
    for (auto elem : v1) {
        if (elem == sign) {
            if (!currentGroup.empty()) {
                groups.push_back(currentGroup);  // 存储当前组
                currentGroup.clear();
            }
        } else {
            currentGroup.push_back(elem);
        }
    }

    // 处理最后一组（如果存在）
    if (!currentGroup.empty()) {
        groups.push_back(currentGroup);
    }

    return groups;
}

// 主函数：划分元素为 n 组
std::vector<std::vector<dev::h256>> partitionElements( const std::vector<dev::h256>& v1, const std::vector<dev::h256>& v2, int n) {
    
    // 1. 将 v1 划分成多个组
    std::vector<std::vector<dev::h256>> groups = splitGroups(v1);

    // 2. 将所有组的长度与 v2 的元素加入总列表
    std::vector<int> groupSizes;  // 存储所有组的大小
    for (const auto& group : groups) {
        groupSizes.push_back(group.size());
    }

    // 统计总元素数
    int totalElements = std::accumulate(groupSizes.begin(), groupSizes.end(), 0) + v2.size();

    // 3. 计算每组的目标大小
    int targetSize = (totalElements + n - 1) / n;  // 向上取整

    // 4. 创建结果容器
    std::vector<std::vector<dev::h256>> result(n);

    // 5. 贪心策略：将组尽量均匀地分配
    int currentGroup = 0;
    for (const auto& group : groups) {
        if (result[currentGroup].size() + group.size() <= targetSize) {
            result[currentGroup].insert(result[currentGroup].end(), group.begin(), group.end());
        } 
        else {
            // 如果当前组放不下，则移动到下一组
            currentGroup = (currentGroup + 1) % n;
            result[currentGroup].insert(result[currentGroup].end(), group.begin(), group.end());
        }
    }

    // 6. 将 v2 中的元素均匀分配
    for (auto elem : v2) {
        result[currentGroup].push_back(elem);
        currentGroup = (currentGroup + 1) % n;
    }
    return result;
}

static std::string DecIntToHexStr(dev::u256 const& num)
{
    std::string str;
    dev::u256 Temp = num / 16;
    dev::u256 left = num % 16;
    if (Temp > 0)
        str.append(DecIntToHexStr(Temp));
    if (left < 10)
        str += ((char)left + '0');
    else
        str += ('A' + (char)left - 10);
    return str;
}

static inline std::string str2fixstr(std::string str)
{
    int len = str.length();
    if (len < 4)
    {
        std::string tmp(4 - len, ' ');
        tmp.append(str);
        return tmp;
    }
    return str.substr(len - 4, 4);
}
static inline std::string num2str(int i)
{
    char ss[10];
    sprintf(ss, "%04d", i);
    return ss;
}
// static inline std::string DecToHex(unsigned int num)
// {
//     std::ostringstream buffer;
//     buffer << std::hex << std::uppercase << num;
//     return buffer.str();
// }
static inline int computePostion(const std::string& posStr, int mod)
{
    long res = 0;
    for (int i = 0; i < 4; ++i)
    {
        // res += data[i] * 2;
        if (posStr[i] >= 'A' && posStr[i] <= 'Z')
        {
            res = (res * 16 + (posStr[i] - 'A') + 10) % mod;
        }
        if (posStr[i] >= 'a' && posStr[i] <= 'z')
        {
            res = (res * 16 + (posStr[i] - 'a') + 10) % mod;
        }
        if (posStr[i] >= '0' && posStr[i] <= '9')
        {
            res = (res * 16 + (posStr[i] - '0')) % mod;
        }
    }
    return res % mod;
}
namespace dev
{
namespace mptstate
{
void MPTState::makeEC(int block_number, int thread_number)
{
    std::map<int, std::map<int, std::string>> position_mapinto_accounts;
    // std::cout << "state_storage[block_number].size() = " << state_storage[block_number].size()
    //           << std::endl;
    int count = 0;
    for (auto it = state_storage[block_number].begin(); it != state_storage[block_number].end();
         ++it)
    {
        int pos = computePostion(it->first, state_erasure->getECGroupNum());
        // std::cout << "pos = " << pos << std::endl;
        int sub_pos = computePostion(it->first, state_erasure->getInitVCSize());
        std::string keys = it->first;
        // std::cout << "it->first = " << it->first << std::endl;
        // if (it->first == "E0C194580A962A5A5914EF13D81811AF")
        // {
        //     // std::string values = num2str(stoi(it->second));
        //     std::cout << "pos = " << pos << std::endl;
        //     std::cout << "sub pos = " << sub_pos << std::endl;
        //     std::cout << "key = " << keys << " value = " << num2str(stoi(it->second))<<
        //     std::endl;
        // }

        if (position_mapinto_accounts.count(pos) != 0)
        {
            if (position_mapinto_accounts[pos].count(sub_pos) != 0)
            {
                position_mapinto_accounts[pos][sub_pos] =
                    position_mapinto_accounts[pos][sub_pos].append(keys.append(it->second));
            }
            else
            {
                position_mapinto_accounts[pos].insert(make_pair(sub_pos, keys.append(it->second)));
            }
        }
        else
        {
            std::map<int, std::string> ms;
            std::string keys = it->first;
            ms.insert(make_pair(sub_pos, keys.append(it->second)));
            position_mapinto_accounts.insert(make_pair(pos, ms));
        }
        ++count;
    }
    // std::cout << "count = " << count << std::endl;
    std::map<int, std::map<int, std::string>> chunks;
    int size = position_mapinto_accounts.size();
    // for (auto it = position_mapinto_accounts.begin(); it != position_mapinto_accounts.end();
    // ++it)
    // {
    //     std::cout << "group " << it->first << " size = " << it->second.size() << std::endl;
    // }
    // std::cout << "start ec" << std::endl;
    // std::cout << "size = " << size << std::endl;
    // tbb::parallel_for(tbb::blocked_range<int>(0, size,
    //                       state_erasure->getNumberOfVCInOneChunk() * state_erasure->getK()),
    // [&](const tbb::blocked_range<int>& _r) {
    // std::cout<< "start pos = " << _r.begin() << std::endl;
    for (int i = 0; i < size; i += state_erasure->getNumberOfVCInOneChunk() * state_erasure->getK())
    {
        state_erasure->saveChunk(position_mapinto_accounts, block_number, i, chunks);
        // state_erasure->saveChunk_2D(position_mapinto_accounts, block_number, i, chunks);
    }

    // });
    // std::cout << "start make root" << std::endl;
    state_erasure->makeMerkleRoot(block_number, chunks);
    state_erasure->setCompleteCodingEpoch(block_number);
    chunks.clear();
    position_mapinto_accounts.clear();
    std::cout << "finish ec " << std::endl;
}

/**
* @brief 针对MPT验证结构和状态数据进行编码
* 
* 
* @param block_number 区块编号
* @param node_number 节点个数
* @return totalEncodedData 本轮中编码的结果（以<h256, string>存储）
* @return partition_result 本轮状态划分的结果 n行为第n个节点的节点集合，每一列为chunk Merkle root
*/
std::unordered_map<h256, std::string> MPTState::makeECFromMPT(int block_number, std::vector<int> config){
    
    // 容错设置
    int node_number = config[0];
    int fault_tolerance = config[1];
    int encoding_level = config[2];
    
    /* 2024/10/23 状态编码*/
    auto mut_map = getState().db().get();
    auto mut_set = getState().db().keys();
    for(auto it = mut_map.begin(); it != mut_map.end();){
        if(mut_set.find(it->first) == mut_set.end()){
            it = mut_map.erase(it);
        }
        else{
            ++it;
        }
    }

    auto t1 = std::chrono::steady_clock::now();

    std::cout << " The Map Get From OverlayDB is : \n";
    versionManager.initDB(getState().db());
    versionManager.processBatch(mut_map);
    versionManager.setVersion(block_number);
    // versionManager.printManager();

    StatePartition sp;
    
    auto t2 = std::chrono::steady_clock::now();

    sp.processBatch(mut_map);
    // 节点个数 nodes number
    sp.init(node_number);
    auto _r = getState().rootHash();
    cout << " (0v0)~~MPTRoot : " << _r << endl;

    u_int expression = 1; // which partition mode we choose
    switch (expression){
        case 1:
            sp.partitionMPT(_r); // 我们的方法 
            break;
        case 2:
            sp.partitionMPTWithBaseline(_r); // Baseline 按照mod随机划分
            break;
        case 3:
            sp.partitionMPTWithDHT(_r); // distributed by DHT
            break;
        case 4:
            sp.partitionMPTWithLocal(_r); // local read
            break;
        default:
            break;
    }
    versionManager.setStatePartition(sp);

    auto t3 = std::chrono::steady_clock::now();

    ChunkBuilder cb(versionManager);
    cb.setDB(*ec_db); // binding the DB
    cb.setStateDB(getState().db()); // binding the State DB
    // cb.setDataSet(versionManager.dataSet);
    
    // cb.initPartitions(sp.getPartitionMapResult());
    cb.processBatch(mut_map, versionManager.m_nodeVersions);
    vector<string> chunksRlt = cb.handleDataSet(sp.getPartitionMapResult(), sp.m_groups);
    
    // vector<string> chunksRlt = cb.handleDataSetWithReadyQueue(sp.m_groups);
        
    // cb.printDataSet();

    // 计算以下各个的时间 怎么那么慢
    auto t4 = std::chrono::steady_clock::now();
    auto VM_time = std::chrono::duration_cast<std::chrono::microseconds>(t2 - t1).count() / 1000.0;
    auto SP_time = std::chrono::duration_cast<std::chrono::microseconds>(t3 - t2).count() / 1000.0;
    auto CB_time = std::chrono::duration_cast<std::chrono::microseconds>(t4 - t3).count() / 1000.0;
    auto logStr = "VM time: " + dev::toString(VM_time) + "ms. "
        + "SP time: " + dev::toString(SP_time) + "ms. "
        + "CB time:" + dev::toString(CB_time);
    writeToLog(logStr,"time_log.txt");

    // for(const auto& i: mut_map){
    //     std::cout << " H256: " << i.first << " Value:" << dev::RLP(i.second) << std::endl;
    // }

    // auto bmt = BMT(mut_map); // 根据 状态数据 生成树
    auto bmt = BMT(chunksRlt); // 根据 状态数据集成的chunk 生成树
    

    // 2. 编码阶段
    auto totalEncodedData = state_erasure->makeECFromMPT(block_number, bmt, fault_tolerance, encoding_level);
    BMT_map.emplace(block_number, bmt);
    vector<h256> VCgroup;
    string encoding_group; // 所有编码组的排列 e.g. [016][012345][237]
    auto edges = bmt.buildIndexFromLeaves(VCgroup, encoding_group);
    persist_edges(*ec_db, edges, VCgroup, block_number);
    persist_encoding_group(*ec_db, encoding_group, block_number);
    VCTemplate _vc("vector commitment", VCgroup.size(), VCgroup); // generate vc
    
    cb.StorageForChunks(chunksRlt, totalEncodedData, t_state_size, t_extraInfo_size, t_encoded_size); // 计算存储开销
    
    block_height = block_number;
    /*
    // 3. 状态划分 
    std::vector<h256> data_set;
    m_state.get_m_state().leftOvers(data_set);
    std::vector<h256> encoded_set;
    for(const auto& data: totalEncodedData){
        encoded_set.push_back(data.first);
    }
    // 节点数量
    // int node_number = 3;
    auto partition_result = partitionElements(data_set, encoded_set, node_number);
    // 测试是怎么划分的
    auto node_index = 0;
    for(const auto& i : partition_result){
        // std::cout << "[";
        for(const auto& j : i){
            stateHashToInfoMap[j] = StateLocation(dev::toString(node_index), block_number);;
            // std::cout << j << " | "; 
        }
        // std::cout << "]" << std::endl;
        ++node_index;
    }
    */
    return totalEncodedData;

    /* 2024/10/23 状态编码 end */
}

OverlayDB MPTState::openDB(
    boost::filesystem::path const& _path, h256 const& _genesisHash, WithExisting _we)
{
    // initEC();

    std::cout << "init DB success" << std::endl;
    return State::openDB(_path, _genesisHash, _we);
}

bool MPTState::addressInUse(Address const& _address) const
{
    return m_state.addressInUse(_address);
}

bool MPTState::accountNonemptyAndExisting(Address const& _address) const
{
    return m_state.accountNonemptyAndExisting(_address);
}

bool MPTState::addressHasCode(Address const& _address) const
{
    return m_state.addressHasCode(_address);
}

u256 MPTState::balance(Address const& _id) const
{
    return m_state.balance(_id);
}

void MPTState::addBalance(Address const& _id, u256 const& _amount)
{
    m_state.addBalance(_id, _amount);
}

void MPTState::subBalance(Address const& _addr, u256 const& _value)
{
    m_state.subBalance(_addr, _value);
}

void MPTState::setBalance(Address const& _addr, u256 const& _value)
{
    m_state.setBalance(_addr, _value);
}

void MPTState::transferBalance(Address const& _from, Address const& _to, u256 const& _value)
{
    m_state.transferBalance(_from, _to, _value);
}

h256 MPTState::storageRoot(Address const& _contract) const
{
    return m_state.storageRoot(_contract);
}

u256 MPTState::storage(Address const& _contract, u256 const& _memory)
{
    // std::cout<< "read state_storage["<< block_number <<"] size:"<< state_storage[block_number].size()<< std::endl;
    // state_storage[block_number][DecIntToHexStr(_memory).substr(0, 32)];
    // std::cout<<"read item:"<<DecIntToHexStr(_memory).substr(0, 32)
    //     <<"  data="<<state_storage[block_number][DecIntToHexStr(_memory).substr(0, 32)]<<std::endl;
    return m_state.storage(_contract, _memory);
}

void MPTState::setStorage(Address const& _contract, u256 const& _location, u256 const& _value)
{
    // 在交易写入数据时，记录其kv，并用kv集生成VCtree
    // std::cout<< toString(_value).size() << "   "<< str2fixstr(toString(_value)).size()<<
    // std::endl;
    // std::cout << "toHex(toString(_location)).substr(0, 32) = " <<
    // toHex(toString(_location)).substr(0, 32) << std::endl;
    std::hash<unsigned int> block_hash;
    unsigned int seed = block_hash(state_erasure->getRandCount());
    std::mt19937 rand_num(seed);
    std::string key = dev::sha256(boost::lexical_cast<std::string>(rand_num())).hex();
    // std::cout<< "state_storage size:"<< state_storage.size()<< std::endl;
    if (state_storage.find(block_number) == state_storage.end())
    {
        dev::StringMap key_to_value;

        // key_to_value[DecIntToHexStr(_location).substr(0, 32)] = str2fixstr(toString(_value));
        // key_to_value[DecIntToHexStr(_location).substr(0, 32)] =
        //     std::to_string(1);
        // key_to_value[key.substr(0, 32)] = str2fixstr(toString(_value));
        key_to_value[DecIntToHexStr(_location).substr(0, 32)] = str2fixstr(toString(_value));
        std::cout<<"New Key :"<< DecIntToHexStr(_location).substr(0, 32) 
            << "  Value: " << str2fixstr(toString(_value)) << std::endl;
        state_storage[block_number] = key_to_value;
    }
    else
    {
        // state_storage[block_number][DecIntToHexStr(_location).substr(0, 32)] =
        //     str2fixstr(toString(_value));
        // std::cout<<"Key :"<< key.substr(0, 32) << "  Value: " << str2fixstr(toString(_value)) << std::endl;
        // std::cout<< "state_storage["<< block_number <<"] size:"<< state_storage[block_number].size()<< std::endl;
        // state_storage[block_number][key.substr(0, 32)] = std::to_string(1);
        state_storage[block_number][DecIntToHexStr(_location).substr(0, 32)] = str2fixstr(toString(_value));
        // std::cout<<"write item:"<<DecIntToHexStr(_location).substr(0, 32)
        // <<"  data="<<state_storage[block_number][DecIntToHexStr(_location).substr(0, 32)]<<std::endl;
    }
    m_state.setStorage(_contract, _location, _value);
}

void MPTState::clearStorage(Address const& _contract)
{
    m_state.clearStorage(_contract);
}

void MPTState::createContract(Address const& _address)
{
    m_state.createContract(_address);
}

void MPTState::setCode(Address const& _address, bytes&& _code, u256 const& _version)
{
    m_state.setCode(_address, std::move(_code), _version);
}

void MPTState::kill(Address _a)
{
    m_state.kill(_a);
}

bytes const MPTState::code(Address const& _addr) const
{
    return m_state.code(_addr);
}

h256 MPTState::codeHash(Address const& _contract) const
{
    return m_state.codeHash(_contract);
}

bool MPTState::frozen(Address const& _contract) const
{
    (void)_contract;
    return false;
}

size_t MPTState::codeSize(Address const& _contract) const
{
    return m_state.codeSize(_contract);
}

void MPTState::incNonce(Address const& _id)
{
    m_state.incNonce(_id);
}

void MPTState::setNonce(Address const& _addr, u256 const& _newNonce)
{
    m_state.setNonce(_addr, _newNonce);
}

u256 MPTState::getNonce(Address const& _addr) const
{
    return m_state.getNonce(_addr);
}

h256 MPTState::rootHash(bool) const
{
    return m_state.rootHash();
}

void MPTState::commit()
{
    m_state.commit(dev::eth::State::CommitBehaviour::KeepEmptyAccounts);
}
// void MPTState::getHasherFromDB(bf::hasher& h)
// {
//     std::string hashers = "";
//     state_erasure->getDBHandler()->Get(rocksdb::ReadOptions(), "bf_hasher", &hashers);
//     std::stringstream ifs(hashers);
//     boost::archive::binary_iarchive ia(ifs);
//     ia >> h;
//     ifs.str("");
//     ifs.clear();
// }

bool MPTState::initVC(string baseDir)
{
    namespace fs = boost::filesystem;
    fs::path root = baseDir.empty() ? fs::path("./") : fs::path(std::string(baseDir));
    fs::path dir  = (root.filename() == "vcstorage") ? root : (root / "vcstorage");

    // 静默创建目录（已存在则无事发生）
    boost::system::error_code ec;
    fs::create_directories(dir, ec);

    rocksdb::Options options;
    options.create_if_missing = true;
    rocksdb::DB* raw;
    rocksdb::Status status = rocksdb::DB::Open(options, dir.string(), &raw);
    assert(status.ok());
    // VCGroup::initTemplate(vc_db, subcommit_num, init_vc_size);
    // bf::basic_bloom_filter bloom_filter(false_positive_rate, max_state_size);
    // hashers = bloom_filter.hasher_function();
    // std::stringstream ofs;
    // boost::archive::binary_oarchive oa(ofs);
    // oa << hashers;
    // state_erasure->getDBHandler()->Put(rocksdb::WriteOptions(), "bf_hasher", ofs.str());
    // ofs.str("");
    // ofs.clear();
    ec_db.reset(raw);
    state_erasure->setVCDB(*ec_db);
    std::cout << "init ec in MPTstate finished" << std::endl;
    return true;
}

void MPTState::dbCommit(h256 const&, int64_t)
{
    /*
    std::cout << "MPTState::dbCommit"
              << "   block number = " << block_number << std::endl;
    if (block_number >= 3)
    {
    //     VCGroup* vcGroup = new VCGroup(block_number);
        if (hashers.get_function_num() == 0)
        {
            getHasherFromDB(hashers);
        }
        bf::basic_bloom_filter bf(hashers, max_state_size);
        std::string tmp;
        for (auto it = state_storage[block_number].begin(); it != state_storage[block_number].end();it++){
            bf.add(it->first.substr(0, 32));
            // tmp = it->first.substr(0, 32);
        }
        // 测试利用hasher和bitvector生成bf是否与原过滤器一致，其partition存在差异，一个为true一个为false
        // auto before_str = to_string(bf.storage());
        // std::cout<<"Answer is " << bf.lookup(tmp) <<std::endl;
        // bf::basic_bloom_filter _bf1(hashers, bf.storage());
        // auto after_str = to_string(_bf1.storage());
        // std::cout<<"Answe2 is " << _bf1.lookup(tmp) <<std::endl;
        // if(before_str == after_str){
        //     std::cout<<"Two storage is same"<<std::endl;
        // }
        // else{
        //     std::cout<<"Two storage is 'not' same"<<std::endl;
        // }

        // 序列化
        std::stringstream ofs;
        boost::archive::binary_oarchive oa(ofs);
        oa << bf.storage();
        // std::cout << "bf bitvector " << DecToHex(block_number).append("bf") << std::endl;
        // std::cout << "write bf_bit_vector = " <<  ofs.str() << std::endl;
        state_erasure->getDBHandler()->Put(
            rocksdb::WriteOptions(), DecToHex(block_number).append("bf"), ofs.str());
        // 反序列化
        // auto ans = bf.storage();
        // std::string contents = ofs.str();
        // std::stringstream is(contents);
        // boost::archive::binary_iarchive ia(is);
        // ia >> ans;
        // // std::cout <<"序列化: "<< bf.storage().str() << std::endl;
        // // std::cout <<"反序列化: "<< ans << std::endl;
        // if(ans == bf.storage()){
        //     bf::basic_bloom_filter _bf(hashers, ans);
        //     auto is_find = _bf.lookup("AD692F687445A9744D58B6CB820FFC28");
        //     //"AD692F687445A9744D58B6CB820FFC28"
        //     std::cout<<"Same and answer is "<< is_find <<std::endl;
        // }
        // else{
        //     std::cout<<"No same"<<std::endl;
        // }
        
        ofs.str("");
        ofs.clear();
    //     vcGroup->commitBlock(state_storage[block_number]);
    //     // std::cout << "vcGroup->commitBlock" << std::endl;
    //     // makeEC(block_number, 4);
    //     // std::cout << "makeEC" << std::endl;
    //     // tbb::parallel_for(tbb::blocked_range<int>(0, 1),
    //     //     [&](const tbb::blocked_range<int>& _r) { makeEC(block_number, 4); });
    }
    // qqf 对该区块的状态数据进行EC
    if(block_number >= 2){
        state_erasure->setStateStorage(state_storage);
        // state_erasure->makeEC(block_number, 4);
        // state_erasure->makeMultiEC(block_number, 4);
        // state_erasure->makeECFromKV(block_number);
    }
    ++block_number;
    // m_state.db().commit();
    // state_erasure->accRandCount();
    state_storage[block_number].clear();
    */
}


void MPTState::setRoot(h256 const& _root)
{
    m_state.setRoot(_root);
}

u256 const& MPTState::accountStartNonce() const
{
    return m_state.accountStartNonce();
}

u256 const& MPTState::requireAccountStartNonce() const
{
    return m_state.requireAccountStartNonce();
}

void MPTState::noteAccountStartNonce(u256 const& _actual)
{
    m_state.noteAccountStartNonce(_actual);
}

size_t MPTState::savepoint() const
{
    return m_state.savepoint();
}

void MPTState::rollback(size_t _savepoint)
{
    m_state.rollback(_savepoint);
}

void MPTState::clear()
{
    m_state.cacheClear();
}

bool MPTState::checkAuthority(Address const&, Address const&) const
{
    return true;
}

State& MPTState::getState()
{
    return m_state;
}

MPTState& MPTState::getMPTState()
{
    return (*this);
}


}  // namespace mptstate
}  // namespace dev
