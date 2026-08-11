#include "ebsd_data.h"
#include "texture_figures.h"

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

void verify_png(const std::filesystem::path& path)
{
    std::ifstream stream(path, std::ios::binary);
    std::array<unsigned char, 26> header{};
    stream.read(reinterpret_cast<char*>(header.data()), static_cast<std::streamsize>(header.size()));
    if(!stream.good()) throw std::runtime_error("Could not read generated texture PNG");
    constexpr std::array<unsigned char, 8> signature = {137, 80, 78, 71, 13, 10, 26, 10};
    if(!std::equal(signature.begin(), signature.end(), header.begin())) throw std::runtime_error("Generated texture figure is not a PNG");
    if(read_big_endian_u32(header, 16) != 1150 || read_big_endian_u32(header, 20) != 380) throw std::runtime_error("Unexpected texture figure dimensions");
    if(header[24] != 8 || header[25] != 2) throw std::runtime_error("Texture figure must be an 8-bit RGB PNG");
}

} // namespace

EbsdData load_ebsd_data(const std::filesystem::path&, bool)
{
    return mock_data;
}

int main()
{
    const auto temp_dir = std::filesystem::temp_directory_path();
    const auto pole_path = temp_dir / "ebsd_pole_figure_test.png";
    const auto inverse_path = temp_dir / "ebsd_inverse_pole_figure_test.png";
    try {
        mock_data.x_dim = 4;
        mock_data.y_dim = 3;
        mock_data.x_step = 0.5;
        mock_data.y_step = 0.5;
        mock_data.euler1 = {0.0F, 0.2F, 0.4F, 0.6F, 0.8F, 1.0F, 1.2F, 1.4F, 1.6F, 1.8F, 2.0F, 2.2F};
        mock_data.euler2 = {0.1F, 0.3F, 0.5F, 0.7F, 0.9F, 1.1F, 1.3F, 1.5F, 1.4F, 1.2F, 1.0F, 0.8F};
        mock_data.euler3 = {0.0F, 0.1F, 0.2F, 0.3F, 0.4F, 0.5F, 0.6F, 0.7F, 0.8F, 0.9F, 1.0F, 1.1F};
        mock_data.phase.assign(12, 1);
        mock_data.primary_phase_index = 1;
        mock_data.primary_orientation_ops_index = 1;
        mock_data.phases = {{1, "Cubic", "Copper", {}, 0, 0, 1}};

        save_pole_figure("mock.ctf", pole_path);
        save_inverse_pole_figure("mock.ctf", inverse_path);
        verify_png(pole_path);
        verify_png(inverse_path);
        std::filesystem::remove(pole_path);
        std::filesystem::remove(inverse_path);
    } catch(const std::exception& error) {
        std::filesystem::remove(pole_path);
        std::filesystem::remove(inverse_path);
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
