#include <EbsdLib/IO/EbsdImporter.h>
#include <EbsdLib/IO/HKL/CprReader.h>
#include <vector>
#include <memory>
#include <string>
#include <EbsdLib/Core/OrientationTransformation.hpp>
#include <EbsdLib/LaueOps/LaueOps.h>
#include <algorithm>
#include <ranges>
#include <map>
#include <nlohmann/json.hpp>
#include <string_view>
#include "utils.h"
#include <future>

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
            const std::string base_key = data_patch + "-" + pos;

            if (!has_num || num == "01") {
                data[base_key] = std::make_pair(p, true);
            } 
            else {
                int current_ver = std::stoi(num);
                std::string current_key = base_key + "-" + std::to_string(current_ver);

                data[current_key] = std::make_pair(p, true);

                std::string prev_key;
                if (current_ver == 2) {
                    prev_key = base_key;
                } else {
                    prev_key = base_key + "-" + std::to_string(current_ver - 1);
                }

                auto it = data.find(prev_key);
                if (it == data.end()) {
                    throw std::runtime_error("Previous version key not found for current version " + num + ": " + prev_key);
                }

                it->second.second = false;
            }
        }
    }

    return data;
}


#include "grains.h"
#include "orientations.h"


void test(const std::string &cpr_file_path, double misorientation_threshold) {
    std::shared_ptr<CprReader> reader = std::make_shared<CprReader>();
    reader->setFileName(cpr_file_path);
    if(reader->readFile() < 0) {
        std::cout << "failed" << std::endl;
    }
    int xDim = reader->getXDimension();
    int yDim = reader->getYDimension();
    int totalPoints = xDim * yDim;
    auto phase = reader->getPhasePointer();
    auto euler1 = reader->getEuler1Pointer();
    auto euler2 = reader->getEuler2Pointer();
    auto euler3 = reader->getEuler3Pointer();
    GrainSegmenter grainSegmenter(euler1, euler2, euler3, xDim, yDim);
    std::vector<Grain> grains = grainSegmenter.find_grains(10);
    if(reader->getXStep() != reader->getYStep()) {
        throw std::invalid_argument("X and Y step sizes are not equal");
    }
    double step_size = reader->getXStep();
    Orientations_finder orientations_finder(grains);
    std::vector<double> grain_sizes;
    double pixel_area = step_size * step_size;
    for(auto &g : grains) {
        double area = g.size * pixel_area;
        double equivalent_radius = std::sqrt(area / M_PI);
        grain_sizes.push_back(equivalent_radius * 2); // equivalent diameter
    }

    auto ratio = orientations_finder.ratio(misorientation_threshold);
    nlohmann::json j = {
        {"grain_max", grain_sizes.front()},
        {"grain_count", grain_sizes.size()},
        {"orientation_ratio", {std::get<0>(ratio), std::get<1>(ratio), std::get<2>(ratio)}}
    };
    std::cout << j.dump(4) << std::endl;
}

#include "httplib.h"
#include "stduuid/uuid.h"

int main() {
    httplib::Server svr;
    svr.Post("/features", [](const httplib::Request& req, httplib::Response& res) {
        if (!req.is_multipart_form_data()) {
            res.status = 400;
            res.set_content("Content-Type must be multipart/form-data", "text/plain");
            return;
        }

        const auto& cpr_file = req.get_file_value("cpr");
        const auto& crc_file = req.get_file_value("crc");

        if(cpr_file.filename.ends_with(".cpr") && crc_file.filename.ends_with(".crc")) {
            // ok
        } else {
            res.status = 400;
            res.set_content("Expected files with .cpr and .crc extensions", "text/plain");
            return;
        }

        // save file temp
        std::filesystem::path temp_dir = "./temp";
        std::filesystem::create_directories(temp_dir);
        std::random_device rd;
        thread_local std::mt19937 rng(rd());
        uuids::uuid_random_generator generator(rng);
        std::string rand_name = uuids::to_string(generator());
        std::string cpr_filename = "upload_" + rand_name + ".cpr";
        std::string crc_filename = "upload_" + rand_name + ".crc";
        std::filesystem::path cpr_path = temp_dir / cpr_filename;
        std::filesystem::path crc_path = temp_dir / crc_filename;
        std::ofstream(cpr_path, std::ios::binary).write(cpr_file.content.data(), cpr_file.content.size());
        std::ofstream(crc_path, std::ios::binary).write(crc_file.content.data(), crc_file.content.size());

        nlohmann::json j = features(cpr_path.string());

        std::filesystem::remove(cpr_path);
        std::filesystem::remove(crc_path);

        res.set_content(j.dump(4), "application/json");
    });

    svr.listen("0.0.0.0", 8080);


    // test("./_data/E-B.cpr", 15);
    // save_ipf_map("./_data/E-B.cpr", "./_out/E-B_ipf.png");
    // return 0;
    // -----------------------------------------
    // auto ebsds = get_ebsds("/mnt/e/CODE_programming/.EBSD/202602121503148937---EBSD20260212/EBSD TEST DATA_20260212 - modified/EBSD TEST DATA_20260212/靶材/銅(Cu)");
    // std::cout << "Found " << ebsds.size() << " .cpr files." << std::endl;
    // int good_count = 0;
    // for(auto &[key, value] : ebsds) {
    //     auto &[path, good] = value;
    //     // cout << key << ": " << path << ", good: " << good << endl;
    //     if(good) {
    //         good_count++;
    //     }
    // }
    // cout << "good" << good_count << ", bad: " << (ebsds.size() - good_count) << endl;
    // vector<nlohmann::json> features_and_labels;
    // vector<std::future<nlohmann::json>> futures;
    // for(auto &[key, value] : ebsds) {
    //     auto &[path, good] = value;
    //     futures.push_back(std::async(std::launch::async, [path, good]() {
    //         auto feat = features(path.string());
    //         nlohmann::json j = {
    //             {"label", good},
    //             {"features", feat}
    //         };
    //         return j;
    //     }));
    // }
    // for(auto& f : futures) {
    //     features_and_labels.push_back(f.get());
    // }
    // std::filesystem::create_directories("_out");
    // nlohmann::json j = features_and_labels;
    // std::ofstream out("_out/features_and_labels.json");
    // out << j.dump(4);

    
    
    
    // vector<vector<double>> X;
    // vector<int> Y;
    // for(auto &[key, value] : ebsds) {
    //     auto &[path, good] = value;
    //     auto [grains, orient] = features(path.string());
    //     X.push_back({(double)grains.back(), static_cast<double>(grains.size()), std::get<0>(orient), std::get<1>(orient), std::get<2>(orient)});
    //     Y.push_back(good? 1 : -1);
    //     // cout << key << ": " << grains.size() << " grains, orientation ratio: " << std::get<0>(orient) << ", " << std::get<1>(orient) << ", " << std::get<2>(orient) << endl;
    // }
    // NestedCVConfig cfg;
    // cfg.outer_k = 5;
    // cfg.inner_k = 3;
    // cfg.outer_repeats = 10;         // 小資料建議重複幾次，結果穩一點
    // cfg.use_balanced_class_weight = true;
    // cfg.verbose = true;

    // auto result = nested_cv_report_small_imbalanced(X, Y, cfg);
    // print_final_report(result);
}