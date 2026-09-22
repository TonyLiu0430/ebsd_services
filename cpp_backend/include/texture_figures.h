#pragma once

#include "ebsd_data.h"

#include <array>
#include <cstdint>
#include <filesystem>
#include <vector>

// Kept public so the parity test can exercise the numerical core without
// having to manufacture an EBSD file.
struct TextureDensityMap {
    int width = 0;
    int height = 0;
    std::vector<double> values;
    std::vector<uint8_t> mask;
};

struct TextureFigure {
    int width = 0;
    int height = 0;
    std::vector<uint8_t> rgb;
    std::array<TextureDensityMap, 3> densities;
    double density_min = 0.0;
    double density_max = 0.0;
};

TextureFigure render_pole_figure(const EbsdData& data);
TextureFigure render_inverse_pole_figure(const EbsdData& data);

void save_pole_figure(
    const std::filesystem::path& ebsd_path,
    const std::filesystem::path& image_path
);
void save_inverse_pole_figure(
    const std::filesystem::path& ebsd_path,
    const std::filesystem::path& image_path
);

