#include "PCFG.h"
#include <fstream>
#include <cctype>
#include <algorithm>
#include <sstream>

// 这个文件里面的各函数你都不需要完全理解，甚至根本不需要看
// 从学术价值上讲，加速模型的训练过程是一个没什么价值的问题，因为我们一般假定统计学模型的训练成本较低
// 但是，假如你是一个投稿时顶着ddl做实验的倒霉研究生/实习生，提高训练速度就可以大幅节省你的时间了
// 所以如果你愿意，也可以尝试用多线程加速训练过程

/**
 * 怎么加速PCFG训练过程？据助教所知，没有公开文献提出过有效的加速方法（因为这么做基本无学术价值）
 * 
 * 但是统计学模型好就好在其数据是可加的。例如，假如我把数据集拆分成4个部分，并行训练4个不同的模型。
 * 然后我可以直接将四个模型的统计数据进行简单加和，就得到了和串行训练相同的模型了。
 * 
 * 说起来容易，做起来不一定容易，你可能会碰到一系列具体的工程问题。如果你决定加速训练过程，祝你好运！
 * 
 */

// 训练的wrapper，实际上就是读取训练集
void model::train(string path)
{
    string pw;
    ifstream train_set(path);
    int lines = 0;
    if (mpi_rank == 0) cout << "Training..." << endl;
    if (mpi_rank == 0) cout << "Training phase 1: reading and parsing passwords..." << endl;

    // 1. 各进程分工，只解析符合当前 rank 的行
    while (train_set >> pw)
    {
        lines += 1;
        if (lines > 3000000) {
            break;
        }
        if (lines % mpi_size == mpi_rank) {
            parse(pw);
        }
    }

    // 2. 局部模型序列化与发送规约
    string local_buf = serialize_model();
    int local_len = local_buf.size();

    if (mpi_rank == 0) {
        // Rank 0 负责接收并合并其他所有进程的模型
        for (int p = 1; p < mpi_size; ++p) {
            int other_len;
            MPI_Recv(&other_len, 1, MPI_INT, p, 0, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
            char* remote_buf = new char[other_len + 1];
            MPI_Recv(remote_buf, other_len, MPI_CHAR, p, 1, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
            remote_buf[other_len] = '\0';
            
            merge_from_string(string(remote_buf));
            delete[] remote_buf;
        }
    } else {
        // 其他进程发送给 Rank 0
        MPI_Send(&local_len, 1, MPI_INT, 0, 0, MPI_COMM_WORLD);
        MPI_Send(local_buf.c_str(), local_len, MPI_CHAR, 0, 1, MPI_COMM_WORLD);
    }

    // 3. 将 Rank 0 最终合并完的完整模型广播给所有 Rank
    string final_buf;
    int final_len = 0;
    if (mpi_rank == 0) {
        final_buf = serialize_model();
        final_len = final_buf.size();
    }

    MPI_Bcast(&final_len, 1, MPI_INT, 0, MPI_COMM_WORLD);
    if (mpi_rank != 0) {
        final_buf.resize(final_len);
    }
    MPI_Bcast((void*)final_buf.c_str(), final_len, MPI_CHAR, 0, MPI_COMM_WORLD);

    // 非 0 进程清除本地残留的不完整状态，并根据广播内容重建全局模型
    if (mpi_rank != 0) {
        preterminals.clear();
        letters.clear();
        digits.clear();
        symbols.clear();
        preterm_freq.clear();
        letters_freq.clear();
        digits_freq.clear();
        symbols_freq.clear();
        preterm_id = -1;
        letters_id = -1;
        digits_id = -1;
        symbols_id = -1;
        total_preterm = 0;

        merge_from_string(final_buf);
    }

    // 保证同步
    MPI_Barrier(MPI_COMM_WORLD);
}
/// @brief 在模型中找到一个PT的统计数据
/// @param pt 需要查找的PT
/// @return 目标PT在模型中的对应下标
int model::FindPT(PT pt)
{
    for (int id = 0; id < preterminals.size(); id += 1)
    {
        if (preterminals[id].content.size() != pt.content.size())
        {
            continue;
        }
        else
        {
            bool equal_flag = true;
            for (int idx = 0; idx < preterminals[id].content.size(); idx += 1)
            {
                if (preterminals[id].content[idx].type != pt.content[idx].type || preterminals[id].content[idx].length != pt.content[idx].length)
                {
                    equal_flag = false;
                    break;
                }
            }
            if (equal_flag == true)
            {
                return id;
            }
        }
    }
    return -1;
}

/// @brief 在模型中找到一个letter segment的统计数据
/// @param seg 要找的letter segment
/// @return 目标letter segment的对应下标
int model::FindLetter(segment seg)
{
    for (int id = 0; id < letters.size(); id += 1)
    {
        if (letters[id].length == seg.length)
        {
            return id;
        }
    }
    return -1;
}

/// @brief 在模型中找到一个digit segment的统计数据
/// @param seg 要找的digit segment
/// @return 目标digit segment的对应下标
int model::FindDigit(segment seg)
{
    for (int id = 0; id < digits.size(); id += 1)
    {
        if (digits[id].length == seg.length)
        {
            return id;
        }
    }
    return -1;
}

int model::FindSymbol(segment seg)
{
    for (int id = 0; id < symbols.size(); id += 1)
    {
        if (symbols[id].length == seg.length)
        {
            return id;
        }
    }
    return -1;
}

void PT::insert(segment seg)
{
    content.emplace_back(seg);
}

void segment::insert(string value)
{
    if (values.find(value) == values.end())
    {
        values[value] = values.size();
        freqs[values[value]] = 1;
    }
    else
    {
        freqs[values[value]] += 1;
    }
}


void segment::order()
{
    for (pair<string, int> value : values)
    {
        ordered_values.emplace_back(value.first);
    }
    // cout << "value size:" << ordered_values.size() << endl;
    std::sort(ordered_values.begin(), ordered_values.end(),
              [this](const std::string &a, const std::string &b)
              {
                  return freqs.at(values[a]) > freqs.at(values[b]);
              });

    // 将排序后的频率存入 ordered_freqs 并计算 total_freq
    for (const std::string &val : ordered_values)
    {
        ordered_freqs.emplace_back(freqs.at(values[val]));
        total_freq += freqs.at(values[val]);
    }
    for (string val : ordered_values)
    {
        ordered_freqs.emplace_back(freqs.at(values[val]));
        total_freq += freqs.at(values[val]);
    }
}

void model::parse(string pw)
{
    PT pt;
    string curr_part = "";
    int curr_type = 0; // 0: 未设置, 1: 字母, 2: 数字, 3: 特殊字符
    // 请学会使用这种方式写for循环：for (auto it : iterable)
    // 相信我，以后你会用上的。You're welcome :)
    for (char ch : pw)
    {
        if (isalpha(ch))
        {
            if (curr_type != 1)
            {
                if (curr_type == 2)
                {
                    segment seg(curr_type, curr_part.length());
                    if (FindDigit(seg) == -1)
                    {
                        int id = GetNextDigitsID();
                        digits.emplace_back(seg);
                        digits[id].insert(curr_part);
                        digits_freq[id] = 1;
                    }
                    else
                    {
                        int id = FindDigit(seg);
                        digits_freq[id] += 1;
                        digits[id].insert(curr_part);
                    }
                    curr_part.clear();
                    pt.insert(seg);
                }
                else if (curr_type == 3)
                {
                    segment seg(curr_type, curr_part.length());
                    if (FindSymbol(seg) == -1)
                    {
                        int id = GetNextSymbolsID();
                        symbols.emplace_back(seg);
                        symbols_freq[id] = 1;
                        symbols[id].insert(curr_part);
                    }
                    else
                    {
                        int id = FindSymbol(seg);
                        symbols_freq[id] += 1;
                        symbols[id].insert(curr_part);
                    }
                    curr_part.clear();
                    pt.insert(seg);
                }
            }
            curr_type = 1;
            curr_part += ch;
        }
        else if (isdigit(ch))
        {
            if (curr_type != 2)
            {
                if (curr_type == 1)
                {
                    segment seg(curr_type, curr_part.length());
                    if (FindLetter(seg) == -1)
                    {
                        int id = GetNextLettersID();
                        letters.emplace_back(seg);
                        letters_freq[id] = 1;
                        letters[id].insert(curr_part);
                    }
                    else
                    {
                        int id = FindLetter(seg);
                        letters_freq[id] += 1;
                        letters[id].insert(curr_part);
                    }
                    curr_part.clear();
                    pt.insert(seg);
                }
                else if (curr_type == 3)
                {
                    segment seg(curr_type, curr_part.length());
                    if (FindSymbol(seg) == -1)
                    {
                        int id = GetNextSymbolsID();
                        symbols.emplace_back(seg);
                        symbols_freq[id] = 1;
                        symbols[id].insert(curr_part);
                    }
                    else
                    {
                        int id = FindSymbol(seg);
                        symbols_freq[id] += 1;
                        symbols[id].insert(curr_part);
                    }
                    curr_part.clear();
                    pt.insert(seg);
                }
            }
            curr_type = 2;
            curr_part += ch;
        }
        else
        {
            if (curr_type != 3)
            {
                if (curr_type == 1)
                {
                    segment seg(curr_type, curr_part.length());
                    if (FindLetter(seg) == -1)
                    {
                        int id = GetNextLettersID();
                        letters.emplace_back(seg);
                        letters_freq[id] = 1;
                        letters[id].insert(curr_part);
                    }
                    else
                    {
                        int id = FindLetter(seg);
                        letters_freq[id] += 1;
                        letters[id].insert(curr_part);
                    }
                    curr_part.clear();
                    pt.insert(seg);
                }
                else if (curr_type == 2)
                {
                    segment seg(curr_type, curr_part.length());
                    if (FindDigit(seg) == -1)
                    {
                        int id = GetNextDigitsID();
                        digits.emplace_back(seg);
                        digits_freq[id] = 1;
                        digits[id].insert(curr_part);
                    }
                    else
                    {
                        int id = FindDigit(seg);
                        digits_freq[id] += 1;
                        digits[id].insert(curr_part);
                    }
                    curr_part.clear();
                    pt.insert(seg);
                }
            }
            curr_type = 3;
            curr_part += ch;
        }
    }
    if (!curr_part.empty())
    {
        if (curr_type == 1)
        {
            segment seg(curr_type, curr_part.length());
            if (FindLetter(seg) == -1)
            {
                int id = GetNextLettersID();
                letters.emplace_back(seg);
                letters_freq[id] = 1;
                letters[id].insert(curr_part);
            }
            else
            {
                int id = FindLetter(seg);
                letters_freq[id] += 1;
                letters[id].insert(curr_part);
            }
            curr_part.clear();
            pt.insert(seg);
        }
        else if (curr_type == 2)
        {
            segment seg(curr_type, curr_part.length());
            if (FindDigit(seg) == -1)
            {
                int id = GetNextDigitsID();
                digits.emplace_back(seg);
                digits_freq[id] = 1;
                digits[id].insert(curr_part);
            }
            else
            {
                int id = FindDigit(seg);
                digits_freq[id] += 1;
                digits[id].insert(curr_part);
            }
            curr_part.clear();
            pt.insert(seg);
        }
        else
        {
            segment seg(curr_type, curr_part.length());
            if (FindSymbol(seg) == -1)
            {
                int id = GetNextSymbolsID();
                symbols.emplace_back(seg);
                symbols_freq[id] = 1;
                symbols[id].insert(curr_part);
            }
            else
            {
                int id = FindSymbol(seg);
                symbols_freq[id] += 1;
                symbols[id].insert(curr_part);
            }
            curr_part.clear();
            pt.insert(seg);
        }
    }
    // pt.PrintPT();
    // cout<<endl;
    // cout << FindPT(pt) << endl;
    total_preterm += 1;
    if (FindPT(pt) == -1)
    {
        for (int i = 0; i < pt.content.size(); i += 1)
        {
            pt.curr_indices.emplace_back(0);
        }
        int id = GetNextPretermID();
        // cout << id << endl;
        preterminals.emplace_back(pt);
        preterm_freq[id] = 1;
    }
    else
    {
        int id = FindPT(pt);
        // cout << id << endl;
        preterm_freq[id] += 1;
    }
}

void segment::PrintSeg()
{
    if (type == 1)
    {
        cout << "L" << length;
    }
    if (type == 2)
    {
        cout << "D" << length;
    }
    if (type == 3)
    {
        cout << "S" << length;
    }
}

void segment::PrintValues()
{
    // order();
    for (string iter : ordered_values)
    {
        cout << iter << " freq:" << freqs[values[iter]] << endl;
    }
}

void PT::PrintPT()
{
    for (auto iter : content)
    {
        iter.PrintSeg();
    }
}

void model::print()
{
    cout << "preterminals:" << endl;
    for (int i = 0; i < preterminals.size(); i += 1)
    {
        preterminals[i].PrintPT();
        // cout << preterminals[i].curr_indices.size() << endl;
        cout << " freq:" << preterm_freq[i];
        cout << endl;
    }
    // order();
    for (auto iter : ordered_pts)
    {
        iter.PrintPT();
        cout << " freq:" << preterm_freq[FindPT(iter)];
        cout << endl;
    }
    cout << "segments:" << endl;
    for (int i = 0; i < letters.size(); i += 1)
    {
        letters[i].PrintSeg();
        // letters[i].PrintValues();
        cout << " freq:" << letters_freq[i];
        cout << endl;
    }
    for (int i = 0; i < digits.size(); i += 1)
    {
        digits[i].PrintSeg();
        // digits[i].PrintValues();
        cout << " freq:" << digits_freq[i];
        cout << endl;
    }
    for (int i = 0; i < symbols.size(); i += 1)
    {
        symbols[i].PrintSeg();
        // symbols[i].PrintValues();
        cout << " freq:" << symbols_freq[i];
        cout << endl;
    }
}

bool compareByPretermProb(const PT& a, const PT& b) {
    return a.preterm_prob > b.preterm_prob;  // 降序排序
}

void model::order()
{
    if (mpi_rank == 0) cout << "Training phase 2: Ordering segment values and PTs..." << endl;
    for (PT pt : preterminals)
    {
        pt.preterm_prob = float(preterm_freq[FindPT(pt)]) / total_preterm;
        ordered_pts.emplace_back(pt);
    }
    bool swapped;
    if (mpi_rank == 0) cout << "total pts" << ordered_pts.size() << endl;
    std::sort(ordered_pts.begin(), ordered_pts.end(), compareByPretermProb);
    if (mpi_rank == 0) cout << "Ordering letters" << endl;
    // cout << "total letters" << endl;
    for (int i = 0; i < letters.size(); i += 1)
    {
        // cout << i << endl;
        letters[i].order();
    }
    if (mpi_rank == 0) cout << "Ordering digits" << endl;
    // cout << "total letters" << endl;
    for (int i = 0; i < digits.size(); i += 1)
    {
        digits[i].order();
    }
    if (mpi_rank == 0) cout << "ordering symbols" << endl;
    // cout << "total letters" << endl;
    for (int i = 0; i < symbols.size(); i += 1)
    {
        symbols[i].order();
    }
}

// 序列化当前进程的局部模型状态为字符串
string model::serialize_model()
{
    stringstream ss;
    // 1. 序列化总 Preterminal 数量
    ss << total_preterm << "\n";
    
    // 2. 序列化 Preterminals 及频数
    ss << preterminals.size() << "\n";
    for (size_t i = 0; i < preterminals.size(); ++i) {
        ss << preterminals[i].content.size() << " ";
        for (auto& seg : preterminals[i].content) {
            ss << seg.type << " " << seg.length << " ";
        }
        ss << preterm_freq[i] << "\n";
    }

    // 序列化单个 Segment 集合的 lambda 辅助函数
    auto serialize_segs = [&](const vector<segment>& segs, unordered_map<int, int>& segs_freq) {
        ss << segs.size() << "\n";
        for (size_t i = 0; i < segs.size(); ++i) {
            ss << segs[i].type << " " << segs[i].length << " " << segs_freq[i] << " " << segs[i].values.size() << "\n";
            for (auto& pair : segs[i].values) {
                ss << pair.first << " " << segs[i].freqs.at(pair.second) << " ";
            }
            ss << "\n";
        }
    };

    serialize_segs(letters, letters_freq);
    serialize_segs(digits, digits_freq);
    serialize_segs(symbols, symbols_freq);

    return ss.str();
}

// 从接收到的字符串中解析并合并到当前进程的模型中
void model::merge_from_string(const string& buf)
{
    stringstream ss(buf);
    int other_total_preterm;
    if (!(ss >> other_total_preterm)) return;
    total_preterm += other_total_preterm;

    // 1. 合并 Preterminals
    int preterm_size;
    ss >> preterm_size;
    for (int i = 0; i < preterm_size; ++i) {
        int content_size;
        ss >> content_size;
        PT pt;
        for (int j = 0; j < content_size; ++j) {
            int type, length;
            ss >> type >> length;
            pt.insert(segment(type, length));
        }
        int freq;
        ss >> freq;

        int local_id = FindPT(pt);
        if (local_id == -1) {
            for (size_t k = 0; k < pt.content.size(); ++k) {
                pt.curr_indices.emplace_back(0);
            }
            int id = GetNextPretermID();
            preterminals.emplace_back(pt);
            preterm_freq[id] = freq;
        } else {
            preterm_freq[local_id] += freq;
        }
    }

    // 辅助合并 Segment 字典值的 lambda 函数
    auto merge_values_to_local_segment = [&](segment& local_seg, const string& val, int val_freq) {
        if (local_seg.values.find(val) == local_seg.values.end()) {
            int val_id = local_seg.values.size();
            local_seg.values[val] = val_id;
            local_seg.freqs[val_id] = val_freq;
        } else {
            int val_id = local_seg.values[val];
            local_seg.freqs[val_id] += val_freq;
        }
    };

    // 2. 合并 Letters
    int letters_size;
    ss >> letters_size;
    for (int i = 0; i < letters_size; ++i) {
        int type, length, seg_freq, values_size;
        ss >> type >> length >> seg_freq >> values_size;
        segment temp_seg(type, length);
        int local_id = FindLetter(temp_seg);
        if (local_id == -1) {
            local_id = GetNextLettersID();
            letters.emplace_back(temp_seg);
            letters_freq[local_id] = seg_freq;
        } else {
            letters_freq[local_id] += seg_freq;
        }
        for (int j = 0; j < values_size; ++j) {
            string val; int val_freq;
            ss >> val >> val_freq;
            merge_values_to_local_segment(letters[local_id], val, val_freq);
        }
    }

    // 3. 合并 Digits
    int digits_size;
    ss >> digits_size;
    for (int i = 0; i < digits_size; ++i) {
        int type, length, seg_freq, values_size;
        ss >> type >> length >> seg_freq >> values_size;
        segment temp_seg(type, length);
        int local_id = FindDigit(temp_seg);
        if (local_id == -1) {
            local_id = GetNextDigitsID();
            digits.emplace_back(temp_seg);
            digits_freq[local_id] = seg_freq;
        } else {
            digits_freq[local_id] += seg_freq;
        }
        for (int j = 0; j < values_size; ++j) {
            string val; int val_freq;
            ss >> val >> val_freq;
            merge_values_to_local_segment(digits[local_id], val, val_freq);
        }
    }

    // 4. 合并 Symbols
    int symbols_size;
    ss >> symbols_size;
    for (int i = 0; i < symbols_size; ++i) {
        int type, length, seg_freq, values_size;
        ss >> type >> length >> seg_freq >> values_size;
        segment temp_seg(type, length);
        int local_id = FindSymbol(temp_seg);
        if (local_id == -1) {
            local_id = GetNextSymbolsID();
            symbols.emplace_back(temp_seg);
            symbols_freq[local_id] = seg_freq;
        } else {
            symbols_freq[local_id] += seg_freq;
        }
        for (int j = 0; j < values_size; ++j) {
            string val; int val_freq;
            ss >> val >> val_freq;
            merge_values_to_local_segment(symbols[local_id], val, val_freq);
        }
    }
}
