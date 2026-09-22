#include "texture_figures.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb/stb_image_write.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>

EbsdData load_ebsd_data(const std::filesystem::path&, bool)
{
    throw std::runtime_error("The parity fixture accepts Euler angles directly");
}

namespace {

EbsdData read_euler_csv(const std::filesystem::path& path)
{
    std::ifstream stream(path);
    if(!stream) throw std::runtime_error("Could not open Euler CSV: " + path.string());
    EbsdData data;
    data.primary_phase_index = 1;
    std::string line;
    if(!std::getline(stream, line)) throw std::runtime_error("Euler CSV is empty");
    data.hcp = line == "hcp";
    while(std::getline(stream, line)) {
        if(line.empty()) continue;
        std::replace(line.begin(), line.end(), ',', ' ');
        std::istringstream row(line);
        float phi1 = 0.0F, phi = 0.0F, phi2 = 0.0F;
        if(!(row >> phi1 >> phi >> phi2)) throw std::runtime_error("Invalid Euler CSV row");
        data.euler1.push_back(phi1);
        data.euler2.push_back(phi);
        data.euler3.push_back(phi2);
        data.phase.push_back(1);
    }
    if(data.phase.empty()) throw std::runtime_error("Euler CSV has no orientations");
    data.x_dim = static_cast<int>(data.phase.size());
    data.y_dim = 1;
    data.x_step = data.y_step = 1.0;
    return data;
}

void write_png(const std::filesystem::path& path, const TextureFigure& figure)
{
    if(stbi_write_png(path.string().c_str(), figure.width, figure.height, 3, figure.rgb.data(), figure.width * 3) == 0) {
        throw std::runtime_error("Could not write parity PNG: " + path.string());
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
        std::cerr << "usage: texture_parity_fixture EULER_CSV OUTPUT_DIR\n";
        return 2;
    }
    try {
        const EbsdData data = read_euler_csv(argv[1]);
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
