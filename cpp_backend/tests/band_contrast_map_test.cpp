#include "band_contrast_map.h"
#include "ebsd_data.h"
#include "scale_bar.h"

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
        stream.close();

        mock_data.x_dim = 300;
        mock_data.y_dim = 200;
        mock_data.x_step = 2.0;
        mock_data.y_step = 2.0;
        mock_data.band_contrast.assign(static_cast<size_t>(mock_data.x_dim) * mock_data.y_dim, 128);
        save_band_contrast_map("mock.ctf", output_path, true);

        std::ifstream scaled_stream(output_path, std::ios::binary);
        std::array<unsigned char, 26> scaled_header{};
        scaled_stream.read(reinterpret_cast<char*>(scaled_header.data()), static_cast<std::streamsize>(scaled_header.size()));
        require(scaled_stream.good(), "Could not read scale-bar PNG header");
        require(read_big_endian_u32(scaled_header, 16) == 1500, "Unexpected scale-bar PNG width");
        require(read_big_endian_u32(scaled_header, 20) == 1040, "Scale-bar footer must be compact");
        scaled_stream.close();

        const std::vector<uint8_t> raw_rgb(static_cast<size_t>(300) * 200 * 3, 128);
        const RasterImage footer = add_scale_bar_footer(raw_rgb, 300, 200, 3, 2.0, 300, 200, "GS", 1);
        require(footer.width == 300, "Shared scale-bar renderer changed image width");
        require(footer.height == 208, "Shared scale-bar renderer must add an eight-pixel footer");
        require(footer.channels == 3, "Shared scale-bar renderer changed channel count");
        const auto footer_pixel = [&footer](int x, int y, int channel) {
            return footer.pixels[(static_cast<size_t>(y) * footer.width + x) * footer.channels + channel];
        };
        require(footer_pixel(1, 201, 0) == 20, "Scale bar must start one source pixel from the left");
        require(footer_pixel(100, 206, 2) == 20, "Scale bar must cover exactly 100 source pixels");
        require(footer_pixel(101, 206, 0) == 255, "Scale bar must not exceed 100 source pixels");

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
