#pragma once

#include <iostream>
#include <fstream>
#include <string>
#include <chrono>
#include <ctime>
#include <iomanip>
#include <queue>
#include <libdevcore/FixedHash.h>
#include <libdevcore/OverlayDB.h>
#include "rocksdb/db.h"
#include <tbb/tbb.h>
extern "C" {
#include "pointproofs.h"
}

using namespace std;
using namespace dev;

inline int zipf_rand(int N, double skew)
{
    static std::random_device rd;
    static std::mt19937 gen(rd());
    static std::uniform_real_distribution<> dis(0.0, 1.0);

    double b = pow(2.0, skew - 1.0);
    double r = dis(gen);
    int rank = (int)(N * pow(r, 1.0 / (1.0 - skew)));
    if (rank >= N) rank = N - 1;
    return rank;
}

// 注入交易
inline vector<vector<u160>> transactionsInject(){
    vector<vector<u160>> block_account_list;
    block_account_list.push_back(vector<u160>());

    vector<u160> last_account_list;

    int _block_num = 1; // 几个区块
    int _account_num = 20; // 一个块内交易数量
    int account_size = 1000000; 
    double skew = 0.0;
    for (int i=1; i <= _block_num; ++i){
        vector<u160> account_list;
        if(!last_account_list.empty()){
            account_list = last_account_list;
        }
        else{
            for(int j=0; j<_account_num; j++){
                u160 tmp;
                if(skew){
                    tmp = zipf_rand(account_size, skew);
                    // cout<<" "<<tmp<<endl;
                }
                else{
                    tmp = u160(rand() % account_size);
                }
                account_list.push_back(tmp);
                // processed_data.push_back(tmp);
            }
        }
        block_account_list.push_back(account_list);;
    }
    return block_account_list;
}


// 一些计算内存大小的函数
inline string printMemorySize(size_t bytes) {
    const double KB = 1024.0;
    const double MB = KB * 1024;
    const double GB = MB * 1024;

    ostringstream oss;
    cout << fixed << setprecision(2);
    oss.precision(2);
    oss.setf(ios::fixed);

    if(bytes < KB){
        cout << bytes << "Bytes";
        oss << bytes << "Bytes";
    }
    else if(bytes < MB){
        cout << bytes / KB << "KB";
        oss << (bytes / KB) << "KB";
    }
    else if(bytes < GB){
        cout << bytes / MB << "MB";
        oss << bytes / MB << "MB";
    }
    else{
        cout << bytes / GB << "GB";
        oss << bytes / GB << "GB";
    }

    return oss.str();
}

inline std::string getTimestamp() {
    auto now = std::chrono::system_clock::now();
    auto time = std::chrono::system_clock::to_time_t(now);
    std::stringstream ss;
    ss << std::put_time(std::localtime(&time), "%Y-%m-%d %H:%M:%S");
    return ss.str();
}
inline void writeToLog(const std::string& message, const std::string& logFile) {
    std::ofstream logStream(logFile, std::ios::app);
    if (!logStream.is_open()) {
        std::cerr << "Failed to open log file: " << logFile << std::endl;
        return;
    }
    logStream << "[" << getTimestamp() << "] " << message << std::endl;
    logStream.close();
}

using Edge = std::pair<h256,h256>;   // src -> dst

static inline h256 to_h256(const std::string& s) {
    if (s.size() != 32) throw std::runtime_error("to_h256: size != 32");
    h256 x;                               // 零初始化
    std::memcpy(x.data(), s.data(), 32);  // 原样拷贝
    return x;
}

static inline void append_u32_be(std::string& s, uint32_t v) {
    s.push_back(char((v >> 24) & 0xFF));
    s.push_back(char((v >> 16) & 0xFF));
    s.push_back(char((v >> 8)  & 0xFF));
    s.push_back(char(v & 0xFF));
}

static inline uint32_t read_u32_be(const char* p) {
    return (uint32_t(uint8_t(p[0])) << 24) |
           (uint32_t(uint8_t(p[1])) << 16) |
           (uint32_t(uint8_t(p[2])) << 8)  |
            uint32_t(uint8_t(p[3]));
}

inline void append_u8(std::string& out, uint8_t v) {
    out.push_back(static_cast<char>(v));   // 注意 cast，避免 char 有符号问题
}

inline uint8_t read_u8(const uint8_t* p) {
    return static_cast<uint8_t>(*p);
}

