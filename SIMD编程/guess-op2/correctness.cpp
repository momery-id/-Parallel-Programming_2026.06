#include "PCFG.h"
#include <chrono>
#include <fstream>
#include "md5.h"
#include <iomanip>
#include <iostream>

using namespace std;
using namespace chrono;

// 编译指令如下：
// g++ correctness.cpp train.cpp guessing.cpp md5.cpp -o test.exe

int main()
{
    // 测试用例
    string test_str = "bvaisdbjasdkafkasdfnavkjnakdjfejfanjsdnfkajdfkajdfjkwanfdjaknsvjkanbjbjadfajwefajksdfakdnsvjadfasjdvabvaisdbjasdkafkasdfnavkjnakdjfejfanjsdnfkajdfkajdfjkwanfdjaknsvjkanbjbjadfajwefajksdfakdnsvjadfasjdvabvaisdbjasdkafkasdfnavkjnakdjfejfanjsdnfkajdfkajdfjkwanfdjaknsvjkanbjbjadfajwefajksdfakdnsvjadfasjdvabvaisdbjasdkafkasdfnavkjnakdjfejfanjsdnfkajdfkajdfjkwanfdjaknsvjkanbjbjadfajwefajksdfakdnsvjadfasjdva";

    bit32 state_serial[4];
    MD5Hash(test_str, state_serial);
    
    cout << "========== 原始串行算法结果 ==========" << endl;
    for (int i1 = 0; i1 < 4; i1 += 1) {
        cout << std::setw(8) << std::setfill('0') << hex << state_serial[i1];
    }
    cout << endl << endl;

    string input_simd[4] = {test_str, test_str, test_str, test_str};
    bit32 state_simd[4][4]; 
    
    MD5Hash_SIMD(input_simd, state_simd);
    
    cout << "========== SIMD 并行算法结果 =========" << endl;
    for (int i1 = 0; i1 < 4; i1 += 1) {
        cout << std::setw(8) << std::setfill('0') << hex << state_simd[0][i1]; 
    }
    cout << endl << endl;

    bool is_correct = true;
    for (int i = 0; i < 4; i++) {
        if (state_serial[i] != state_simd[0][i]) {
            is_correct = false;
            break;
        }
    }

    if (is_correct) {
        cout << "测试通过" << endl;
    } else {
        cout << "测试失败" << endl;
    }

    return 0; 
}