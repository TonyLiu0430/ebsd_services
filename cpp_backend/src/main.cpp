#include <EbsdLib/IO/EbsdImporter.h>
#include <EbsdLib/IO/HKL/CprReader.h>
#include <vector>
#include <array>
#include <memory>
#include <string>
#include <EbsdLib/Core/OrientationTransformation.hpp>
#include <EbsdLib/LaueOps/LaueOps.h>
#include <algorithm>
#include <cmath>
#include <ranges>
#include <map>
#include <stdexcept>
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

std::string std_pos(const std::string& s) {
    std::string res = trim(s);

    // Format 1: 已有破折號的標準格式，如 "C-B", "M-U", "E-M"
    if (res.size() == 3 && res[1] == '-') {
        char col = std::toupper(static_cast<unsigned char>(res[0]));
        char row = std::toupper(static_cast<unsigned char>(res[2]));
        if ((col == 'C' || col == 'M' || col == 'E') &&
            (row == 'U' || row == 'M' || row == 'B')) {
            return std::string{col} + "-" + std::string{row};
        }
    }

    // Format 2 (DATA13): E/I/M 開頭 → E=E, I=C, M=M
    // Format 3 (DATA14+): A/B/C 開頭 → A=C, B=M, C=E
    if (res.size() >= 2) {
        char c0 = std::toupper(static_cast<unsigned char>(res[0]));
        char c1 = std::toupper(static_cast<unsigned char>(res[1]));

        std::string col;
        switch (c0) {
            case 'E': col = "E"; break;
            case 'I': col = "C"; break;
            case 'M': col = "M"; break;
            case 'A': col = "C"; break;
            case 'B': col = "M"; break;
            case 'C': col = "E"; break;
            default: break;
        }

        std::string row;
        switch (c1) {
            case 'U': row = "U"; break;
            case 'M': row = "M"; break;
            case 'B': row = "B"; break;
            case 'D': row = "B"; break;
            default: break;
        }

        if (!col.empty() && !row.empty()) {
            return col + "-" + row;
        }
    }

    return res;
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

            const std::string pos = std_pos(p.stem().string());
            const std::string base_key = data_patch + "_" + pos;

            if (!has_num || num == "01") {
                data[base_key] = std::make_pair(p, true);
            } 
            else {
                int current_ver = std::stoi(num);
                std::string current_key = base_key + "_" + std::to_string(current_ver);

                data[current_key] = std::make_pair(p, true);

                std::string prev_key;
                if (current_ver == 2) {
                    prev_key = base_key;
                } else {
                    prev_key = base_key + "_" + std::to_string(current_ver - 1);
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




#include "httplib.h"
#include "stduuid/uuid.h"

void ml_feature() {
    auto ebsds = get_ebsds("/mnt/e/CODE_programming/.EBSD/202602121503148937---EBSD20260212/EBSD TEST DATA_20260212 - modified/EBSD TEST DATA_20260212/靶材/銅(Cu)");
    std::cout << "Found " << ebsds.size() << " .cpr files." << std::endl;
    int good_count = 0;
    for(auto &[key, value] : ebsds) {
        auto &[path, good] = value;
        // cout << key << ": " << path << ", good: " << good << endl;
        if(good) {
            good_count++;
        }
    }
    cout << "good" << good_count << ", bad: " << (ebsds.size() - good_count) << endl;
    vector<nlohmann::json> features_and_labels;
    vector<std::future<nlohmann::json>> futures;
    for(auto &[key, value] : ebsds) {
        auto &[path, good] = value;


        if(key.substr(0, 6) == "DATA43") {
            good = false; // DATA43 is bad
            // continue;
        }

        bool golden = false;
        if(key.substr(0, 6) == "DATA10" || key.substr(0, 6) == "DATA13" || key.substr(0, 6) == "DATA24") {
            if(good) {
                golden = true;
            }
        }
        
        futures.push_back(std::async(std::launch::async, [path, good, golden, key]() {
            auto feat = features(path.string());
            nlohmann::json j = {
                {"patch", key.substr(0, 6)},
                {"pos", key.substr(7, 3)},
                {"golden", golden},
                {"label", good},
                {"features", feat}
            };
            return j;
        }));
    }
    for(auto& f : futures) {
        features_and_labels.push_back(f.get());
        // cout << features_and_labels.back()["patch"] << " " << features_and_labels.back()["golden"] << endl;
    }
    std::filesystem::create_directories("_out");
    nlohmann::json j = features_and_labels;
    std::ofstream out("_out/features_and_labels.json");
    out << j.dump(4);
}

class TempFile {
    std::filesystem::path path;
public:
    TempFile(const std::filesystem::path &_path): path(_path) {
        if(std::filesystem::exists(path)) {
            throw std::runtime_error("File already exists: " + path.string());
        }
    }
    std::filesystem::path get_path() const {
        return path;
    }
    ~TempFile() {
        if(std::filesystem::exists(path)) {
            std::filesystem::remove(path);
        }
    }
};

int parse_min_grain_size(const httplib::Request& req) {
    if(!req.has_param("min_grain_size")) {
        return 10;
    }

    try {
        const std::string raw_value = req.get_param_value("min_grain_size");
        std::size_t parsed = 0;
        int value = std::stoi(raw_value, &parsed);
        if(parsed != raw_value.size()) {
            throw std::invalid_argument("invalid trailing characters");
        }
        if(value < 1 || value > 1000000) {
            throw std::out_of_range("min_grain_size out of range");
        }
        return value;
    } catch(...) {
        throw std::invalid_argument("min_grain_size must be an integer between 1 and 1000000");
    }
}

std::array<double, 3> parse_ipf_reference_dir(const httplib::Request& req) {
    if(!req.has_param("reference_x") && !req.has_param("reference_y") && !req.has_param("reference_z")) {
        return {0.0, 0.0, 1.0};
    }
    if(!req.has_param("reference_x") || !req.has_param("reference_y") || !req.has_param("reference_z")) {
        throw std::invalid_argument("reference_x, reference_y, and reference_z are required together");
    }

    try {
        const double x = std::stod(req.get_param_value("reference_x"));
        const double y = std::stod(req.get_param_value("reference_y"));
        const double z = std::stod(req.get_param_value("reference_z"));
        const double norm = std::sqrt(x * x + y * y + z * z);
        if(!std::isfinite(norm) || norm <= 1e-12) {
            throw std::invalid_argument("zero vector");
        }
        return {x / norm, y / norm, z / norm};
    } catch(...) {
        throw std::invalid_argument("reference direction vector must be finite numbers and not zero");
    }
}

void test() {
    
    std::string cpr_file_path = "/mnt/e/CODE_programming/.EBSD/202602121503148937---EBSD20260212/EBSD TEST DATA_20260212 - modified/EBSD TEST DATA_20260212/靶材/銅(Cu)/DATA10-01/M-B.cpr";
    std::shared_ptr<CprReader> reader = std::make_shared<CprReader>();
    reader->setFileName(cpr_file_path);
    int err_code = reader->readFile();
    if(err_code < 0) {
        throw std::runtime_error("Failed to read CPR file: " + reader->getErrorMessage());
    }
    int xDim = reader->getXDimension();
    int yDim = reader->getYDimension();
    int totalPoints = xDim * yDim;
    auto phase = reader->getPhasePointer();
    auto euler1 = reader->getEuler1Pointer();
    auto euler2 = reader->getEuler2Pointer();
    auto euler3 = reader->getEuler3Pointer();
    {
        GrainSegmenter grainSegmenter(euler1, euler2, euler3, xDim, yDim);
        grainSegmenter.find_grains(5);
        //grainSegmenter.save_grains_map(10, "/home/tonyliu/project/ebsd_services/cpp_backend/_out/DATA10-C-U_grains.png");
        Orientations_finder_by_pixel finder(euler1, euler2, euler3, phase, xDim, yDim);
        std::tuple<double, double, double> ratio = finder.ratio(20);
        cout << "ratio(pixel): " << std::get<0>(ratio) << ", " << std::get<1>(ratio) << ", " << std::get<2>(ratio) << endl;
        Orientations_finder finder2(grainSegmenter.find_grains(10));
        ratio = finder2.ratio(20);
        cout << "ratio(grain): " << std::get<0>(ratio) << ", " << std::get<1>(ratio) << ", " << std::get<2>(ratio) << endl;
    }

    denoindex(euler1, euler2, euler3, phase, xDim, yDim, 50);
    {
        GrainSegmenter grainSegmenter(euler1, euler2, euler3, xDim, yDim);
        grainSegmenter.find_grains(5);
        //grainSegmenter.save_grains_map(10, "/home/tonyliu/project/ebsd_services/cpp_backend/_out/DATA10-C-U_grains.png");
        Orientations_finder_by_pixel finder(euler1, euler2, euler3, phase, xDim, yDim);
        std::tuple<double, double, double> ratio = finder.ratio(20);
        cout << "ratio(pixel): " << std::get<0>(ratio) << ", " << std::get<1>(ratio) << ", " << std::get<2>(ratio) << endl;
        Orientations_finder finder2(grainSegmenter.find_grains(10));
        ratio = finder2.ratio(20);
        cout << "ratio(grain): " << std::get<0>(ratio) << ", " << std::get<1>(ratio) << ", " << std::get<2>(ratio) << endl;

        denoindex(euler1, euler2, euler3, phase, xDim, yDim, 50);
    }

}

void test2() {
    auto ebsds = get_ebsds("/mnt/e/CODE_programming/.EBSD/202602121503148937---EBSD20260212/EBSD TEST DATA_20260212 - modified/EBSD TEST DATA_20260212/靶材/銅(Cu)");
    for(auto &[key, value] : ebsds) {
        auto &[path, good] = value;
        std::filesystem::path img_path = "/home/tonyliu/project/ebsd_services/cpp_backend/_out";
        img_path += path;
        auto stem = img_path.stem().string() + ".png";
        img_path = img_path.parent_path() / stem;
        save_ipf_map(path, img_path);
    }
}

void test3() {
    
}

int main() {
    // test();
    // return 0;
    //test2();
    //return 0;
    // srand(time(NULL));
    // test();
    // return 0;
    // ml_feature();
    // return 0;
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
        TempFile cpr(cpr_path);
        TempFile crc(crc_path);
        std::ofstream(cpr_path, std::ios::binary).write(cpr_file.content.data(), cpr_file.content.size());
        std::ofstream(crc_path, std::ios::binary).write(crc_file.content.data(), crc_file.content.size());

        int min_grain_size = 10;
        try {
            min_grain_size = parse_min_grain_size(req);
        } catch(const std::exception& exc) {
            res.status = 400;
            res.set_content(exc.what(), "text/plain");
            return;
        }

        nlohmann::json j = features(cpr_path.string(), min_grain_size);

        res.set_content(j.dump(4), "application/json");
    });

    svr.Post("/material_metadata", [](const httplib::Request& req, httplib::Response& res) {
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
        TempFile cpr(cpr_path);
        TempFile crc(crc_path);
        std::ofstream(cpr_path, std::ios::binary).write(cpr_file.content.data(), cpr_file.content.size());
        std::ofstream(crc_path, std::ios::binary).write(crc_file.content.data(), crc_file.content.size());

        try {
            nlohmann::json j = material_metadata(cpr_path.string());
            res.set_content(j.dump(4), "application/json");
        } catch(const std::exception& exc) {
            res.status = 500;
            res.set_content(exc.what(), "text/plain");
        }
    });

    svr.Post("/ipf_map", [](const httplib::Request& req, httplib::Response& res) {
        const auto& cpr_file = req.get_file_value("cpr");
        const auto& crc_file = req.get_file_value("crc");

        if(cpr_file.filename.ends_with(".cpr") && crc_file.filename.ends_with(".crc")) {
            // ok
        } else {
            res.status = 400;
            res.set_content("Expected files with .cpr and .crc extensions", "text/plain");
            return;
        }

        std::filesystem::path temp_dir = "./temp";
        std::filesystem::create_directories(temp_dir);
        std::random_device rd;
        thread_local std::mt19937 rng(rd());
        uuids::uuid_random_generator generator(rng);
        std::string rand_name = uuids::to_string(generator());
        std::string cpr_filename = "upload_" + rand_name + ".cpr";
        std::string crc_filename = "upload_" + rand_name + ".crc";
        std::string ipf_filename = "tempsaved_" + rand_name + ".png";
        std::filesystem::path cpr_path = temp_dir / cpr_filename;
        std::filesystem::path crc_path = temp_dir / crc_filename;
        std::filesystem::path ipf_path = temp_dir / ipf_filename;
        TempFile cpr(cpr_path);
        TempFile crc(crc_path);
        TempFile ipf(ipf_path);
        std::ofstream(cpr_path, std::ios::binary).write(cpr_file.content.data(), cpr_file.content.size());
        std::ofstream(crc_path, std::ios::binary).write(crc_file.content.data(), crc_file.content.size());

        std::array<double, 3> reference_dir = {0.0, 0.0, 1.0};
        try {
            reference_dir = parse_ipf_reference_dir(req);
        } catch(const std::exception& exc) {
            res.status = 400;
            res.set_content(exc.what(), "text/plain");
            return;
        }

        save_ipf_map(cpr_path.string(), ipf_path, reference_dir);

        std::ifstream ipf_fs(ipf_path, std::ios::binary);
        std::string buffer;
        auto size = std::filesystem::file_size(ipf_path);
        buffer.resize(size);

        ipf_fs.read(buffer.data(), size);

        res.set_content(buffer, "image/png");
    });

    svr.Get("/ipf_legend", [](const httplib::Request& req, httplib::Response& res) {
        std::filesystem::path temp_dir = "./temp";
        std::filesystem::create_directories(temp_dir);
        std::random_device rd;
        thread_local std::mt19937 rng(rd());
        uuids::uuid_random_generator generator(rng);
        std::string legend_filename = "ipf_legend_" + uuids::to_string(generator()) + ".png";
        std::filesystem::path legend_path = temp_dir / legend_filename;
        TempFile legend(legend_path);

        int size = 220;
        if(req.has_param("size")) {
            try {
                size = std::stoi(req.get_param_value("size"));
            } catch(...) {
                res.status = 400;
                res.set_content("size must be an integer", "text/plain");
                return;
            }
        }
        size = std::max(80, std::min(size, 640));

        save_ipf_legend(legend_path, size);
        std::ifstream legend_fs(legend_path, std::ios::binary);
        std::string buffer;
        auto file_size = std::filesystem::file_size(legend_path);
        buffer.resize(file_size);
        legend_fs.read(buffer.data(), file_size);

        res.set_content(buffer, "image/png");
    });

    svr.Get("/", [](const httplib::Request& req, httplib::Response& res) {
        res.set_content("Hello, World!", "text/plain");
    });

    std::cout << "Server is running" << std::endl;
    svr.listen("0.0.0.0", 8080);


    // test("./_data/E-B.cpr", 15);
    // save_ipf_map("./_data/E-B.cpr", "./_out/E-B_ipf.png");
    // return 0;
    // -----------------------------------------
}
