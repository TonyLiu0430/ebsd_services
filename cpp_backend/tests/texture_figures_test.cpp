#include "texture_figures.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb/stb_image_write.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <random>
#include <stdexcept>
#include <string>

EbsdData load_ebsd_data(const std::filesystem::path&, bool)
{
    throw std::runtime_error("The texture unit test renders in-memory EBSD data only");
}

namespace {

void require(bool condition, const std::string& message)
{
    if(!condition) throw std::runtime_error(message);
}

EbsdData make_random_data(uint32_t seed, size_t count, bool hcp = false)
{
    std::mt19937 generator(seed);
    std::uniform_real_distribution<float> phi1(0.0F, 2.0F * static_cast<float>(std::acos(-1.0)));
    std::uniform_real_distribution<float> phi(0.0F, static_cast<float>(std::acos(-1.0)));
    EbsdData data;
    data.x_dim = static_cast<int>(count);
    data.y_dim = 1;
    data.x_step = data.y_step = 1.0;
    data.primary_phase_index = 1;
    data.primary_orientation_ops_index = hcp ? 0 : 1;
    data.hcp = hcp;
    data.phase.assign(count, 1);
    data.euler1.reserve(count);
    data.euler2.reserve(count);
    data.euler3.reserve(count);
    for(size_t index = 0; index < count; ++index) {
        data.euler1.push_back(phi1(generator));
        data.euler2.push_back(phi(generator));
        data.euler3.push_back(phi1(generator));
    }
    return data;
}

void verify_density(const TextureFigure& figure, const std::string& name)
{
    require(figure.width == 1260 && figure.height == 423, name + " has unexpected dimensions");
    require(figure.rgb.size() == static_cast<size_t>(figure.width) * figure.height * 3, name + " has invalid RGB storage");
    require(figure.density_max > figure.density_min, name + " has an invalid density range");
    for(const auto& map : figure.densities) {
        require(map.width == 251 && map.height == 251, name + " has an unexpected density grid");
        double sum = 0.0;
        size_t count = 0;
        for(size_t index = 0; index < map.values.size(); ++index) {
            if(!map.mask[index]) continue;
            require(std::isfinite(map.values[index]), name + " contains a non-finite in-domain density");
            require(map.values[index] >= 0.0, name + " contains a negative density");
            sum += map.values[index];
            ++count;
        }
        require(count > 1000, name + " has an empty projection domain");
        require(std::fabs(sum / count - 1.0) < 1e-10, name + " is not normalized to MRD=1");
    }
}

uint32_t png_u32(const std::array<unsigned char, 26>& header, size_t offset)
{
    return (static_cast<uint32_t>(header[offset]) << 24U)
        | (static_cast<uint32_t>(header[offset + 1]) << 16U)
        | (static_cast<uint32_t>(header[offset + 2]) << 8U)
        | static_cast<uint32_t>(header[offset + 3]);
}

void verify_png_round_trip(const TextureFigure& figure, const std::filesystem::path& path)
{
    require(stbi_write_png(path.string().c_str(), figure.width, figure.height, 3, figure.rgb.data(), figure.width * 3) != 0,
            "Could not write random-data comparison PNG");
    std::ifstream stream(path, std::ios::binary);
    std::array<unsigned char, 26> header{};
    stream.read(reinterpret_cast<char*>(header.data()), static_cast<std::streamsize>(header.size()));
    require(stream.good(), "Could not read random-data comparison PNG");
    constexpr std::array<unsigned char, 8> signature = {137,80,78,71,13,10,26,10};
    require(std::equal(signature.begin(), signature.end(), header.begin()), "Texture artifact is not PNG");
    require(png_u32(header, 16) == static_cast<uint32_t>(figure.width), "PNG width differs from rendered image");
    require(png_u32(header, 20) == static_cast<uint32_t>(figure.height), "PNG height differs from rendered image");
    require(header[24] == 8 && header[25] == 2, "Texture PNG is not 8-bit RGB");
}

double image_mae(const TextureFigure& left, const TextureFigure& right)
{
    require(left.rgb.size() == right.rgb.size(), "Cannot compare differently sized texture images");
    double error = 0.0;
    for(size_t index = 0; index < left.rgb.size(); ++index) {
        error += std::abs(static_cast<int>(left.rgb[index]) - static_cast<int>(right.rgb[index]));
    }
    return error / left.rgb.size();
}

} // namespace

int main()
{
    const auto temporary = std::filesystem::temp_directory_path();
    const auto pole_path = temporary / "ebsd_random_pole_figure_test.png";
    const auto inverse_path = temporary / "ebsd_random_inverse_pole_figure_test.png";
    try {
        const EbsdData input = make_random_data(0x5eedU, 4096);
        const TextureFigure pole = render_pole_figure(input);
        const TextureFigure inverse = render_inverse_pole_figure(input);
        verify_density(pole, "PF");
        verify_density(inverse, "IPF");

        // A second render from the identical random input must be byte exact;
        // this catches hidden iteration-order and uninitialized-memory issues.
        require(pole.rgb == render_pole_figure(input).rgb, "PF output is not deterministic");
        require(inverse.rgb == render_inverse_pole_figure(input).rgb, "IPF output is not deterministic");

        // Different random orientations must produce materially different
        // images, ensuring the comparison does not merely validate a template.
        const EbsdData other = make_random_data(0xc0ffeeU, 4096);
        require(image_mae(pole, render_pole_figure(other)) > 0.25, "PF image ignores orientation data");
        require(image_mae(inverse, render_inverse_pole_figure(other)) > 0.25, "IPF image ignores orientation data");

        verify_png_round_trip(pole, pole_path);
        verify_png_round_trip(inverse, inverse_path);

        // Exercise the second crystal-family branch as part of every CTest run.
        verify_density(render_pole_figure(make_random_data(73, 1024, true)), "HCP PF");
        verify_density(render_inverse_pole_figure(make_random_data(73, 1024, true)), "HCP IPF");

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