// key = [epoch(4B,BE)] + [raplicaID(1B)] + [src(32B)]
static inline std::string idxKey(uint32_t epoch, uint8_t raplicaID, const h256& src) {
    std::string k;
    k.reserve(4 + 1 + 32);
    append_u32_be(k, epoch);
    append_u8(k, raplicaID);
    k.append(reinterpret_cast<const char*>(src.data()), 32);
    return k;
}

// value 还是 32B 的 dst（不变）
static inline std::string idxVal(const h256& dst) {
    return std::string(reinterpret_cast<const char*>(dst.data()), 32);
}

inline constexpr uint8_t kIdSentinelEmpty = 0xFF; // 占位符(不是chunk，而是Encoding Group表示的节点
inline constexpr uint8_t kDataParitySep = 0xFF;

inline void persist_edges(rocksdb::DB& db,
                          const std::vector<Edge>& edges,
                          const vector<h256>& VCgroup,
                          uint32_t epoch) {
    rocksdb::WriteBatch wb;
    for (const auto& e : edges) {
        uint8_t replicaID = kIdSentinelEmpty; // defualt is encoding group hash
        auto it = find(VCgroup.begin(), VCgroup.end(), e.first);
        if(it != VCgroup.end()){
            // find chunk hash, no encoding group hash
            auto idx = static_cast<size_t>(it-VCgroup.begin());
            if(idx < 254){
                replicaID = static_cast<uint8_t>(idx);
            }
            else{
                cout << "[persist_edges] idx is overhead!" << endl;
            }
        }

        auto r = wb.Put(idxKey(epoch, replicaID, e.first), idxVal(e.second));
        if(r.ok()){
            cout << "[Write Ok]:"<< epoch << "|" << replicaID << "|"
                << to_h256(idxVal(e.first)) << "->"
                << to_h256(idxVal(e.second)) << endl;
            cout << idxKey(epoch, replicaID, e.first).size() << " " << idxVal(e.second).size() <<endl;
        }
    }
    auto s = db.Write(rocksdb::WriteOptions{}, &wb);
    if (!s.ok()) {
        // 错误处理
        cout<<"Wirte edges error"<<endl;
    }
}

static inline void persist_encoding_group(rocksdb::DB& _db, string& encoding_group, uint32_t epoch){
    if(true){
        string key = "G|";
        key.append(toString(epoch));
        auto s = _db.Put(rocksdb::WriteOptions(), 
        rocksdb::Slice(key), 
        rocksdb::Slice(encoding_group));
        if(!s.ok()){
            cout << "[persist_encoding_group] write failed" << endl;
        }
        else{
            cout << "[persist_encoding_group] write encoding_group successful!" << endl;
        }
    }
}

// 构造 [epoch_lo, epoch_hi] 范围的起止前缀
static inline std::string SeekKey(uint32_t epoch, uint8_t replicaID) {
    std::string k; 
    append_u32_be(k, epoch); 
    append_u8(k, replicaID);
    return k; // 仅5字节即可
}

// 这一个func可能只能读parity chunk
static inline string scan_epoch_chunk(rocksdb::DB* db, uint32_t lo, uint8_t replicaID) { // replicaID 其实就是 chunk id
    
    cout << "\x1b[35m[scanEpoChunk]\x1b[0m start" << lo << endl;

    string rlt; // 返回 [key:32bytes][value] 组成的字符串

    std::string prefix = SeekKey(lo, replicaID); // 5B
    std::string upper  = prefix;
    upper.back() = static_cast<char>((unsigned char)upper.back() + 1);

    rocksdb::ReadOptions ro;
    rocksdb::Slice upper_slice(upper);
    ro.iterate_upper_bound = &upper_slice;
    auto it = std::unique_ptr<rocksdb::Iterator>(db->NewIterator(ro));

    for (it->Seek(SeekKey(lo, replicaID)); it->Valid(); it->Next()) {
        auto k = it->key();
        if(!k.empty()){
            // cout << "k is ok" << k.size() << endl;
        }
        if (k.size() < 37) {
            continue;  // 期望 4B epoch + 32B src
        }
        // —— 剥掉前缀，得到 src(32B) 的零拷贝视图 ——
        string src(k.data() + 4 + 1, 32);
        cout<<"\x1b[35m[scanEpoChunk]\x1b[0m parity chunk hash:"<< to_h256(src) << endl;
        // 用 src 作为“正表”的 key 去 Get
        string val;
        rocksdb::Status s = db->Get(ro, src, &val);
        if (!s.ok()) {
            if (s.IsNotFound()) {
                // cout << "It is 36B, but can not found, data chunk maybe." <<endl;
                // 索引有但正表缺：按需处理（跳过/修复/告警）
                continue;
            }
            throw std::runtime_error(s.ToString());
        }
        
        // 因为这里没有datachunk，所以搜出来的只能是paritychunk
        // cout << "\x1b[35m[scan_epoch_chunk]\x1b[0m parity hash:" << to_h256(src) << endl;
        // break;
        // TODO: 在这里处理 val（val.data(), val.size()）
        src.append(val);
        rlt = src;
        // 如果要持久化保存：std::string payload(val.data(), val.size());
    }

    // 迭代器状态检查
    if (!it->status().ok()) throw std::runtime_error(it->status().ToString());

    return rlt;
}

