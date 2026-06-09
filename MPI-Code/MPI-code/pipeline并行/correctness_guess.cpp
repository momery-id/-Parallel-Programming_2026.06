#include "PCFG.h"
#include <chrono>
#include <fstream>
#include "md5.h"
#include <iomanip>
#include <unordered_set>
#include <mpi.h>
using namespace std;
using namespace chrono;

int mpi_rank = 0;
int mpi_size = 1;

// 编译指令如下
// g++ main.cpp train.cpp guessing.cpp md5.cpp -o main
// g++ main.cpp train.cpp guessing.cpp md5.cpp -o main -O1
// g++ main.cpp train.cpp guessing.cpp md5.cpp -o main -O2

int main(int argc, char* argv[])
{
    // 初始化 MPI
    MPI_Init(&argc, &argv);
    MPI_Comm_rank(MPI_COMM_WORLD, &mpi_rank);
    MPI_Comm_size(MPI_COMM_WORLD, &mpi_size);

    double time_hash = 0;  // 用于MD5哈希的时间
    double time_guess = 0; // 哈希和猜测的总时长
    double time_train = 0; // 模型训练的总时长
    PriorityQueue q;

    MPI_Barrier(MPI_COMM_WORLD);
    auto start_train = system_clock::now();
    q.m.train("/guessdata/Rockyou-singleLined-full.txt");
    q.m.order();
    auto end_train = system_clock::now();
    auto duration_train = duration_cast<microseconds>(end_train - start_train);
    time_train = double(duration_train.count()) * microseconds::period::num / microseconds::period::den;
    
    // 加载一些测试数据
    unordered_set<std::string> test_set;
    ifstream test_data("/guessdata/Rockyou-singleLined-full.txt");
    int test_count=0;
    string pw;
    while(test_data>>pw)
    {   
        test_count+=1;
        test_set.insert(pw);
        if (test_count>=1000000)
        {
            break;
        }
    }
    int cracked=0;

    q.init();
    if (mpi_rank == 0) cout << "here" << endl;
    int curr_num = 0;
    auto start = system_clock::now();
    // 由于需要定期清空内存，我们在这里记录已生成的猜测总数
    int history = 0;
    // std::ofstream a("./files/results.txt");
#ifdef PIPELINE_MODE
    if (mpi_rank == 0) 
    {
        int hasher_idx = 1; 
        int generate_n = 10000000; // 10M 口令上限

        while (!q.priority.empty())
        {
            q.PopNext();
            q.total_guesses = q.guesses.size();

            if (q.total_guesses - curr_num >= 100000)
            {
                cout << "Guesses generated: " << history + q.total_guesses << endl;
                curr_num = q.total_guesses;

                if (history + q.total_guesses > generate_n) {
                    int needed = generate_n - history;
                    if (needed > 0 && needed < q.guesses.size()) {
                        q.guesses.resize(needed);
                    }

                    stringstream ss;
                    for (const string& pw : q.guesses) {
                        ss << pw << "\n"; 
                    }

                    string buf_str = ss.str();
                    int buf_len = buf_str.size();
                    MPI_Send(&buf_len, 1, MPI_INT, hasher_idx, 99, MPI_COMM_WORLD);
                    MPI_Send(buf_str.c_str(), buf_len, MPI_CHAR, hasher_idx, 100, MPI_COMM_WORLD);

                    history = generate_n;
                    q.guesses.clear(); 
                    break; 
                }
            }

            // 本地生成数量攒满 50 万时，打包发往下一个空闲 Hasher 
            if (q.guesses.size() >= 500000)
            {
                stringstream ss;
                for (const string& pw : q.guesses) {
                    ss << pw << "\n";
                }
                string buf_str = ss.str();
                int buf_len = buf_str.size();

                MPI_Send(&buf_len, 1, MPI_INT, hasher_idx, 99, MPI_COMM_WORLD);
                MPI_Send(buf_str.c_str(), buf_len, MPI_CHAR, hasher_idx, 100, MPI_COMM_WORLD);

                hasher_idx++;
                if (hasher_idx >= mpi_size) {
                    hasher_idx = 1;
                }

                history += q.guesses.size();
                curr_num = 0;
                q.guesses.clear();
            }
        }

        // 处理最后剩余的残余包
        if (history < generate_n && !q.guesses.empty()) {
            stringstream ss;
            for (const string& pw : q.guesses) {
                ss << pw << "\n";
            }
            string buf_str = ss.str();
            int buf_len = buf_str.size();
            MPI_Send(&buf_len, 1, MPI_INT, hasher_idx, 99, MPI_COMM_WORLD);
            MPI_Send(buf_str.c_str(), buf_len, MPI_CHAR, hasher_idx, 100, MPI_COMM_WORLD);
            history += q.guesses.size();
            q.guesses.clear();
        }

        // 发送结束标识（len = -1）通知所有 Hasher 退出
        for (int p = 1; p < mpi_size; ++p) {
            int term_signal = -1;
            MPI_Send(&term_signal, 1, MPI_INT, p, 99, MPI_COMM_WORLD);
        }

        auto end = system_clock::now();
        auto duration = duration_cast<microseconds>(end - start);
        time_guess = double(duration.count()) * microseconds::period::num / microseconds::period::den;
    } 
    else 
    {
        // Hasher 消费逻辑
        while (true)
        {
            int len;
            MPI_Recv(&len, 1, MPI_INT, 0, 99, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
            if (len == -1) {
                break;
            }

            char* buf = new char[len + 1];
            MPI_Recv(buf, len, MPI_CHAR, 0, 100, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
            buf[len] = '\0';

            auto start_hash = system_clock::now();
            stringstream ss(buf);
            string pw;
            bit32 state[4];
            while (getline(ss, pw, '\n')) {
                if (pw.empty()) continue;
                
                // 【提示】：在 main.cpp 性能测试版本中，因无字典加载，需将下面 test_set 查找的 if 段注释掉
                if (test_set.find(pw) != test_set.end()) {
                    cracked += 1;
                }
                
                MD5Hash(pw, state);
            }
            auto end_hash = system_clock::now();
            auto duration = duration_cast<microseconds>(end_hash - start_hash);
            time_hash += double(duration.count()) * microseconds::period::num / microseconds::period::den;

            delete[] buf;
        }

        auto end_hasher = system_clock::now(); 
        auto duration_hasher = duration_cast<microseconds>(end_hasher - start);
        time_guess = double(duration_hasher.count()) * microseconds::period::num / microseconds::period::den;
    }
#else
    while (!q.priority.empty())
    {
        q.PopNext();
        q.total_guesses = q.guesses.size();

        if (q.total_guesses - curr_num >= 100000 / mpi_size)
        {
            if (mpi_rank == 0) cout << "Guesses generated: " <<history + q.total_guesses << endl;
            curr_num = q.total_guesses;

            // 在此处更改实验生成的猜测上限
            int generate_n=10000000 / mpi_size;
            if (history + q.total_guesses > generate_n)
            {
                auto end = system_clock::now();
                auto duration = duration_cast<microseconds>(end - start);
                time_guess = double(duration.count()) * microseconds::period::num / microseconds::period::den;
                break;
            }
        }
        // 为了避免内存超限，我们在q.guesses中口令达到一定数目时，将其中的所有口令取出并且进行哈希
        // 然后，q.guesses将会被清空。为了有效记录已经生成的口令总数，维护一个history变量来进行记录
        if (curr_num > 1000000 / mpi_size)
        {
            auto start_hash = system_clock::now();
            bit32 state[4];
            for (string pw : q.guesses)
            {
                if (test_set.find(pw) != test_set.end()) {
                    cracked+=1;
                }
                // TODO：对于SIMD实验，将这里替换成你的SIMD MD5函数
                MD5Hash(pw, state);

                // 以下注释部分用于输出猜测和哈希，但是由于自动测试系统不太能写文件，所以这里你可以改成cout
                // a<<pw<<"\t";
                // for (int i1 = 0; i1 < 4; i1 += 1)
                // {
                //     a << std::setw(8) << std::setfill('0') << hex << state[i1];
                // }
                // a << endl;
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
#endif

    // 结束同步
    MPI_Barrier(MPI_COMM_WORLD);
    
    int global_cracked = 0;
    double max_time_train = 0;
    double max_time_guess = 0;
    double max_time_hash = 0;

    MPI_Reduce(&cracked, &global_cracked, 1, MPI_INT, MPI_SUM, 0, MPI_COMM_WORLD);
    MPI_Reduce(&time_train, &max_time_train, 1, MPI_DOUBLE, MPI_MAX, 0, MPI_COMM_WORLD);
    MPI_Reduce(&time_guess, &max_time_guess, 1, MPI_DOUBLE, MPI_MAX, 0, MPI_COMM_WORLD);
    MPI_Reduce(&time_hash, &max_time_hash, 1, MPI_DOUBLE, MPI_MAX, 0, MPI_COMM_WORLD);

    if (mpi_rank == 0)
    {
        cout << "Guess time:" << max_time_guess - max_time_hash << "seconds" << endl;
        cout << "Hash time:" << max_time_hash << "seconds" << endl;
        cout << "Train time:" << max_time_train << "seconds" << endl;
        cout << "Cracked:" << global_cracked << endl; 
    }

    MPI_Finalize();
    return 0;

}
