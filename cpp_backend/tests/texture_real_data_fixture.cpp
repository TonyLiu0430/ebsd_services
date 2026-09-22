#include "ebsd_data.h"
#include "texture_figures.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb/stb_image_write.h>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {

void write_png(const std::filesystem::path& path, const TextureFigure& figure)
{
    if(stbi_write_png(path.string().c_str(), figure.width, figure.height, 3, figure.rgb.data(), figure.width * 3) == 0) {
        throw std::runtime_error("Could not write real-data texture PNG: " + path.string());
    }
}

void write_density_csv(const std::filesystem::path& path, const TextureFigure& figure)
{
    std::ofstream stream(path);
    stream.precision(17);
    stream << figure.density_min << ',' << figure.density_max << '\n';
    for(size_t panel = 0; panel < figure.densities.size(); ++panel) {
        const auto& map = figure.densities[panel];
        stream << "panel," << panel << ',' << map.width << ',' << map.height << '\n';
        for(size_t index = 0; index < map.values.size(); ++index) {
            if(index != 0) stream << ',';
            stream << (map.mask[index] ? map.values[index] : -1.0);
        }
        stream << '\n';
    }
}

} // namespace

int main(int argc, char** argv)
{
    if(argc != 3) {
        std::cerr << "usage: texture_real_data_fixture EBSD_FILE OUTPUT_DIR\n";
        return 2;
    }
    try {
        const EbsdData data = load_ebsd_data(argv[1]);
        const std::filesystem::path output(argv[2]);
        std::filesystem::create_directories(output);
        const TextureFigure pole = render_pole_figure(data);
        const TextureFigure inverse = render_inverse_pole_figure(data);
        write_png(output / "pole.png", pole);
        write_png(output / "inverse.png", inverse);
        write_density_csv(output / "pole-density.csv", pole);
        write_density_csv(output / "inverse-density.csv", inverse);
    } catch(const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}

