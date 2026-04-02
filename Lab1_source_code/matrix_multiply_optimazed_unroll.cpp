#include <iostream>
#include <Windows.h>

using namespace std;

double run(int n, int k)
{
    int *b = new int[n * n];   
    int *a = new int[n];
    int *sum = new int[n];

    for (int i = 0; i < n; i++) {
        a[i] = i;
        for (int j = 0; j < n; j++) {
            b[i * n + j] = i + j; 
        }
    }

    long long head, tail, freq;
    QueryPerformanceFrequency((LARGE_INTEGER *)&freq);
    QueryPerformanceCounter((LARGE_INTEGER *)&head);
    
    for (int count = 0; count < k; count++)
    {
        int i = 0;
        for (; i <= n - 4; i += 4)
        {
            int sum0 = 0, sum1 = 0, sum2 = 0, sum3 = 0;

            for (int j = 0; j < n; j++)
            {
                int a_val = a[j]; 

                sum0 += b[(i + 0) * n + j] * a_val;
                sum1 += b[(i + 1) * n + j] * a_val;
                sum2 += b[(i + 2) * n + j] * a_val;
                sum3 += b[(i + 3) * n + j] * a_val;
            }

            sum[i + 0] = sum0;
            sum[i + 1] = sum1;
            sum[i + 2] = sum2;
            sum[i + 3] = sum3;
        }

        for (; i < n; i++)
        {
            sum[i] = 0;
            for (int j = 0; j < n; j++) {
                sum[i] += b[i * n + j] * a[j];
            }
        }
    }
    
    QueryPerformanceCounter((LARGE_INTEGER *)&tail);

    delete[] a; delete[] b; delete[] sum;

    double average_time_ms = (double)(tail - head) * 1000.0 / (freq * k);
    
    cout << "n: " << n << " \t k: " << k 
         << " \t Average time: " << average_time_ms << " ms" << endl;
         
    return average_time_ms;
}

int main()
{
    cout << "*********** Optimized_unroll Way ***************" << endl;
    
    int n[] = {50, 100, 150, 200, 300, 400, 500, 600, 800, 1000, 1500, 2000, 2500, 3000, 4000, 5000};
    int num_tests = sizeof(n) / sizeof(n[0]);
    
    for (int i = 0; i < num_tests; i++)
    {
        int k = 1000; 
        if (n[i] > 500) k = 100;
        if (n[i] > 1000) k = 10;
        
        run(n[i], k);
    }

    return 0;
}