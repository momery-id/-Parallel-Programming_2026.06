#include "PCFG.h"
#include <chrono>
#include <fstream>
#include <sstream>
#include "md5.h"
#include <iomanip>
#include <cstdlib>
#include <cstdint>
#ifdef USE_MPI
#include <mpi.h>
#endif
using namespace std;
using namespace chrono;

// 编译指令如下
// g++ main.cpp train.cpp guessing.cpp md5.cpp -o main
// g++ main.cpp train.cpp guessing.cpp md5.cpp -o main -O1
// g++ main.cpp train.cpp guessing.cpp md5.cpp -o main -O2

int main(int argc, char **argv)
{
#ifdef USE_MPI
    MPI_Init(&argc, &argv);
    int mpi_rank = 0;
    int mpi_size = 1;
    MPI_Comm_rank(MPI_COMM_WORLD, &mpi_rank);
    MPI_Comm_size(MPI_COMM_WORLD, &mpi_size);
#else
    int mpi_rank = 0;
    int mpi_size = 1;
#endif

    //下面代码用于测试MD5哈希的正确性
    if (mpi_rank == 0)
    {
        cout << "Testing MD5Hash correctness..." << endl;
    }
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
            cout << "Expected: " << test_hashes[i] << "\nGot:      " << ss.str() << endl;
            return 1;
        }
    }

    string simd_batch[4] = {test_pws[0], test_pws[1], test_pws[2], test_pws[3]};
    bit32 simd_states[4][4];
    MD5Hash_SIMD4(simd_batch, simd_states);
    for (int i = 0; i < 4; i += 1)
    {
        stringstream ss;
        for (int j = 0; j < 4; j += 1)
        {
            ss << std::setw(8) << std::setfill('0') << hex << simd_states[i][j];
        }
        if (ss.str() != test_hashes[i])
        {
            if (mpi_rank == 0)
            {
                cout << "MD5Hash_SIMD4 test failed for " << simd_batch[i] << "!" << endl;
                cout << "Expected: " << test_hashes[i] << "\nGot:      " << ss.str() << endl;
            }
#ifdef USE_MPI
            MPI_Finalize();
#endif
            return 1;
        }
    }
    if (mpi_rank == 0)
    {
        cout << "MD5Hash test passed!" << endl; //请不要修改这一行
    }

    double time_hash = 0;  // 用于MD5哈希的时间
    double time_guess = 0; // 哈希和猜测的总时长
    double time_train = 0; // 模型训练的总时长
    uint64_t local_hash_checksum = 0;
    PriorityQueue q;
    q.SetMPIContext(mpi_rank, mpi_size);

    string train_path = "/guessdata/Rockyou-singleLined-full.txt";
    if (argc >= 2)
    {
        train_path = argv[1];
    }
    long long generate_n = 10000000;
    if (argc >= 3)
    {
        generate_n = atoll(argv[2]);
    }

    auto start_train = system_clock::now();

    if (mpi_rank == 0)
    {
        q.m.train(train_path);
    }

#ifdef USE_MPI
    string model_buffer;
    if (mpi_rank == 0)
    {
        model_buffer = q.m.serialize();
    }
    int model_len = static_cast<int>(model_buffer.size());
    MPI_Bcast(&model_len, 1, MPI_INT, 0, MPI_COMM_WORLD);
    if (mpi_rank != 0)
    {
        model_buffer.resize(model_len);
    }
    MPI_Bcast(&model_buffer[0], model_len, MPI_CHAR, 0, MPI_COMM_WORLD);
    if (mpi_rank != 0)
    {
        q.m.deserialize(model_buffer);
    }
#endif

    q.m.order();
    auto end_train = system_clock::now();
    auto duration_train = duration_cast<microseconds>(end_train - start_train);
    time_train = double(duration_train.count()) * microseconds::period::num / microseconds::period::den;

    q.init();
    auto start = system_clock::now();
    long long generated_global = 0;
    long long next_report = 100000;
    const size_t hash_batch_size = 1000000;

    while (!q.priority.empty())
    {
        q.PopNext();
        generated_global += q.last_generate_global;

        if (mpi_rank == 0 && generated_global >= next_report)
        {
            cout << "Guesses generated: " << generated_global << endl;
            while (next_report <= generated_global)
            {
                next_report += 100000;
            }
        }

        if (q.guesses.size() >= hash_batch_size || generated_global > generate_n)
        {
            auto start_hash = system_clock::now();
            local_hash_checksum ^= MD5HashBatchSIMD(q.guesses);
            auto end_hash = system_clock::now();
            auto duration = duration_cast<microseconds>(end_hash - start_hash);
            time_hash += double(duration.count()) * microseconds::period::num / microseconds::period::den;

            q.guesses.clear();
        }

        if (generated_global > generate_n)
        {
            break;
        }
    }

    if (!q.guesses.empty())
    {
        auto start_hash = system_clock::now();
        local_hash_checksum ^= MD5HashBatchSIMD(q.guesses);
        auto end_hash = system_clock::now();
        auto duration = duration_cast<microseconds>(end_hash - start_hash);
        time_hash += double(duration.count()) * microseconds::period::num / microseconds::period::den;
        q.guesses.clear();
    }

    auto end = system_clock::now();
    auto duration = duration_cast<microseconds>(end - start);
    double time_guess_hash = double(duration.count()) * microseconds::period::num / microseconds::period::den;
    time_guess = time_guess_hash - time_hash;

#ifdef USE_MPI
    double max_time_train = 0;
    double max_time_guess = 0;
    double max_time_hash = 0;
    uint64_t global_hash_checksum = 0;
    MPI_Reduce(&time_train, &max_time_train, 1, MPI_DOUBLE, MPI_MAX, 0, MPI_COMM_WORLD);
    MPI_Reduce(&time_guess, &max_time_guess, 1, MPI_DOUBLE, MPI_MAX, 0, MPI_COMM_WORLD);
    MPI_Reduce(&time_hash, &max_time_hash, 1, MPI_DOUBLE, MPI_MAX, 0, MPI_COMM_WORLD);
    MPI_Reduce(&local_hash_checksum, &global_hash_checksum, 1, MPI_UNSIGNED_LONG_LONG, MPI_BXOR, 0, MPI_COMM_WORLD);
    if (mpi_rank == 0)
    {
        cout << "Guess time:" << max_time_guess << "seconds"<< endl;//请不要修改这一行
        cout << "Hash time:" << max_time_hash << "seconds"<<endl;//请不要修改这一行
        cout << "Train time:" << max_time_train <<"seconds"<<endl;//请不要修改这一行
        cout << "Hash checksum:" << global_hash_checksum << endl;
    }
    MPI_Finalize();
#else
    cout << "Guess time:" << time_guess << "seconds"<< endl;//请不要修改这一行
    cout << "Hash time:" << time_hash << "seconds"<<endl;//请不要修改这一行
    cout << "Train time:" << time_train <<"seconds"<<endl;//请不要修改这一行
    cout << "Hash checksum:" << local_hash_checksum << endl;
#endif
}
