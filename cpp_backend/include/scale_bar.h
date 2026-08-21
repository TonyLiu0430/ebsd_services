#pragma once

#include <cstdint>
#include <string>
#include <vector>

struct RasterImage {
    int width = 0;
    int height = 0;
    int channels = 0;
    std::vector<uint8_t> pixels;
};

int map_upscale(int grid_width, int grid_height);

RasterImage upscale_nearest(
    const std::vector<uint8_t>& pixels,
    int width,
    int height,
    int channels,
    int upscale
);

RasterImage add_scale_bar_footer(
    const std::vector<uint8_t>& map_pixels,
    int map_width,
    int map_height,
    int channels,
    double x_step_um,
    int grid_width,
    int grid_height,
    const std::string& map_name,
    int pixels_per_grid_point
);

std::string encode_png(const RasterImage& image);
