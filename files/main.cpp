#include "PCFG.h"
#include <chrono>
#include <fstream>
#include <sstream>
#include "md5.h"
#include <iomanip>
using namespace std;
using namespace chrono;

// 编译指令如下
// g++ main.cpp train.cpp guessing.cpp md5.cpp -o main
// g++ main.cpp train.cpp guessing.cpp md5.cpp -o main -O1
// g++ main.cpp train.cpp guessing.cpp md5.cpp -o main -O2

int main()
{
    //omp_set_num_threads(1);

    // --- 验证 SIMD 版本的正确性 ---
    cout << "Testing MD5Hash_SIMD correctness..." << endl;
    string test_pws_simd[4] = {"123456", "password", "12345678", "qwerty"};
    string test_hashes_simd[4] = {
        "e10adc3949ba59abbe56e057f20f883e",
        "5f4dcc3b5aa765d61d8327deb882cf99",
        "25d55ad283aa400af464c76d713c07ad",
        "d8578edf8458ce06fbc5bb76a58c5ca4"
    };
    
    bit32 batch_state[4][4];
    MD5Hash_SIMD(test_pws_simd, batch_state);
    
    for (int i = 0; i < 4; i++) {
        stringstream ss;
        for (int j = 0; j < 4; j++) {
            ss << std::setw(8) << std::setfill('0') << hex << batch_state[i][j];
        }
        if (ss.str() != test_hashes_simd[i]) {
            cout << "MD5Hash_SIMD test failed for " << test_pws_simd[i] << "!" << endl;
            cout << "Expected: " << test_hashes_simd[i] << "\nGot:      " << ss.str() << endl;
            return 1;
        }
    }
    cout << "MD5Hash_SIMD test passed!" << endl;

    double time_hash = 0;  // 用于MD5哈希的时间
    double time_guess = 0; // 哈希和猜测的总时长
    double time_train = 0; // 模型训练的总时长
    PriorityQueue q;
    auto start_train = system_clock::now();
    q.m.train("/guessdata/Rockyou-singleLined-full.txt");
    q.m.order();
    auto end_train = system_clock::now();
    auto duration_train = duration_cast<microseconds>(end_train - start_train);
    time_train = double(duration_train.count()) * microseconds::period::num / microseconds::period::den;

    q.init();
    cout << "here" << endl;
    int curr_num = 0;
    auto start = system_clock::now();
    // 由于需要定期清空内存，我们在这里记录已生成的猜测总数
    int history = 0;
    // std::ofstream a("./files/results.txt");
    while (!q.priority.empty())
    {
        q.PopNext();
        q.total_guesses = q.guesses.size();
        if (q.total_guesses - curr_num >= 100000)
        {
            cout << "Guesses generated: " <<history + q.total_guesses << endl;
            curr_num = q.total_guesses;

            // 在此处更改实验生成的猜测上限
            int generate_n=10000000;
            if (history + q.total_guesses > 10000000)
            {
                auto end = system_clock::now();
                auto duration = duration_cast<microseconds>(end - start);
                time_guess = double(duration.count()) * microseconds::period::num / microseconds::period::den;
                cout << "Guess time:" << time_guess - time_hash << "seconds"<< endl;//请不要修改这一行
                cout << "Hash time:" << time_hash << "seconds"<<endl;//请不要修改这一行
                cout << "Train time:" << time_train <<"seconds"<<endl;//请不要修改这一行
                break;
            }
        }
        // 为了避免内存超限，我们在q.guesses中口令达到一定数目时，将其中的所有口令取出并且进行哈希
        // 然后，q.guesses将会被清空。为了有效记录已经生成的口令总数，维护一个history变量来进行记录
        if (curr_num > 1000000)
        {
            auto start_hash = system_clock::now();
            size_t total_in_buffer = q.guesses.size();
            size_t batch_idx = 0;

            // --- SIMD 优化部分：4 个口令为一组进行批量哈希 ---
            for (; batch_idx + 3 < total_in_buffer; batch_idx += 4)
            {
                string batch_pws[4] = {
                    q.guesses[batch_idx], 
                    q.guesses[batch_idx + 1], 
                    q.guesses[batch_idx + 2], 
                    q.guesses[batch_idx + 3]
                };
                
                bit32 batch_state[4][4];
               
                MD5Hash_SIMD(batch_pws, batch_state);

            }

            for (; batch_idx < total_in_buffer; batch_idx++)
            {
                bit32 single_state[4];
                MD5Hash(q.guesses[batch_idx], single_state);
            }

            // 在这里对哈希所需的总时长进行计算
            auto end_hash = system_clock::now();
            auto duration = duration_cast<microseconds>(end_hash - start_hash);
            time_hash += double(duration.count()) * microseconds::period::num / microseconds::period::den;

            // 记录已经生成的口令总数
            history += curr_num;
            curr_num = 0;
            q.guesses.clear();
        }
    }
}