// 解析: 从 value 里抽出每个用 [ ... ] 包起来的“字节组”  
// 注意：这里的“[" "]" "|" 都会和 uint8_t 冲突，以后最好换成其他形式 如前面用两个字节表示该组k m数量
static std::vector<std::vector<uint8_t>>
parse_bracketed_groups(const std::string& s)
{
    std::vector<std::vector<uint8_t>> groups;
    size_t i = 0, n = s.size();

    while (i < n) {
        // 找到下一个 '['
        while (i < n && s[i] != '[') ++i;
        if (i >= n) break;
        ++i; // 跳过 '['

        std::vector<uint8_t> g;
        // 收集直到遇到 ']'
        while (i < n && s[i] != ']') {
            if(s[i] == '|') {
                g.push_back(kDataParitySep); // 寻找到了分隔符
            }
            else{
                // 把 char 转为 unsigned 再存成 uint8_t，避免有符号扩展
                g.push_back(static_cast<uint8_t>(static_cast<unsigned char>(s[i])));
            }
            ++i;
        }
        // i==n: 没有匹配到 ']'，视为数据损坏；这里也把已收集的 g 放进去
        groups.emplace_back(std::move(g));

        // 跳过 ']'（如存在）
        if (i < n && s[i] == ']') ++i;
    }
    return groups;
}

// 在已排序的 groups 中，返回“包含 target 的所有组下标”（按少到多的排序）
static std::vector<size_t>
groups_containing(const std::vector<std::vector<uint8_t>>& groups, uint8_t target)
{
    std::vector<size_t> idxs;
    for (size_t i = 0; i < groups.size(); ++i) {
        const auto& g = groups[i];
        if (std::find(g.begin(), g.end(), target) != g.end())
            idxs.push_back(i);
    }
    return idxs;
}

// 打印一个组（十进制/十六进制任选）
static void print_group(const std::vector<uint8_t>& g) {
    std::cout << "[";
    for (size_t i = 0; i < g.size(); ++i) {
        if (i) std::cout << " ";
        if(g[i] == kDataParitySep){
            std::cout << "|";
        }
        else{
            std::cout << unsigned(g[i]); // 十进制
            // 或者十六进制:
            // std::cout << "0x" << std::hex << std::uppercase << unsigned(g[i]) << std::dec;
        }
    }
    std::cout << "]";
}

// 主流程：读取、解析、排序、匹配并打印
static vector<vector<uint8_t>> scan_encoding_groups(rocksdb::DB& db, uint32_t epoch, uint32_t targetID)
{
    vector<vector<uint8_t>> rlt;  // 返回的编码组

    // 1) 读取
    std::string key = "G|";
    key.append(std::to_string(epoch));
    std::string val;
    rocksdb::Status s = db.Get(rocksdb::ReadOptions{}, rocksdb::Slice(key), &val);
    if (!s.ok()) {
        std::cerr << "[scan_encoding_groups] Get failed: " << s.ToString() << "\n";
        return rlt;
    }

    // 2) 解析
    auto groups = parse_bracketed_groups(val);

    // 3) 按长度升序排序（若你想稳定相对顺序，用 stable_sort）
    std::stable_sort(groups.begin(), groups.end(),
                     [](const auto& a, const auto& b){ return a.size() < b.size(); });

    // 4) 目标字节
    uint8_t target = static_cast<uint8_t>(targetID & 0xFF);

    // 5) 打印所有组（少→多）
    std::cout << "Total groups: " << groups.size() << "\n";
    for (size_t i = 0; i < groups.size(); ++i) {
        std::cout << "  #" << i << " (len=" << groups[i].size() << ") ";
        print_group(groups[i]);
        std::cout << "\n";
    }

    // 6) 按顺序判断 target 是否在组里 & 打印匹配的组
    auto hits = groups_containing(groups, target);
    if (hits.empty()) {
        std::cout << "Target " << unsigned(target) << " not found in any group.\n";
    } else {
        std::cout << "Target " << unsigned(target) << " found in groups (by ascending size):\n";
        for (size_t idx : hits) {
            std::cout << "  hit #" << idx << " -> ";
            print_group(groups[idx]);
            rlt.push_back(groups[idx]);
            std::cout << "\n";
        }
    }
    return rlt;
}

