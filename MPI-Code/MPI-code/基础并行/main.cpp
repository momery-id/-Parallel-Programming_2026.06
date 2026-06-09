#include "PCFG.h"
#include <chrono>
#include <fstream>
#include <sstream>
#include "md5.h"
#include <iomanip>
#include <mpi.h>

using namespace std;
using namespace chrono;

// 定义全局 MPI 变量
int mpi_rank = 0;
int mpi_size = 1;

int main(int argc, char* argv[])
{
    // 初始化 MPI
    MPI_Init(&argc, &argv);
    MPI_Comm_rank(MPI_COMM_WORLD, &mpi_rank);
    MPI_Comm_size(MPI_COMM_WORLD, &mpi_size);

    // 正确性校验仅在 Rank 0 打印输出
    if (mpi_rank == 0) {
        cout << "Testing MD5Hash correctness..." << endl;
        string test_pws[8] = {"123456", "password", "12345678", "qwerty", "123456789", "12345", "1234", "111111"};
        string test_hashes[8] = {
            "e10adc3949ba59abbe56e057f20f883e",
            "5f4dcc3b5aa765d61d8327deb882cf99",
            "25d55ad283aa400af464c76d713c07ad",
            "d8578edf8458ce06fbc5bb76a58c5ca4",
            "25f9e794323b453885f5181f1b624d0b",
            "827ccb0eea8a706c4c34a16891f84e7b",
            "81dc9bdb52d04dc20036dbd8313ed055",
            "96e79218965eb72c92a549dd5a330112"
        };
        for (int i = 0; i < 8; i++) {
            bit32 state[4];
            MD5Hash(test_pws[i], state);
            stringstream ss;
            for (int i1 = 0; i1 < 4; i1 += 1) {
                ss << std::setw(8) << std::setfill('0') << hex << state[i1];
            }
            if (ss.str() != test_hashes[i]) {
                cout << "MD5Hash test failed for " << test_pws[i] << "!" << endl;
                return 1;
            }
        }
        cout << "MD5Hash test passed!" << endl; 
    }

    // 确保各进程同时启动训练
    MPI_Barrier(MPI_COMM_WORLD);

    double time_hash = 0;  
    double time_guess = 0; 
    double time_train = 0; 
    PriorityQueue q;

    auto start_train = system_clock::now();
    q.m.train("/guessdata/Rockyou-singleLined-full.txt");
    q.m.order();
    auto end_train = system_clock::now();
    auto duration_train = duration_cast<microseconds>(end_train - start_train);
    time_train = double(duration_train.count()) * microseconds::period::num / microseconds::period::den;

    q.init();
    if (mpi_rank == 0) {
        cout << "here" << endl;
    }

    int curr_num = 0;
    auto start = system_clock::now();
    int history = 0;

    while (!q.priority.empty())
    {
        q.PopNext();
        q.total_guesses = q.guesses.size();

        if (q.total_guesses - curr_num >= 100000 / mpi_size)
        {
            if (mpi_rank == 0) {
                cout << "Guesses generated (global approx): " << (history + q.total_guesses) * mpi_size << endl;
            }
            curr_num = q.total_guesses;

            // 各进程本地生成目标上限 = 10,000,000 / 进程总数
            int generate_n = 10000000 / mpi_size;
            if (history + q.total_guesses > generate_n)
            {
                auto end = system_clock::now();
                auto duration = duration_cast<microseconds>(end - start);
                time_guess = double(duration.count()) * microseconds::period::num / microseconds::period::den;
                break;
            }
        }

        if (curr_num > 1000000 / mpi_size)
        {
            auto start_hash = system_clock::now();
            bit32 state[4];
            for (string pw : q.guesses)
            {
                MD5Hash(pw, state);
            }

            auto end_hash = system_clock::now();
            auto duration = duration_cast<microseconds>(end_hash - start_hash);
            time_hash += double(duration.count()) * microseconds::period::num / microseconds::period::den;

            history += curr_num;
            curr_num = 0;
            q.guesses.clear();
        }
    }

    // 收集所有进程中的最大运行时间
    MPI_Barrier(MPI_COMM_WORLD);
    double max_time_train = 0;
    double max_time_guess = 0;
    double max_time_hash = 0;

    MPI_Reduce(&time_train, &max_time_train, 1, MPI_DOUBLE, MPI_MAX, 0, MPI_COMM_WORLD);
    MPI_Reduce(&time_guess, &max_time_guess, 1, MPI_DOUBLE, MPI_MAX, 0, MPI_COMM_WORLD);
    MPI_Reduce(&time_hash, &max_time_hash, 1, MPI_DOUBLE, MPI_MAX, 0, MPI_COMM_WORLD);

    if (mpi_rank == 0)
    {
        cout << "Guess time:" << max_time_guess - max_time_hash << "seconds" << endl; 
        cout << "Hash time:" << max_time_hash << "seconds" << endl; 
        cout << "Train time:" << max_time_train << "seconds" << endl; 
    }

    MPI_Finalize();
    return 0;
}