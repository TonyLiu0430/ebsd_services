#include <EbsdLib/IO/EbsdImporter.h>
#include <EbsdLib/IO/HKL/CprReader.h>
#include <vector>
#include <array>
#include <memory>
#include <string>
#include <EbsdLib/Core/OrientationTransformation.hpp>
#include <EbsdLib/LaueOps/LaueOps.h>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <ranges>
#include <map>
#include <stdexcept>
#include <nlohmann/json.hpp>
#include <string_view>
#include "band_contrast_map.h"
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

class PreparedEbsdUpload {
public:
    std::filesystem::path primary_path;
    std::vector<std::filesystem::path> temporary_paths;

    PreparedEbsdUpload() = default;
    PreparedEbsdUpload(const PreparedEbsdUpload&) = delete;
    PreparedEbsdUpload& operator=(const PreparedEbsdUpload&) = delete;
    PreparedEbsdUpload(PreparedEbsdUpload&& other) noexcept
    : primary_path(std::move(other.primary_path))
    , temporary_paths(std::move(other.temporary_paths))
    {
        other.temporary_paths.clear();
    }
    ~PreparedEbsdUpload() {
        for(const auto& path : temporary_paths) {
            if(std::filesystem::exists(path)) std::filesystem::remove(path);
        }
    }
};

std::string lower_file_extension(const std::string& filename) {
    std::string extension = std::filesystem::path(filename).extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return extension;
}

PreparedEbsdUpload prepare_ebsd_upload(const httplib::Request& req, const std::string& prefix) {
    const char* primary_field = req.has_file("ebsd") ? "ebsd" : "cpr";
    if(!req.has_file(primary_field)) {
        throw std::invalid_argument("Missing EBSD file field (expected ebsd or cpr)");
    }
    const auto& primary_file = req.get_file_value(primary_field);
    const std::string primary_extension = lower_file_extension(primary_file.filename);
    const bool is_single_file = primary_extension == ".ang" || primary_extension == ".ctf";
    const bool is_cpr_pair = primary_extension == ".cpr";
    if(!is_cpr_pair && !is_single_file) {
        throw std::invalid_argument("Expected .cpr + .crc, a single .ang, or a single .ctf file");
    }

    if(is_cpr_pair) {
        if(!req.has_file("crc")) {
            throw std::invalid_argument("A .cpr upload requires its matching .crc file");
        }
    }

    std::filesystem::path temp_dir = "./temp";
    std::filesystem::create_directories(temp_dir);
    std::random_device rd;
    thread_local std::mt19937 rng(rd());
    uuids::uuid_random_generator generator(rng);
    const std::string random_name = uuids::to_string(generator());

    PreparedEbsdUpload upload;
    upload.primary_path = temp_dir / (prefix + "_" + random_name + primary_extension);
    upload.temporary_paths.push_back(upload.primary_path);
    {
        std::ofstream stream(upload.primary_path, std::ios::binary);
        stream.write(primary_file.content.data(), static_cast<std::streamsize>(primary_file.content.size()));
        if(!stream) throw std::runtime_error("Failed to store uploaded EBSD file");
    }

    if(is_cpr_pair) {
        const auto secondary_file = req.get_file_value("crc");
        if(lower_file_extension(secondary_file.filename) != ".crc") {
            throw std::invalid_argument("A .cpr upload requires its matching .crc file");
        }
        const std::filesystem::path secondary_path = temp_dir / (prefix + "_" + random_name + ".crc");
        upload.temporary_paths.push_back(secondary_path);
        std::ofstream stream(secondary_path, std::ios::binary);
        stream.write(secondary_file.content.data(), static_cast<std::streamsize>(secondary_file.content.size()));
        if(!stream) throw std::runtime_error("Failed to store uploaded CRC file");
    }
    return upload;
}

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

double parse_ipf_denoindex_threshold(const httplib::Request& req) {
    if(!req.has_param("denoindex_threshold")) {
        return 0.0;
    }

    try {
        const std::string raw_value = req.get_param_value("denoindex_threshold");
        std::size_t parsed = 0;
        const double value = std::stod(raw_value, &parsed);
        if(parsed != raw_value.size()) {
            throw std::invalid_argument("invalid trailing characters");
        }
        if(!std::isfinite(value) || value < 0.0 || value > 100.0) {
            throw std::out_of_range("denoindex_threshold out of range");
        }
        return value;
    } catch(...) {
        throw std::invalid_argument("denoindex_threshold must be a finite number between 0 and 100");
    }
}

double parse_ipf_denoindex_misorientation_threshold(const httplib::Request& req) {
    if(!req.has_param("denoindex_misorientation_threshold")) {
        return 10.0;
    }

    try {
        const std::string raw_value = req.get_param_value("denoindex_misorientation_threshold");
        std::size_t parsed = 0;
        const double value = std::stod(raw_value, &parsed);
        if(parsed != raw_value.size()) {
            throw std::invalid_argument("invalid trailing characters");
        }
        if(!std::isfinite(value) || value < 0.0 || value > 180.0) {
            throw std::out_of_range("denoindex_misorientation_threshold out of range");
        }
        return value;
    } catch(...) {
        throw std::invalid_argument("denoindex_misorientation_threshold must be a finite number between 0 and 180");
    }
}

