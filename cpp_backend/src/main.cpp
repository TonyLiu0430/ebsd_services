#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <random>
#include <stdexcept>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "band_contrast_map.h"
#include "httplib.h"
#include "scale_bar.h"
#include "stduuid/uuid.h"
#include "texture_figures.h"
#include "utils.h"

class TempFile {
    std::filesystem::path path;
public:
    TempFile(const std::filesystem::path &_path): path(_path) {
        if(std::filesystem::exists(path)) {
            throw std::runtime_error("File already exists: " + path.string());
        }
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

int main() {
    httplib::Server svr;

    auto register_texture_figure_api = [&svr](
        const std::string& route,
        const std::string& upload_prefix,
        void (*render)(const std::filesystem::path&, const std::filesystem::path&)
    ) {
        svr.Post(route, [upload_prefix, render](const httplib::Request& req, httplib::Response& res) {
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
            const std::filesystem::path image_path = temp_dir / (
                upload_prefix + "_" + uuids::to_string(generator()) + ".png"
            );
            TempFile image(image_path);

            try {
                auto upload = prepare_ebsd_upload(req, upload_prefix);
                render(upload.primary_path, image_path);
                std::ifstream stream(image_path, std::ios::binary);
                if(!stream) throw std::runtime_error("Failed to read generated texture figure PNG");
                std::string buffer(std::filesystem::file_size(image_path), '\0');
                stream.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
                if(!stream) throw std::runtime_error("Failed to read generated texture figure PNG");
                res.set_content(buffer, "image/png");
            } catch(const std::exception& exc) {
                res.status = 400;
                res.set_content(exc.what(), "text/plain");
            }
        });
    };
    register_texture_figure_api("/pole_figure", "pole_figure", save_pole_figure);
    register_texture_figure_api("/inverse_pole_figure", "inverse_pole_figure", save_inverse_pole_figure);

    svr.Post("/scale_bar", [](const httplib::Request& req, httplib::Response& res) {
        if(!req.is_multipart_form_data()) {
            res.status = 400;
            res.set_content("Content-Type must be multipart/form-data", "text/plain");
            return;
        }

        try {
            if(!req.has_file("image")) throw std::invalid_argument("Missing raw RGB image field");
            if(!req.has_param("width") || !req.has_param("height") || !req.has_param("x_step_um") || !req.has_param("map_name")) {
                throw std::invalid_argument("width, height, x_step_um, and map_name are required");
            }

            const int width = std::stoi(req.get_param_value("width"));
            const int height = std::stoi(req.get_param_value("height"));
            const double x_step_um = std::stod(req.get_param_value("x_step_um"));
            const std::string map_name = req.get_param_value("map_name");
            if(width <= 0 || height <= 0 || width > 5000 || height > 5000) {
                throw std::invalid_argument("Image dimensions must be between 1 and 5000");
            }
            if(!std::isfinite(x_step_um) || x_step_um <= 0.0) {
                throw std::invalid_argument("x_step_um must be positive");
            }
            if(map_name.empty() || map_name.size() > 16 || !std::all_of(map_name.begin(), map_name.end(), [](unsigned char ch) {
                return std::isalnum(ch) || ch == '+' || ch == '-' || ch == '_';
            })) {
                throw std::invalid_argument("map_name must contain 1-16 letters, numbers, '+', '-', or '_'");
            }

            const auto& raw_image = req.get_file_value("image").content;
            const size_t expected_size = static_cast<size_t>(width) * height * 3;
            if(raw_image.size() != expected_size) {
                throw std::invalid_argument("Raw RGB image byte count does not match width and height");
            }
            const std::vector<uint8_t> source_rgb(raw_image.begin(), raw_image.end());
            const int upscale = map_upscale(width, height);
            RasterImage image = upscale_nearest(source_rgb, width, height, 3, upscale);
            image = add_scale_bar_footer(
                image.pixels,
                image.width,
                image.height,
                image.channels,
                x_step_um,
                width,
                height,
                map_name,
                upscale
            );
            res.set_content(encode_png(image), "image/png");
        } catch(const std::exception& exc) {
            res.status = 400;
            res.set_content(exc.what(), "text/plain");
        }
    });

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

    const char* configured_host = std::getenv("EBSD_BIND_HOST");
    const std::string host = configured_host && *configured_host ? configured_host : "0.0.0.0";
    int port = 8080;
    if (const char* configured_port = std::getenv("EBSD_PORT")) {
        try {
            const std::string value(configured_port);
            size_t consumed = 0;
            port = std::stoi(value, &consumed);
            if (consumed != value.size() || port < 1 || port > 65535)
                throw std::invalid_argument("port out of range");
        } catch (const std::exception&) {
            std::cerr << "EBSD_PORT must be an integer from 1 to 65535" << std::endl;
            return 1;
        }
    }
    std::cout << "Listening on " << host << ':' << port << std::endl;
    if (!svr.listen(host, port)) {
        std::cerr << "Unable to bind backend listener" << std::endl;
        return 1;
    }


}
