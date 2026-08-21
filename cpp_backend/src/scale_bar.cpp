#include "scale_bar.h"

#include <stb/stb_image_write.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <stdexcept>
#include <string>
#include <unordered_map>

namespace {

constexpr int scale_bar_grid_points = 100;

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
        {';', {0x00, 0x00, 0x00, 0x0C, 0x0C, 0x04, 0x08}},
        {'=', {0x00, 0x1F, 0x00, 0x1F, 0x00, 0x00, 0x00}},
        {'B', {0x1E, 0x11, 0x11, 0x1E, 0x11, 0x11, 0x1E}},
        {'C', {0x0E, 0x11, 0x10, 0x10, 0x10, 0x11, 0x0E}},
        {'F', {0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x10}},
        {'G', {0x0E, 0x11, 0x10, 0x17, 0x11, 0x11, 0x0F}},
        {'I', {0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x1F}},
        {'O', {0x0E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E}},
        {'P', {0x1E, 0x11, 0x11, 0x1E, 0x10, 0x10, 0x10}},
        {'R', {0x1E, 0x11, 0x11, 0x1E, 0x14, 0x12, 0x11}},
        {'S', {0x0F, 0x10, 0x10, 0x0E, 0x01, 0x01, 0x1E}},
        {'d', {0x01, 0x01, 0x0F, 0x11, 0x11, 0x13, 0x0D}},
        {'e', {0x00, 0x00, 0x0E, 0x11, 0x1F, 0x10, 0x0F}},
        {'i', {0x04, 0x00, 0x0C, 0x04, 0x04, 0x04, 0x0E}},
        // Render the ASCII fallback in "um" with the shape of a micro sign.
        {'u', {0x00, 0x00, 0x11, 0x11, 0x11, 0x1F, 0x10}},
        {'m', {0x00, 0x00, 0x1A, 0x15, 0x15, 0x15, 0x15}},
        {'p', {0x00, 0x00, 0x1E, 0x11, 0x1E, 0x10, 0x10}},
        {'r', {0x00, 0x00, 0x16, 0x19, 0x10, 0x10, 0x10}},
        {'t', {0x04, 0x04, 0x1F, 0x04, 0x04, 0x04, 0x03}},
        {'x', {0x00, 0x00, 0x11, 0x0A, 0x04, 0x0A, 0x11}},
        {' ', {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}},
    };
    static const std::array<uint8_t, 7> unknown = {0x1F, 0x11, 0x02, 0x04, 0x00, 0x04, 0x00};
    const auto it = glyphs.find(ch);
    return it == glyphs.end() ? unknown : it->second;
}

void set_pixel(RasterImage& image, int x, int y, uint8_t value)
{
    if(x < 0 || y < 0 || x >= image.width || y >= image.height) return;
    const size_t start = (static_cast<size_t>(y) * image.width + x) * image.channels;
    for(int channel = 0; channel < image.channels; ++channel) {
        image.pixels[start + channel] = value;
    }
}

void fill_rect(RasterImage& image, int x, int y, int width, int height, uint8_t value)
{
    for(int py = y; py < y + height; ++py) {
        for(int px = x; px < x + width; ++px) {
            set_pixel(image, px, py, value);
        }
    }
}

void draw_text(RasterImage& image, int x, int y, const std::string& text, int pixel_scale)
{
    int cursor = x;
    for(char ch : text) {
        const auto& glyph = glyph_for(ch);
        for(int row = 0; row < 7; ++row) {
            for(int col = 0; col < 5; ++col) {
                if((glyph[row] & (1U << (4 - col))) == 0) continue;
                fill_rect(
                    image,
                    cursor + col * pixel_scale,
                    y + row * pixel_scale,
                    pixel_scale,
                    pixel_scale,
                    20
                );
            }
        }
        cursor += 6 * pixel_scale;
    }
}

std::string format_micrometers(double micrometers)
{
    if(micrometers >= 10.0 || std::fabs(micrometers - std::round(micrometers)) < 1e-6) {
        return std::to_string(static_cast<long long>(std::llround(micrometers))) + " um";
    }
    std::string value = std::to_string(micrometers);
    value.erase(value.find_last_not_of('0') + 1);
    if(!value.empty() && value.back() == '.') value.pop_back();
    return value + " um";
}

std::string footer_label(
    const std::string& map_name,
    double x_step_um,
    int grid_width,
    int grid_height
)
{
    return format_micrometers(x_step_um * scale_bar_grid_points)
        + "; " + map_name + "; Step=" + format_micrometers(x_step_um)
        + "; Grid" + std::to_string(grid_width) + "x" + std::to_string(grid_height);
}

