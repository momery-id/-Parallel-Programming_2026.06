#include <iostream>
#include <Windows.h>

using namespace std;

double run(int n, int k)
{
    int *b = new int[n];   
    int sum = 0;

    for (int i = 0; i < n; i++) {
            b[i] = i; 
    }

    long long head, tail, freq;
    QueryPerformanceFrequency((LARGE_INTEGER *)&freq);

    QueryPerformanceCounter((LARGE_INTEGER *)&head);
    
    for (int count = 0; count < k; count++)
    {
        sum = 0;
        for (int i = 0; i < n; i++)
        {
            sum += b[i];
        }
    }
    
    QueryPerformanceCounter((LARGE_INTEGER *)&tail);

    delete[] b;

    double average_time_ms = (double)(tail - head) * 1000.0 / (freq * k);
    
    cout << "n: " << n << " \t k: " << k 
         << " \t Average time: " << average_time_ms << " ms" << endl;
         
    return average_time_ms;
}

int main()
{
    cout << "*********** Ordinary Way ***************" << endl;
    
    int n[] = {
                10000, 50000, 100000, 150000, 
                180000, 200000, 220000, 240000, 280000, 
                500000, 1000000, 1500000, 2000000, 
                2200000, 2300000, 2400000, 2500000, 2800000,
                3500000, 4500000, 5500000,
                5800000, 6000000, 6200000, 6500000, 7000000,
                10000000, 15000000, 25000000
            };       
    int num_tests = sizeof(n) / sizeof(n[0]);
    
    for (int i = 0; i < num_tests; i++)
    {
        int k = 1000; 
        if (n[i] > 300000) k = 100;
        if (n[i] > 3000000) k = 20;
        if (n[i] > 7000000) k = 5;

        
        run(n[i], k);
    }

    return 0;
}