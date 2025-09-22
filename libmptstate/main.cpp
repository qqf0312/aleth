#include <stdlib.h>
#include <fstream>
#include <iostream>

#include "MPTState.h"
#include <libdevcore/SHA3.h>
// #include <tbb/tbb.h>

using namespace std;
using namespace dev::eth;

class MyTimer {
public:
    MyTimer(const std::string &tag_) : tag(tag_) {
        time_count++;
        beginTime = std::chrono::steady_clock::now();
    }

    ~MyTimer() {
        time_count--;
        for (int i = 0; i < time_count; i++) {
            std::cout << "  ";
        }
        std::cout << tag << ":" << std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - beginTime).count() / 1000.0 << " ms" << std::endl;
    }

    inline string PrintTPS(int count) {
        double sec_time = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - beginTime).count() / 1000000.0;
        double tps = count / sec_time;
        auto str = tag + ":" + toString(tps) + "/s" + toString(sec_time) +" s";
        // std::cout << tag << ":" << tps << "/s "
        //           << sec_time << " s" << std::endl;
        return str;
    }

private:
    static int time_count;
    std::string tag;
    std::chrono::_V2::steady_clock::time_point beginTime;
};
int MyTimer::time_count = 0;

int zipf_rand(int N, double skew)
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


int main(int argc, char** argv){
    cout << "Hello mptstate" << endl;

    // init 
    int nodes_number = 8;
    int fault_tolerance = 2;
    int encoding_level = 2;

    int _block_num = 1;
    int _account_num = 2000;
    double skew = 0.0;

    int account_size = 1000000;
    dev::mptstate::MPTState mptState(u256(0), dev::mptstate::MPTState::openDB("./", sha3("0x1234")), BaseState::Empty);
    mptState.state_erasure = new ec::Eurasure();
    {
        MyTimer timer("WRITE");

        double build_time = 0;
        double write_time = 0;

        // auto _block_num = 2;
        // auto _account_num = 10;
        
        int balance = 1;

        vector<u160> processed_data;
        unordered_map<int, vector<h256>> data_map;
        // double skew = 0.0; // address skew
        // writeToLog("Skew"+toString(skew), "output_block_number_log.txt");

        // transaction inject
        vector<vector<u160>> block_account_list;
        block_account_list.push_back(vector<u160>());

        vector<u160> last_account_list;

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
                    processed_data.push_back(tmp);
                }
            }
            block_account_list.push_back(account_list);;
        }
        vector<h256> execute_data_set;

        for (int i = 1; i <= _block_num; i++) {
            // std::cout << i << std::endl;
            vector<h256> data_set;
            auto t1 = std::chrono::steady_clock::now();

            const auto& account_list = block_account_list[i];
            for(const auto& tmp : account_list){
                mptState.addBalance(tmp, u256(balance++)); 
            }

            auto t2 = std::chrono::steady_clock::now();
            // test transcaction execution
            bool execute = false;
            
            // 1. 提交至内存
            mptState.commit();

            // 2. 编码   3. 划分状态
            // mptState.getState().get_m_state().leftOvers(data_set); 
            // data_map[i] = data_set; // 窃取一些h256
            vector<int> _config = {nodes_number, fault_tolerance, encoding_level};
            auto totalEncodedData = mptState.makeECFromMPT(i, _config);

            // 4. 提交至DB（与编码块 # later storge Encoding result to another DB
            // mptState.getState().db().commit(tmp);
            mptState.getState().db().commit();

            auto t3 = std::chrono::steady_clock::now();
            build_time+=std::chrono::duration_cast<std::chrono::microseconds>(t2 - t1).count() / 1000.0;
            write_time+=std::chrono::duration_cast<std::chrono::microseconds>(t3 - t2).count() / 1000.0;
            if (i % 10 == 0) {
                size_t ttsize = 0;
                for(const auto& k : data_set) {
                    std::string rlpDate = mptState.getState().get_m_state().db()->lookup(k);
                    ttsize += rlpDate.size();
                }
                
                // timer.PrintTPS(_account_num);
                // std::cout<<"BUILD:"<<build_time<<" WRITE:"<<write_time<<std::endl;
                auto output = "height:" + toString(i)  
                    + ",State: " + printMemorySize(mptState.t_state_size) 
                    + ",ExtraInfo: " + printMemorySize(mptState.t_extraInfo_size) 
                    + ",Encoded: " + printMemorySize(mptState.t_encoded_size)
                    + ",SOTA State: " + printMemorySize(ttsize);
                // writeToLog(output, "output_block_number_log.txt");
                cout << output << endl;
            }
            cout<<"ROOT HASH:"<< mptState.rootHash(true)<<std::endl;
        }
    }

    return 0;
}