void append_png_bytes(void* context, void* data, int size)
{
    auto& output = *static_cast<std::string*>(context);
    output.append(static_cast<const char*>(data), static_cast<size_t>(size));
}

void validate_raster(const std::vector<uint8_t>& pixels, int width, int height, int channels)
{
    if(width <= 0 || height <= 0) throw std::invalid_argument("Image dimensions must be positive");
    if(channels != 1 && channels != 3) throw std::invalid_argument("Image must have one or three channels");
    const size_t expected = static_cast<size_t>(width) * height * channels;
    if(pixels.size() != expected) throw std::invalid_argument("Image byte count does not match its dimensions");
}

} // namespace

int map_upscale(int grid_width, int grid_height)
{
    constexpr int minimum_output_dimension = 900;
    const int shortest_dimension = std::max(1, std::min(grid_width, grid_height));
    return std::clamp(
        static_cast<int>(std::ceil(static_cast<double>(minimum_output_dimension) / shortest_dimension)),
        1,
        8
    );
}

RasterImage upscale_nearest(
    const std::vector<uint8_t>& pixels,
    int width,
    int height,
    int channels,
    int upscale
)
{
    validate_raster(pixels, width, height, channels);
    if(upscale <= 0) throw std::invalid_argument("Image upscale must be positive");

    RasterImage image;
    image.width = width * upscale;
    image.height = height * upscale;
    image.channels = channels;
    image.pixels.resize(static_cast<size_t>(image.width) * image.height * channels);
    for(int source_y = 0; source_y < height; ++source_y) {
        for(int source_x = 0; source_x < width; ++source_x) {
            const size_t source = (static_cast<size_t>(source_y) * width + source_x) * channels;
            for(int dy = 0; dy < upscale; ++dy) {
                for(int dx = 0; dx < upscale; ++dx) {
                    const int target_x = source_x * upscale + dx;
                    const int target_y = source_y * upscale + dy;
                    const size_t target = (static_cast<size_t>(target_y) * image.width + target_x) * channels;
                    for(int channel = 0; channel < channels; ++channel) {
                        image.pixels[target + channel] = pixels[source + channel];
                    }
                }
            }
        }
    }
    return image;
}

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
)
{
    validate_raster(map_pixels, map_width, map_height, channels);
    if(!std::isfinite(x_step_um) || x_step_um <= 0.0) {
        throw std::invalid_argument("x_step_um must be positive");
    }
    if(grid_width <= 0 || grid_height <= 0) throw std::invalid_argument("Grid dimensions must be positive");
    if(pixels_per_grid_point <= 0) throw std::invalid_argument("pixels_per_grid_point must be positive");

    const int footer_height = 8 * pixels_per_grid_point;
    RasterImage output;
    output.width = map_width;
    output.height = map_height + footer_height;
    output.channels = channels;
    output.pixels.assign(static_cast<size_t>(output.width) * output.height * channels, 255);

    const size_t row_bytes = static_cast<size_t>(map_width) * channels;
    for(int y = 0; y < map_height; ++y) {
        std::copy_n(
            map_pixels.data() + static_cast<size_t>(y) * row_bytes,
            row_bytes,
            output.pixels.data() + static_cast<size_t>(y) * row_bytes
        );
    }

    const int bar_x = pixels_per_grid_point;
    const int bar_y = map_height + pixels_per_grid_point;
    const int bar_width = scale_bar_grid_points * pixels_per_grid_point;
    const int bar_height = 6 * pixels_per_grid_point;
    fill_rect(output, bar_x, bar_y, bar_width, bar_height, 20);

    const int text_scale = std::clamp((pixels_per_grid_point + 1) / 2, 1, 2);
    const int text_height = 7 * text_scale;
    const int text_x = bar_x + bar_width + 2 * pixels_per_grid_point;
    const int text_y = map_height + std::max(0, (footer_height - text_height) / 2);
    draw_text(output, text_x, text_y, footer_label(map_name, x_step_um, grid_width, grid_height), text_scale);
    return output;
}

std::string encode_png(const RasterImage& image)
{
    validate_raster(image.pixels, image.width, image.height, image.channels);
    std::string output;
    if(stbi_write_png_to_func(
        append_png_bytes,
        &output,
        image.width,
        image.height,
        image.channels,
        image.pixels.data(),
        image.width * image.channels
    ) == 0) {
        throw std::runtime_error("Failed to encode PNG");
    }
    return output;
}
