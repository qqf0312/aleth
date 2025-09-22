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
 * @MPT state interface for EVM
 *
 * @file MPTState.h
 * @author jimmyshi
 * @date 2018-09-21
 */
#pragma once

#include <libdevcore/Common.h>
#include <libethcore/Exceptions.h>
#include <libethereum/State.h>
// #include <libp2p/Service.h>
#include <boost/property_tree/ptree.hpp>

#include "StateFace.h"
#include "rocksdb/db.h"
#include "rocksdb/options.h"
#include "rocksdb/slice.h"
#include <unordered_map>
// #include "Mediator.h"
// #include "Eurasure-P2P.h"
#include "Eurasure.h"
#include "VersionManager.h"


// #include "BMT.h"

using namespace dev::eth;
using namespace std;

namespace dev
{
namespace mptstate
{

class MPTState : public dev::executive::StateFace
{
private:
    dev::eth::State m_state;
    std::unordered_map<int,dev::StringMap> state_storage;
    int block_number = 1;
    rocksdb::DB* ec_db;                   // DB句柄
    rocksdb::DB* vc_db;                   // DB句柄
    const int subcommit_num = 16;
    const int init_vc_size = 5000;

    // std::shared_ptr<ec::Eurasure> state_erasure;
    const int max_state_size = 4000;
    // const double false_positive_rate = 0.01;
    // bf::hasher hashers;
    bool flag = false;
public:
    
    int64_t t_state_size = 0;
    int64_t t_extraInfo_size = 0;
    int64_t t_encoded_size = 0;
    // 记录状态数据的存储信息
    struct StateLocation {
        public:
        std::string node_id; // 存储节点的标识符
        int block_number; // 区块高度编号
        
        StateLocation() : node_id(""), block_number() {}

        StateLocation(std::string _node_id, int _block_number){
            node_id= _node_id;
            block_number = _block_number;
        }
    };
    std::unordered_map<dev::h256, StateLocation> stateHashToInfoMap;

    static ec::Eurasure* state_erasure;

    std::unordered_map<int, BMT> BMT_map; 

    VersionManager versionManager;

    explicit MPTState(u256 const& _accountStartNonce) : m_state(_accountStartNonce)
    {
        std::cout << "init start1" << std::endl;
        if(!flag)
        {
            // initVC();
            flag = true;
        }
       
       std::cout << "init success" << std::endl;
    };

    explicit MPTState(u256 const& _accountStartNonce, OverlayDB const& _db,
        BaseState _bs = BaseState::PreExisting)
      : m_state(_accountStartNonce, _db, _bs)
      {
          std::cout << "init start2" << std::endl;
        if(!flag)
        {
            // initVC();
            flag = true;
        }
        std::cout << "init success" << std::endl;
      };

    enum NullType
    {
        Null
    };
    MPTState(NullType) : m_state(Invalid256, OverlayDB(), BaseState::Empty){
        std::cout << "init start3" << std::endl;
        if(!flag)
        {
            // initVC();
            flag = true;
        }
                  
       std::cout << "init success" << std::endl;
    };

    /// Copy state object.
    MPTState(MPTState const& _s) : m_state(_s.m_state) {
        std::cout << "init start4" << std::endl;
        if(!flag)
        {
            // initVC();
            flag = true;
        }
                  
       std::cout << "init success" << std::endl;
    }
    virtual ~MPTState() = default;
    /// Copy state object.
    MPTState& operator=(MPTState const& _s)
    {
        m_state = _s.m_state;
        return *this;
    }

    static OverlayDB openDB(boost::filesystem::path const& _path, h256 const& _genesisHash,
        WithExisting _we = WithExisting::Trust);

    bool initVC();
    // std::shared_ptr<dev::p2p::Service> initP2PService(boost::property_tree::ptree& pt);
    // void getNodeList(dev::h512s& sealers, std::string config_path);
    // void getHasherFromDB(bf::hasher& h);

    void makeEC(int block_number, int thread_number);

    std::unordered_map<h256, std::string> makeECFromMPT(int block_number, std::vector<int> config);

    bool addressInUse(Address const& _address) const override;

    bool accountNonemptyAndExisting(Address const& _address) const override;

    bool addressHasCode(Address const& _address) const override;

    u256 balance(Address const& _id) const override;

    void addBalance(Address const& _id, u256 const& _amount) override;

    void subBalance(Address const& _addr, u256 const& _value) override;

    void setBalance(Address const& _addr, u256 const& _value) override;

    void transferBalance(Address const& _from, Address const& _to, u256 const& _value) override;

    h256 storageRoot(Address const& _contract) const override;

    u256 storage(Address const& _contract, u256 const& _memory) override;

    void setStorage(Address const& _contract, u256 const& _location, u256 const& _value) override;

    void clearStorage(Address const& _contract) override;

    void createContract(Address const& _address) override;

    void setCode(Address const& _address, bytes&& _code, u256 const& _version) override;

    void kill(Address _a) override;

    bytes const code(Address const& _addr) const override;

    h256 codeHash(Address const& _contract) const override;

    bool frozen(Address const& _contract) const override;

    size_t codeSize(Address const& _contract) const override;

    void incNonce(Address const& _id) override;

    void setNonce(Address const& _addr, u256 const& _newNonce) override;

    u256 getNonce(Address const& _addr) const override;

    h256 rootHash(bool _needCal = true) const override;
    void commit() override;

    void dbCommit(h256 const& _blockHash, int64_t _blockNumber) override;

    void setRoot(h256 const& _root) override;

    u256 const& accountStartNonce() const override;

    u256 const& requireAccountStartNonce() const override;

    void noteAccountStartNonce(u256 const& _actual) override;

    size_t savepoint() const override;

    void rollback(size_t _savepoint) override;

    void clear() override;

    bool checkAuthority(Address const& _origin, Address const& _contract) const override;

    State& getState();
    MPTState& getMPTState() ;
    int getsubcommitsize(){return subcommit_num;}

    // std::shared_ptr<ec::Eurasure> getErasure(){return state_erasure;}
    ec::Eurasure* getErasure(){return state_erasure;}

};

}  // namespace mptstate
}  // namespace dev
