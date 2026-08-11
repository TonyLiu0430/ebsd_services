#include "band_contrast_map.h"
#include "ebsd_data.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb/stb_image_write.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {

EbsdData mock_data;

uint32_t read_big_endian_u32(const std::array<unsigned char, 26>& header, size_t offset)
{
    return (static_cast<uint32_t>(header[offset]) << 24U)
        | (static_cast<uint32_t>(header[offset + 1]) << 16U)
        | (static_cast<uint32_t>(header[offset + 2]) << 8U)
        | static_cast<uint32_t>(header[offset + 3]);
}

void require(bool condition, const std::string& message)
{
    if(!condition) throw std::runtime_error(message);
}

} // namespace

EbsdData load_ebsd_data(const std::filesystem::path&, bool)
{
    return mock_data;
}

int main()
{
    const std::filesystem::path output_path = std::filesystem::temp_directory_path() / "ebsd_band_contrast_map_test.png";
    try {
        mock_data.x_dim = 3;
        mock_data.y_dim = 2;
        mock_data.x_step = 0.5;
        mock_data.y_step = 0.5;
        mock_data.band_contrast = {0, 64, 128, 192, 224, 255};
        save_band_contrast_map("mock.ctf", output_path, false);

        std::ifstream stream(output_path, std::ios::binary);
        std::array<unsigned char, 26> header{};
        stream.read(reinterpret_cast<char*>(header.data()), static_cast<std::streamsize>(header.size()));
        require(stream.good(), "Could not read generated PNG header");
        constexpr std::array<unsigned char, 8> png_signature = {137, 80, 78, 71, 13, 10, 26, 10};
        require(std::equal(png_signature.begin(), png_signature.end(), header.begin()), "Generated file is not a PNG");
        require(read_big_endian_u32(header, 16) == 24, "Unexpected band contrast PNG width");
        require(read_big_endian_u32(header, 20) == 16, "Unexpected band contrast PNG height");
        require(header[24] == 8, "Band contrast PNG must use 8-bit samples");
        require(header[25] == 0, "Band contrast PNG must be grayscale");

        mock_data.band_contrast.clear();
        bool rejected_missing_data = false;
        try {
            save_band_contrast_map("mock.ang", output_path, false);
        } catch(const std::invalid_argument&) {
            rejected_missing_data = true;
        }
        require(rejected_missing_data, "Files without band contrast data must be rejected");
        std::filesystem::remove(output_path);
    } catch(const std::exception& error) {
        std::filesystem::remove(output_path);
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
