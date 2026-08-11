#include "band_contrast_map.h"
#include "ebsd_data.h"

#include <stb/stb_image_write.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

namespace {

const std::array<uint8_t, 7>& glyph_for(char ch)
{
    static const std::unordered_map<char, std::array<uint8_t, 7>> glyphs = {
        {'0', {0x0E, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0E}},
        {'1', {0x04, 0x0C, 0x14, 0x04, 0x04, 0x04, 0x1F}},
        {'2', {0x0E, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1F}},
        {'3', {0x1E, 0x01, 0x01, 0x0E, 0x01, 0x01, 0x1E}},
        {'4', {0x02, 0x06, 0x0A, 0x12, 0x1F, 0x02, 0x02}},
        {'5', {0x1F, 0x10, 0x10, 0x1E, 0x01, 0x01, 0x1E}},
        {'6', {0x0E, 0x10, 0x10, 0x1E, 0x11, 0x11, 0x0E}},
        {'7', {0x1F, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08}},
        {'8', {0x0E, 0x11, 0x11, 0x0E, 0x11, 0x11, 0x0E}},
        {'9', {0x0E, 0x11, 0x11, 0x0F, 0x01, 0x01, 0x0E}},
        {'.', {0x00, 0x00, 0x00, 0x00, 0x00, 0x0C, 0x0C}},
        {'u', {0x00, 0x00, 0x11, 0x11, 0x11, 0x13, 0x0D}},
        {'m', {0x00, 0x00, 0x1A, 0x15, 0x15, 0x15, 0x15}},
        {' ', {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}},
    };
    static const std::array<uint8_t, 7> unknown = {0x1F, 0x11, 0x02, 0x04, 0x00, 0x04, 0x00};
    const auto it = glyphs.find(ch);
    return it == glyphs.end() ? unknown : it->second;
}

void set_pixel(std::vector<uint8_t>& image, int width, int height, int x, int y, uint8_t value)
{
    if(x < 0 || y < 0 || x >= width || y >= height) return;
    image[static_cast<size_t>(y) * width + x] = value;
}

void fill_rect(std::vector<uint8_t>& image, int width, int height, int x, int y, int rect_width, int rect_height, uint8_t value)
{
    for(int py = y; py < y + rect_height; ++py) {
        for(int px = x; px < x + rect_width; ++px) {
            set_pixel(image, width, height, px, py, value);
        }
    }
}

void draw_text(std::vector<uint8_t>& image, int width, int height, int x, int y, const std::string& text, int pixel_scale)
{
    int cursor = x;
    for(char ch : text) {
        const auto& glyph = glyph_for(ch);
        for(int row = 0; row < 7; ++row) {
            for(int col = 0; col < 5; ++col) {
                if((glyph[row] & (1U << (4 - col))) == 0) continue;
                fill_rect(image, width, height, cursor + col * pixel_scale, y + row * pixel_scale, pixel_scale, pixel_scale, 20);
            }
        }
        cursor += 6 * pixel_scale;
    }
}

double nice_scale_length(double maximum)
{
    if(!std::isfinite(maximum) || maximum <= 0.0) return 0.0;
    const double exponent = std::pow(10.0, std::floor(std::log10(maximum)));
    const double normalized = maximum / exponent;
    const double multiplier = normalized >= 5.0 ? 5.0 : normalized >= 2.0 ? 2.0 : 1.0;
    return multiplier * exponent;
}

std::string format_scale_length(double micrometers)
{
    if(micrometers >= 10.0 || std::fabs(micrometers - std::round(micrometers)) < 1e-6) {
        return std::to_string(static_cast<long long>(std::llround(micrometers))) + " um";
    }
    std::string value = std::to_string(micrometers);
    value.erase(value.find_last_not_of('0') + 1);
    if(!value.empty() && value.back() == '.') value.pop_back();
    return value + " um";
}

} // namespace

void save_band_contrast_map(const std::filesystem::path& ebsd_path, const std::filesystem::path& img_path, bool include_scale_bar)
{
    const EbsdData data = load_ebsd_data(ebsd_path);
    const int x_dim = data.x_dim;
    const int y_dim = data.y_dim;
    const size_t point_count = static_cast<size_t>(x_dim) * y_dim;
    if(data.band_contrast.size() != point_count) {
        throw std::invalid_argument("Band contrast data is not available for " + ebsd_path.extension().string() + " files");
    }

    constexpr int minimum_output_dimension = 900;
    const int shortest_dimension = std::max(1, std::min(x_dim, y_dim));
    const int upscale = std::clamp(
        static_cast<int>(std::ceil(static_cast<double>(minimum_output_dimension) / shortest_dimension)),
        1,
        8
    );
    const int map_width = x_dim * upscale;
    const int map_height = y_dim * upscale;
    const int scale_region_height = include_scale_bar ? std::max(76, 24 * upscale) : 0;
    const int output_height = map_height + scale_region_height;
    std::vector<uint8_t> image(static_cast<size_t>(map_width) * output_height, 255);

    for(int source_y = 0; source_y < y_dim; ++source_y) {
        for(int source_x = 0; source_x < x_dim; ++source_x) {
            const uint8_t value = data.band_contrast[static_cast<size_t>(source_y) * x_dim + source_x];
            fill_rect(image, map_width, output_height, source_x * upscale, source_y * upscale, upscale, upscale, value);
        }
    }

    if(include_scale_bar) {
        const double map_width_um = data.x_step * x_dim;
        const double bar_um = nice_scale_length(map_width_um * 0.28);
        if(bar_um > 0.0) {
            const int bar_width = std::clamp(
                static_cast<int>(std::llround((bar_um / data.x_step) * upscale)),
                1,
                std::max(1, map_width - 40)
            );
            const int bar_height = std::max(3, upscale);
            const int tick_height = std::max(10, 4 * upscale);
            const int bar_x = (map_width - bar_width) / 2;
            const int bar_y = map_height + std::max(14, 5 * upscale);
            fill_rect(image, map_width, output_height, bar_x, bar_y, bar_width, bar_height, 20);
            fill_rect(image, map_width, output_height, bar_x, bar_y - tick_height / 2, bar_height, tick_height, 20);
            fill_rect(image, map_width, output_height, bar_x + bar_width - bar_height, bar_y - tick_height / 2, bar_height, tick_height, 20);

            const std::string label = format_scale_length(bar_um);
            const int text_scale = std::max(2, std::min(4, upscale));
            const int text_width = static_cast<int>(label.size()) * 6 * text_scale - text_scale;
            draw_text(image, map_width, output_height, std::max(0, (map_width - text_width) / 2), bar_y + tick_height, label, text_scale);
        }
    }

    if(!img_path.parent_path().empty()) std::filesystem::create_directories(img_path.parent_path());
    if(stbi_write_png(img_path.string().c_str(), map_width, output_height, 1, image.data(), map_width) == 0) {
        throw std::runtime_error("Failed to write band contrast PNG");
    }
}