inline void append_u64_be(std::string& s, uint64_t v){
    for (int i=7;i>=0;--i) s.push_back(char((v>>(8*i)) & 0xFF));
}
inline uint16_t read_u16_be(const char* p){
    return (uint16_t(uint8_t(p[0]))<<8) | uint16_t(uint8_t(p[1]));
}

static inline std::string serialize_h256_queue_raw(const std::queue<h256>& q) {
    std::string out; out.reserve(size_t(q.size())*32);
    auto tmp = q;
    while (!tmp.empty()) {
        out.append(reinterpret_cast<const char*>(tmp.front().data()), 32);
        tmp.pop();
    }
    return out;
}
static inline std::vector<h256> deserialize_h256_list_raw(const std::string& v) {
    if (v.size() % 32 != 0) throw std::runtime_error("length not multiple of 32");
    size_t n = v.size() / 32;
    std::vector<h256> out(n);
    const char* p = v.data();
    for (size_t i=0;i<n;++i, p+=32) std::memcpy(out[i].data(), p, 32);
    return out;
}

class VCTemplate {
  private:
    std::string m_name;

    pointproofs_params vc_param;
    pointproofs_value vc_initValue;
    pointproofs_commitment vc_commit;
    pointproofs_proof *vc_proofs;
    void initCommitment();

  public:
    /**
     * VCTemplate初始化
     * input:
     *  _name: VCTemplate存储对应的名称
     *  _size: VC数组大小
     *  valueGroup: VC数组的初始化内容
     *
     *
     **/
    VCTemplate(const std::string &_name, int _size,
               const vector<h256> &valueGroup)
        : m_name(_name), m_size(_size) {
        
        vector<string> hashGroup;
        for(auto chunk_hash: valueGroup){
            hashGroup.push_back(toHex(chunk_hash));
        }
        // init value
        m_name += std::to_string(m_size);
        std::cout << m_name << " init value";
        pointproofs_value initValues[m_size];
        for(int i = 0; i < m_size; i++){
            uint8_t *tmp = new uint8_t[hashGroup[i].length()];
            memcpy(tmp, (uint8_t *)(const_cast<char *>(hashGroup[i].c_str())),hashGroup[i].length());
            initValues[i].data = tmp;
            initValues[i].len = hashGroup[i].length();
        }
        

        // init param commit proofs
        char seed[] = "this is a very long seed for pointproofs tests";
        std::string flag("");

        if (1) {
            std::cout << " param";
            pointproofs_paramgen((const uint8_t *)seed, sizeof(seed), 0, m_size,
                                 &vc_param);

            std::cout << " commitment";
            if (pointproofs_commit(vc_param.prover, initValues, m_size,
                                   &vc_commit))
                std::cout << m_name << " ERROR:initCommit" << std::endl;

            std::cout << " proofs";
            vc_proofs = new pointproofs_proof[m_size];
            tbb::parallel_for(
                tbb::blocked_range<int>(0, m_size),
                [&](const tbb::blocked_range<int> &_r) {
                    for (int pos = _r.begin(); pos != _r.end(); ++pos) {
                    
                        pointproofs_prove(vc_param.prover, initValues, m_size,
                                          pos, &vc_proofs[pos]);
                        // std::cout << GetMS() - time1 << std::endl;
                    }
                });
        }
    }
    VCTemplate() {}

    ~VCTemplate() {
        // delete[] vc_initValue.data;
        pointproofs_free_commit(vc_commit);
        pointproofs_free_prover_params(vc_param.prover);
        pointproofs_free_verifier_params(vc_param.verifier);
        for (int i = 0; i < m_size; i++)
            pointproofs_free_proof(vc_proofs[i]);

        delete[] vc_proofs;
    };
    int m_size;
    // rocksdb::DB *m_db;
    // bool writeDB();
    // pointproofs_proof getProof(int _pos) { return vc_proofs[_pos]; }
    // pointproofs_commitment getCommitment() { return vc_commit; }
    // pointproofs_params getParams() { return vc_param; }
};
