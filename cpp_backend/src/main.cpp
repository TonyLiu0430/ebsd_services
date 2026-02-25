#include <EbsdLib/IO/EbsdImporter.h>
#include <EbsdLib/IO/HKL/CprReader.h>
#include <vector>
#include <memory>
#include <EbsdLib/Core/OrientationTransformation.hpp>
#include <EbsdLib/LaueOps/LaueOps.h>
#include <algorithm>
#include <ranges>
#include <map>
#include "ebsd.h"
#include <ranges>

using std::vector;
using std::pair;
using std::swap;
using std::cout;
using std::endl;

static std::string trim(const std::string& s)
{
    std::size_t begin = 0;
    while (begin < s.size() &&
           std::isspace(static_cast<unsigned char>(s[begin]))) {
        ++begin;
    }

    std::size_t end = s.size();
    while (end > begin &&
           std::isspace(static_cast<unsigned char>(s[end - 1]))) {
        --end;
    }

    return s.substr(begin, end - begin);
}

std::unordered_map<std::string, std::pair<std::filesystem::path, bool>> get_ebsds(const std::string& dir_path) {
    std::filesystem::path dir(dir_path);
    std::unordered_map<std::string, std::pair<std::filesystem::path, bool>> data;

    if (!std::filesystem::exists(dir) || !std::filesystem::is_directory(dir)) {
        throw std::runtime_error("Invalid directory: " + dir_path);
    }

    for (const std::filesystem::directory_entry& subdir_entry : std::filesystem::directory_iterator(dir)) {
        if (!subdir_entry.is_directory()) {
            continue;
        }

        const std::filesystem::path subdir = subdir_entry.path();
        const std::string subdir_name = subdir.filename().string();

        std::vector<std::string> subdir_parts;
        for (auto&& part_view : subdir_name | std::views::split('-')) {
            std::string part;
            for (char ch : part_view) {
                part.push_back(ch);
            }
            subdir_parts.push_back(std::move(part));
        }

        if (subdir_parts.empty()) {
            continue;
        }

        const std::string data_patch = subdir_parts[0];
        const bool has_num = (subdir_parts.size() > 1);
        const std::string num = has_num ? subdir_parts[1] : std::string{};

        for (const std::filesystem::directory_entry& path_entry :
             std::filesystem::directory_iterator(subdir)) {
            if (!path_entry.is_regular_file()) {
                continue;
            }

            const std::filesystem::path p = path_entry.path();

            if (p.extension() != ".cpr") {
                continue;
            }

            const std::string pos = trim(p.stem().string());

            if (!has_num || num == "01") {
                data[data_patch + "-" + pos] = std::make_pair(p, true);
            } else {
                const std::string key02 = data_patch + "-" + pos + "-02";
                const std::string base_key = data_patch + "-" + pos;

                data[key02] = std::make_pair(p, true);

                auto it = data.find(base_key);
                if (it == data.end()) {
                    throw std::runtime_error("Base key not found before -02: " + base_key);
                }

                it->second = std::make_pair(it->second.first, false);
            }
        }
    }

    return data;
}

#include "svm_nestedCV.h"
int main() {
    auto ebsds = get_ebsds("/mnt/e/CODE_programming/.EBSD/202602121503148937---EBSD20260212/EBSD TEST DATA_20260212 - modified/EBSD TEST DATA_20260212/靶材/銅(Cu)");
    vector<vector<double>> X;
    vector<int> Y;
    for(auto &[key, value] : ebsds) {
        auto &[path, good] = value;
        auto [grains, orient] = features(path.string());
        X.push_back({(double)grains.back(), static_cast<double>(grains.size()), std::get<0>(orient), std::get<1>(orient), std::get<2>(orient)});
        Y.push_back(good? 1 : -1);
        // cout << key << ": " << grains.size() << " grains, orientation ratio: " << std::get<0>(orient) << ", " << std::get<1>(orient) << ", " << std::get<2>(orient) << endl;
    }
    NestedCVConfig cfg;
    cfg.outer_k = 5;
    cfg.inner_k = 3;
    cfg.outer_repeats = 10;         // 小資料建議重複幾次，結果穩一點
    cfg.use_balanced_class_weight = true;
    cfg.verbose = true;

    auto result = nested_cv_report_small_imbalanced(X, Y, cfg);
    print_final_report(result);
}