bool parse_include_scale_bar(const httplib::Request& req) {
    if(!req.has_param("include_scale_bar")) {
        return true;
    }
    std::string value = req.get_param_value("include_scale_bar");
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    if(value == "1" || value == "true" || value == "yes" || value == "on") return true;
    if(value == "0" || value == "false" || value == "no" || value == "off") return false;
    throw std::invalid_argument("include_scale_bar must be true or false");
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

        int min_grain_size = 10;
        try {
            min_grain_size = parse_min_grain_size(req);
            auto upload = prepare_ebsd_upload(req, "features");
            nlohmann::json j = features(upload.primary_path.string(), min_grain_size);
            res.set_content(j.dump(4), "application/json");
        } catch(const std::exception& exc) {
            res.status = 400;
            res.set_content(exc.what(), "text/plain");
        }
    });

    svr.Post("/material_metadata", [](const httplib::Request& req, httplib::Response& res) {
        if (!req.is_multipart_form_data()) {
            res.status = 400;
            res.set_content("Content-Type must be multipart/form-data", "text/plain");
            return;
        }

        try {
            auto upload = prepare_ebsd_upload(req, "metadata");
            nlohmann::json j = material_metadata(upload.primary_path.string());
            res.set_content(j.dump(4), "application/json");
        } catch(const std::exception& exc) {
            res.status = 500;
            res.set_content(exc.what(), "text/plain");
        }
    });

    svr.Post("/ipf_map", [](const httplib::Request& req, httplib::Response& res) {
        std::filesystem::path temp_dir = "./temp";
        std::filesystem::create_directories(temp_dir);
        std::random_device rd;
        thread_local std::mt19937 rng(rd());
        uuids::uuid_random_generator generator(rng);
        std::string rand_name = uuids::to_string(generator());
        std::string ipf_filename = "tempsaved_" + rand_name + ".png";
        std::filesystem::path ipf_path = temp_dir / ipf_filename;
        TempFile ipf(ipf_path);

        std::array<double, 3> reference_dir = {0.0, 0.0, 1.0};
        double denoindex_threshold = 0.0;
        double denoindex_misorientation_threshold = 10.0;
        bool include_scale_bar = true;
        try {
            auto upload = prepare_ebsd_upload(req, "ipf");
            reference_dir = parse_ipf_reference_dir(req);
            denoindex_threshold = parse_ipf_denoindex_threshold(req);
            denoindex_misorientation_threshold = parse_ipf_denoindex_misorientation_threshold(req);
            include_scale_bar = parse_include_scale_bar(req);
            save_ipf_map(upload.primary_path, ipf_path, reference_dir, denoindex_threshold, denoindex_misorientation_threshold, include_scale_bar);
        } catch(const std::exception& exc) {
            res.status = 400;
            res.set_content(exc.what(), "text/plain");
            return;
        }

        std::ifstream ipf_fs(ipf_path, std::ios::binary);
        std::string buffer;
        auto size = std::filesystem::file_size(ipf_path);
        buffer.resize(size);

        ipf_fs.read(buffer.data(), size);

        res.set_content(buffer, "image/png");
    });

    svr.Post("/band_contrast_map", [](const httplib::Request& req, httplib::Response& res) {
        if(!req.is_multipart_form_data()) {
            res.status = 400;
            res.set_content("Content-Type must be multipart/form-data", "text/plain");
            return;
        }

        std::filesystem::path temp_dir = "./temp";
        std::filesystem::create_directories(temp_dir);
        std::random_device rd;
        thread_local std::mt19937 rng(rd());
        uuids::uuid_random_generator generator(rng);
        const std::filesystem::path image_path = temp_dir / ("band_contrast_" + uuids::to_string(generator()) + ".png");
        TempFile image(image_path);

        try {
            auto upload = prepare_ebsd_upload(req, "band_contrast");
            const bool include_scale_bar = parse_include_scale_bar(req);
            save_band_contrast_map(upload.primary_path, image_path, include_scale_bar);

            std::ifstream image_stream(image_path, std::ios::binary);
            if(!image_stream) throw std::runtime_error("Failed to read generated band contrast PNG");
            std::string buffer;
            buffer.resize(std::filesystem::file_size(image_path));
            image_stream.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
            if(!image_stream) throw std::runtime_error("Failed to read generated band contrast PNG");
            res.set_content(buffer, "image/png");
        } catch(const std::exception& exc) {
            res.status = 400;
            res.set_content(exc.what(), "text/plain");
        }
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

    svr.Post("/ipf_legend", [](const httplib::Request& req, httplib::Response& res) {
        if (!req.is_multipart_form_data()) {
            res.status = 400;
            res.set_content("Content-Type must be multipart/form-data", "text/plain");
            return;
        }

        std::filesystem::path temp_dir = "./temp";
        std::filesystem::create_directories(temp_dir);
        std::random_device rd;
        thread_local std::mt19937 rng(rd());
        uuids::uuid_random_generator generator(rng);
        std::string rand_name = uuids::to_string(generator());
        std::filesystem::path legend_path = temp_dir / ("ipf_legend_" + rand_name + ".png");
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

        try {
            auto upload = prepare_ebsd_upload(req, "legend");
            save_ipf_legend(upload.primary_path, legend_path, size);
        } catch(const std::exception& exc) {
            res.status = 500;
            res.set_content(exc.what(), "text/plain");
            return;
        }

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
