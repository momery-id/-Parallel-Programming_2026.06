#include "PCFG.h"
#include <fstream>
#include <cctype>
#include <algorithm>
#include <sstream>
#include <iomanip>

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
    vector<string> passwords;
    cout<<"Training..."<<endl;
    cout<<"Training phase 1: reading and parsing passwords..."<<endl;
    while (train_set >> pw)
    {
        lines += 1;
        if (lines % 10000 == 0)
        {
            cout <<"Lines processed: "<< lines << endl;
            // 在这里更改读取的训练集口令上限
            if (lines > 3000000)
            {
                break;
            }
        }
        passwords.emplace_back(pw);
    }

#ifdef _OPENMP
    int max_threads = omp_get_max_threads();
#else
    int max_threads = 1;
#endif
    vector<model> local_models(max_threads);

#ifdef _OPENMP
#pragma omp parallel num_threads(max_threads)
    {
        int tid = omp_get_thread_num();
#pragma omp for schedule(static)
        for (int i = 0; i < static_cast<int>(passwords.size()); i += 1)
        {
            local_models[tid].parse(passwords[i]);
        }
    }
#else
    for (int i = 0; i < static_cast<int>(passwords.size()); i += 1)
    {
        local_models[0].parse(passwords[i]);
    }
#endif

    for (const model &local_model : local_models)
    {
        merge_from(local_model);
    }
}

static void merge_segment_values(segment &dst, const segment &src)
{
    for (const auto &entry : src.values)
    {
        const string &value = entry.first;
        int src_id = entry.second;
        int src_freq = src.freqs.at(src_id);
        auto iter = dst.values.find(value);
        if (iter == dst.values.end())
        {
            int new_id = static_cast<int>(dst.values.size());
            dst.values[value] = new_id;
            dst.freqs[new_id] = src_freq;
        }
        else
        {
            dst.freqs[iter->second] += src_freq;
        }
    }
}

void model::merge_from(const model &local_model)
{
    for (int i = 0; i < static_cast<int>(local_model.preterminals.size()); i += 1)
    {
        PT local_pt = local_model.preterminals[i];
        int local_freq = local_model.preterm_freq.at(i);
        int global_id = FindPT(local_pt);
        if (global_id == -1)
        {
            int new_id = GetNextPretermID();
            preterminals.emplace_back(local_pt);
            preterm_freq[new_id] = local_freq;
        }
        else
        {
            preterm_freq[global_id] += local_freq;
        }
        total_preterm += local_freq;
    }

    for (int i = 0; i < static_cast<int>(local_model.letters.size()); i += 1)
    {
        const segment &local_seg = local_model.letters[i];
        int global_id = FindLetter(local_seg);
        if (global_id == -1)
        {
            int new_id = GetNextLettersID();
            letters.emplace_back(local_seg);
            letters_freq[new_id] = local_model.letters_freq.at(i);
        }
        else
        {
            merge_segment_values(letters[global_id], local_seg);
            letters_freq[global_id] += local_model.letters_freq.at(i);
        }
    }

    for (int i = 0; i < static_cast<int>(local_model.digits.size()); i += 1)
    {
        const segment &local_seg = local_model.digits[i];
        int global_id = FindDigit(local_seg);
        if (global_id == -1)
        {
            int new_id = GetNextDigitsID();
            digits.emplace_back(local_seg);
            digits_freq[new_id] = local_model.digits_freq.at(i);
        }
        else
        {
            merge_segment_values(digits[global_id], local_seg);
            digits_freq[global_id] += local_model.digits_freq.at(i);
        }
    }

    for (int i = 0; i < static_cast<int>(local_model.symbols.size()); i += 1)
    {
        const segment &local_seg = local_model.symbols[i];
        int global_id = FindSymbol(local_seg);
        if (global_id == -1)
        {
            int new_id = GetNextSymbolsID();
            symbols.emplace_back(local_seg);
            symbols_freq[new_id] = local_model.symbols_freq.at(i);
        }
        else
        {
            merge_segment_values(symbols[global_id], local_seg);
            symbols_freq[global_id] += local_model.symbols_freq.at(i);
        }
    }
}

static void serialize_segment(ostream &out, const segment &seg, int seg_freq)
{
    out << seg.type << ' ' << seg.length << ' ' << seg_freq << ' ' << seg.values.size() << '\n';
    for (const auto &entry : seg.values)
    {
        const string &value = entry.first;
        int id = entry.second;
        out << value.size() << ' ' << seg.freqs.at(id) << '\n';
        out.write(value.data(), static_cast<streamsize>(value.size()));
        out << '\n';
    }
}

static segment deserialize_segment(istream &in, int &seg_freq)
{
    int type;
    int length;
    size_t value_count;
    in >> type >> length >> seg_freq >> value_count;
    segment seg(type, length);
    for (size_t i = 0; i < value_count; i += 1)
    {
        size_t value_len;
        int freq;
        in >> value_len >> freq;
        in.get();
        string value;
        value.resize(value_len);
        in.read(&value[0], static_cast<streamsize>(value_len));
        in.get();
        int id = static_cast<int>(seg.values.size());
        seg.values[value] = id;
        seg.freqs[id] = freq;
    }
    return seg;
}

string model::serialize() const
{
    ostringstream out;
    out << total_preterm << '\n';

    out << preterminals.size() << '\n';
    for (int i = 0; i < static_cast<int>(preterminals.size()); i += 1)
    {
        const PT &pt = preterminals[i];
        out << preterm_freq.at(i) << ' ' << pt.content.size() << '\n';
        for (const segment &seg : pt.content)
        {
            out << seg.type << ' ' << seg.length << '\n';
        }
    }

    out << letters.size() << '\n';
    for (int i = 0; i < static_cast<int>(letters.size()); i += 1)
    {
        serialize_segment(out, letters[i], letters_freq.at(i));
    }

    out << digits.size() << '\n';
    for (int i = 0; i < static_cast<int>(digits.size()); i += 1)
    {
        serialize_segment(out, digits[i], digits_freq.at(i));
    }

    out << symbols.size() << '\n';
    for (int i = 0; i < static_cast<int>(symbols.size()); i += 1)
    {
        serialize_segment(out, symbols[i], symbols_freq.at(i));
    }
    return out.str();
}

void model::deserialize(const string &data)
{
    preterm_id = letters_id = digits_id = symbols_id = -1;
    total_preterm = 0;
    preterminals.clear();
    letters.clear();
    digits.clear();
    symbols.clear();
    preterm_freq.clear();
    letters_freq.clear();
    digits_freq.clear();
    symbols_freq.clear();
    ordered_pts.clear();

    istringstream in(data);
    in >> total_preterm;

    size_t preterminal_count;
    in >> preterminal_count;
    for (size_t i = 0; i < preterminal_count; i += 1)
    {
        int freq;
        size_t seg_count;
        in >> freq >> seg_count;
        PT pt;
        for (size_t j = 0; j < seg_count; j += 1)
        {
            int type;
            int length;
            in >> type >> length;
            pt.insert(segment(type, length));
            pt.curr_indices.emplace_back(0);
        }
        int id = GetNextPretermID();
        preterminals.emplace_back(pt);
        preterm_freq[id] = freq;
    }

    size_t segment_count;
    in >> segment_count;
    for (size_t i = 0; i < segment_count; i += 1)
    {
        int freq;
        segment seg = deserialize_segment(in, freq);
        int id = GetNextLettersID();
        letters.emplace_back(seg);
        letters_freq[id] = freq;
    }

    in >> segment_count;
    for (size_t i = 0; i < segment_count; i += 1)
    {
        int freq;
        segment seg = deserialize_segment(in, freq);
        int id = GetNextDigitsID();
        digits.emplace_back(seg);
        digits_freq[id] = freq;
    }

    in >> segment_count;
    for (size_t i = 0; i < segment_count; i += 1)
    {
        int freq;
        segment seg = deserialize_segment(in, freq);
        int id = GetNextSymbolsID();
        symbols.emplace_back(seg);
        symbols_freq[id] = freq;
    }
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
    cout << "Training phase 2: Ordering segment values and PTs..." << endl;
    for (PT pt : preterminals)
    {
        pt.preterm_prob = float(preterm_freq[FindPT(pt)]) / total_preterm;
        ordered_pts.emplace_back(pt);
    }
    bool swapped;
    cout << "total pts" << ordered_pts.size() << endl;
    std::sort(ordered_pts.begin(), ordered_pts.end(), compareByPretermProb);
    cout << "Ordering letters" << endl;
    // cout << "total letters" << endl;
    for (int i = 0; i < letters.size(); i += 1)
    {
        // cout << i << endl;
        letters[i].order();
    }
    cout << "Ordering digits" << endl;
    // cout << "total letters" << endl;
    for (int i = 0; i < digits.size(); i += 1)
    {
        digits[i].order();
    }
    cout << "ordering symbols" << endl;
    // cout << "total letters" << endl;
    for (int i = 0; i < symbols.size(); i += 1)
    {
        symbols[i].order();
    }
}
