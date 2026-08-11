#include "texture_figures.h"

#include "ebsd_data.h"

#include <stb/stb_image_write.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <limits>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace {

constexpr int kFigureWidth = 1150;
constexpr int kFigureHeight = 380;
constexpr size_t kMaximumOrientations = 250000;

struct Vec3 {
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
};

struct Matrix3 {
    std::array<double, 9> values{};

    Vec3 multiply(const Vec3& vector) const
    {
        return {
            values[0] * vector.x + values[1] * vector.y + values[2] * vector.z,
            values[3] * vector.x + values[4] * vector.y + values[5] * vector.z,
            values[6] * vector.x + values[7] * vector.y + values[8] * vector.z,
        };
    }

    Vec3 transpose_multiply(const Vec3& vector) const
    {
        return {
            values[0] * vector.x + values[3] * vector.y + values[6] * vector.z,
            values[1] * vector.x + values[4] * vector.y + values[7] * vector.z,
            values[2] * vector.x + values[5] * vector.y + values[8] * vector.z,
        };
    }
};

struct DensityMap {
    int dimension;
    std::vector<double> values;

    explicit DensityMap(int dim)
    : dimension(dim)
    , values(static_cast<size_t>(dim) * dim, 0.0)
    {
    }

    void add(double x, double y, double weight = 1.0)
    {
        const int px = std::clamp(static_cast<int>(std::llround(x)), 0, dimension - 1);
        const int py = std::clamp(static_cast<int>(std::llround(y)), 0, dimension - 1);
        values[static_cast<size_t>(py) * dimension + px] += weight;
    }

    double at(int x, int y) const
    {
        return values[static_cast<size_t>(y) * dimension + x];
    }
};

Matrix3 bunge_matrix(float phi1, float phi, float phi2)
{
    const double c1 = std::cos(phi1);
    const double s1 = std::sin(phi1);
    const double c = std::cos(phi);
    const double s = std::sin(phi);
    const double c2 = std::cos(phi2);
    const double s2 = std::sin(phi2);
    return {{
        c1 * c2 - s1 * s2 * c,
        s1 * c2 + c1 * s2 * c,
        s2 * s,
        -c1 * s2 - s1 * c2 * c,
        -s1 * s2 + c1 * c2 * c,
        c2 * s,
        s1 * s,
        -c1 * s,
        c,
    }};
}

Vec3 normalize(Vec3 value)
{
    const double length = std::sqrt(value.x * value.x + value.y * value.y + value.z * value.z);
    if(!std::isfinite(length) || length <= 1e-12) return {};
    value.x /= length;
    value.y /= length;
    value.z /= length;
    return value;
}

std::array<std::vector<Vec3>, 3> pole_families(bool hcp)
{
    if(hcp) {
        std::array<std::vector<Vec3>, 3> families;
        families[0] = {{0, 0, 1}};
        for(int index = 0; index < 3; ++index) {
            const double angle_a = index * std::acos(-1.0) / 3.0;
            const double angle_b = angle_a + std::acos(-1.0) / 6.0;
            families[1].push_back({std::cos(angle_a), std::sin(angle_a), 0});
            families[2].push_back({std::cos(angle_b), std::sin(angle_b), 0});
        }
        return families;
    }

    return {{
        {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}},
        {{1, 1, 0}, {1, -1, 0}, {1, 0, 1}, {1, 0, -1}, {0, 1, 1}, {0, 1, -1}},
        {{1, 1, 1}, {1, 1, -1}, {1, -1, 1}, {1, -1, -1}},
    }};
}

std::array<std::string, 3> pole_labels(bool hcp)
{
    return hcp
        ? std::array<std::string, 3>{"{0001}", "{10-10}", "{11-20}"}
        : std::array<std::string, 3>{"{100}", "{110}", "{111}"};
}

size_t indexed_point_count(const EbsdData& data)
{
    if(data.primary_phase_index <= 0) return 0;
    return static_cast<size_t>(std::count_if(data.phase.begin(), data.phase.end(), [&](uint8_t phase) {
        return static_cast<int>(phase) == data.primary_phase_index;
    }));
}

template <typename Callback>
size_t for_each_orientation(const EbsdData& data, Callback callback)
{
    const size_t valid_count = indexed_point_count(data);
    if(valid_count == 0) throw std::invalid_argument("EBSD file has no indexed points in its primary phase");
    const size_t stride = std::max<size_t>(1, (valid_count + kMaximumOrientations - 1) / kMaximumOrientations);
    size_t matching_index = 0;
    size_t used_count = 0;
    for(size_t index = 0; index < data.phase.size(); ++index) {
        if(static_cast<int>(data.phase[index]) != data.primary_phase_index) continue;
        if(matching_index++ % stride != 0) continue;
        if(!std::isfinite(data.euler1[index]) || !std::isfinite(data.euler2[index]) || !std::isfinite(data.euler3[index])) continue;
        callback(bunge_matrix(data.euler1[index], data.euler2[index], data.euler3[index]));
        ++used_count;
    }
    if(used_count == 0) throw std::invalid_argument("EBSD file has no finite orientations in its primary phase");
    return used_count;
}

void gaussian_blur(DensityMap& map, double sigma)
{
    const int radius = std::max(1, static_cast<int>(std::ceil(sigma * 3.0)));
    std::vector<double> kernel(static_cast<size_t>(radius * 2 + 1));
    double kernel_sum = 0.0;
    for(int offset = -radius; offset <= radius; ++offset) {
        const double weight = std::exp(-(offset * offset) / (2.0 * sigma * sigma));
        kernel[static_cast<size_t>(offset + radius)] = weight;
        kernel_sum += weight;
    }
    for(double& weight : kernel) weight /= kernel_sum;

    std::vector<double> temporary(map.values.size(), 0.0);
    for(int y = 0; y < map.dimension; ++y) {
        for(int x = 0; x < map.dimension; ++x) {
            double value = 0.0;
            for(int offset = -radius; offset <= radius; ++offset) {
                const int source_x = std::clamp(x + offset, 0, map.dimension - 1);
                value += map.at(source_x, y) * kernel[static_cast<size_t>(offset + radius)];
            }
            temporary[static_cast<size_t>(y) * map.dimension + x] = value;
        }
    }
    for(int y = 0; y < map.dimension; ++y) {
        for(int x = 0; x < map.dimension; ++x) {
            double value = 0.0;
            for(int offset = -radius; offset <= radius; ++offset) {
                const int source_y = std::clamp(y + offset, 0, map.dimension - 1);
                value += temporary[static_cast<size_t>(source_y) * map.dimension + x] * kernel[static_cast<size_t>(offset + radius)];
            }
            map.values[static_cast<size_t>(y) * map.dimension + x] = value;
        }
    }
}

std::array<uint8_t, 3> density_color(double normalized)
{
    normalized = std::clamp(normalized, 0.0, 1.0);
    constexpr std::array<std::array<double, 4>, 6> stops = {{
        {0.00, 0, 0, 128},
        {0.18, 0, 80, 255},
        {0.38, 0, 220, 255},
        {0.58, 0, 210, 60},
        {0.78, 255, 235, 0},
        {1.00, 245, 0, 0},
    }};
    for(size_t index = 1; index < stops.size(); ++index) {
        if(normalized > stops[index][0]) continue;
        const auto& left = stops[index - 1];
        const auto& right = stops[index];
        const double ratio = (normalized - left[0]) / (right[0] - left[0]);
        return {
            static_cast<uint8_t>(std::llround(left[1] + (right[1] - left[1]) * ratio)),
            static_cast<uint8_t>(std::llround(left[2] + (right[2] - left[2]) * ratio)),
            static_cast<uint8_t>(std::llround(left[3] + (right[3] - left[3]) * ratio)),
        };
    }
    return {245, 0, 0};
}

double common_maximum(const std::array<DensityMap, 3>& maps)
{
    double maximum = 0.0;
    for(const DensityMap& map : maps) {
        maximum = std::max(maximum, *std::max_element(map.values.begin(), map.values.end()));
    }
    return std::max(maximum, std::numeric_limits<double>::epsilon());
}

const std::array<uint8_t, 7>& glyph_for(char ch)
{
    static const std::unordered_map<char, std::array<uint8_t, 7>> glyphs = {
        {'A', {0x0E, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11}}, {'B', {0x1E, 0x11, 0x11, 0x1E, 0x11, 0x11, 0x1E}},
        {'C', {0x0E, 0x11, 0x10, 0x10, 0x10, 0x11, 0x0E}}, {'D', {0x1E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x1E}},
        {'E', {0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x1F}}, {'F', {0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x10}},
        {'G', {0x0E, 0x11, 0x10, 0x17, 0x11, 0x11, 0x0F}}, {'H', {0x11, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11}},
        {'I', {0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x1F}}, {'J', {0x07, 0x02, 0x02, 0x02, 0x12, 0x12, 0x0C}},
        {'K', {0x11, 0x12, 0x14, 0x18, 0x14, 0x12, 0x11}}, {'L', {0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1F}},
        {'M', {0x11, 0x1B, 0x15, 0x15, 0x11, 0x11, 0x11}}, {'N', {0x11, 0x19, 0x15, 0x13, 0x11, 0x11, 0x11}},
        {'O', {0x0E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E}}, {'P', {0x1E, 0x11, 0x11, 0x1E, 0x10, 0x10, 0x10}},
        {'Q', {0x0E, 0x11, 0x11, 0x11, 0x15, 0x12, 0x0D}}, {'R', {0x1E, 0x11, 0x11, 0x1E, 0x14, 0x12, 0x11}},
        {'S', {0x0F, 0x10, 0x10, 0x0E, 0x01, 0x01, 0x1E}}, {'T', {0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04}},
        {'U', {0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E}}, {'V', {0x11, 0x11, 0x11, 0x11, 0x11, 0x0A, 0x04}},
        {'W', {0x11, 0x11, 0x11, 0x15, 0x15, 0x1B, 0x11}}, {'X', {0x11, 0x11, 0x0A, 0x04, 0x0A, 0x11, 0x11}},
        {'Y', {0x11, 0x11, 0x0A, 0x04, 0x04, 0x04, 0x04}}, {'Z', {0x1F, 0x01, 0x02, 0x04, 0x08, 0x10, 0x1F}},
        {'0', {0x0E, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0E}}, {'1', {0x04, 0x0C, 0x14, 0x04, 0x04, 0x04, 0x1F}},
        {'2', {0x0E, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1F}}, {'3', {0x1E, 0x01, 0x01, 0x0E, 0x01, 0x01, 0x1E}},
        {'4', {0x02, 0x06, 0x0A, 0x12, 0x1F, 0x02, 0x02}}, {'5', {0x1F, 0x10, 0x10, 0x1E, 0x01, 0x01, 0x1E}},
        {'6', {0x0E, 0x10, 0x10, 0x1E, 0x11, 0x11, 0x0E}}, {'7', {0x1F, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08}},
        {'8', {0x0E, 0x11, 0x11, 0x0E, 0x11, 0x11, 0x0E}}, {'9', {0x0E, 0x11, 0x11, 0x0F, 0x01, 0x01, 0x0E}},
        {'{', {0x06, 0x08, 0x08, 0x10, 0x08, 0x08, 0x06}}, {'}', {0x0C, 0x02, 0x02, 0x01, 0x02, 0x02, 0x0C}},
        {'[', {0x0E, 0x08, 0x08, 0x08, 0x08, 0x08, 0x0E}}, {']', {0x0E, 0x02, 0x02, 0x02, 0x02, 0x02, 0x0E}},
        {'-', {0x00, 0x00, 0x00, 0x1F, 0x00, 0x00, 0x00}}, {':', {0x00, 0x0C, 0x0C, 0x00, 0x0C, 0x0C, 0x00}},
        {' ', {0, 0, 0, 0, 0, 0, 0}},
    };
    static const std::array<uint8_t, 7> unknown = {0x1F, 0x11, 0x02, 0x04, 0x00, 0x04, 0x00};
    const auto it = glyphs.find(ch);
    return it == glyphs.end() ? unknown : it->second;
}

void set_rgb(std::vector<uint8_t>& image, int x, int y, std::array<uint8_t, 3> color)
{
    if(x < 0 || y < 0 || x >= kFigureWidth || y >= kFigureHeight) return;
    const size_t offset = (static_cast<size_t>(y) * kFigureWidth + x) * 3;
    image[offset] = color[0];
    image[offset + 1] = color[1];
    image[offset + 2] = color[2];
}

void draw_line(std::vector<uint8_t>& image, int x0, int y0, int x1, int y1, std::array<uint8_t, 3> color)
{
    const int dx = std::abs(x1 - x0);
    const int sx = x0 < x1 ? 1 : -1;
    const int dy = -std::abs(y1 - y0);
    const int sy = y0 < y1 ? 1 : -1;
    int error = dx + dy;
    while(true) {
        set_rgb(image, x0, y0, color);
        if(x0 == x1 && y0 == y1) break;
        const int twice_error = 2 * error;
        if(twice_error >= dy) { error += dy; x0 += sx; }
        if(twice_error <= dx) { error += dx; y0 += sy; }
    }
}

void draw_text(std::vector<uint8_t>& image, int x, int y, std::string text, int scale = 2)
{
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char ch) { return static_cast<char>(std::toupper(ch)); });
    int cursor = x;
    for(char ch : text) {
        const auto& glyph = glyph_for(ch);
        for(int row = 0; row < 7; ++row) {
            for(int column = 0; column < 5; ++column) {
                if((glyph[row] & (1U << (4 - column))) == 0) continue;
                for(int dy = 0; dy < scale; ++dy) {
                    for(int dx = 0; dx < scale; ++dx) set_rgb(image, cursor + column * scale + dx, y + row * scale + dy, {15, 15, 15});
                }
            }
        }
        cursor += 6 * scale;
    }
}

int text_width(const std::string& text, int scale)
{
    return text.empty() ? 0 : static_cast<int>(text.size()) * 6 * scale - scale;
}

void draw_centered_text(std::vector<uint8_t>& image, int center_x, int y, const std::string& text, int scale)
{
    draw_text(image, center_x - text_width(text, scale) / 2, y, text, scale);
}

void draw_color_bar(std::vector<uint8_t>& image, int x, int y, int width, int height)
{
    for(int py = 0; py < height; ++py) {
        const double value = 1.0 - static_cast<double>(py) / std::max(1, height - 1);
        const auto color = density_color(value);
        for(int px = 0; px < width; ++px) set_rgb(image, x + px, y + py, color);
    }
    draw_line(image, x - 1, y - 1, x + width, y - 1, {20, 20, 20});
    draw_line(image, x - 1, y + height, x + width, y + height, {20, 20, 20});
    draw_line(image, x - 1, y - 1, x - 1, y + height, {20, 20, 20});
    draw_line(image, x + width, y - 1, x + width, y + height, {20, 20, 20});
    draw_text(image, x + width + 8, y - 2, "HIGH", 1);
    draw_text(image, x + width + 8, y + height - 6, "LOW", 1);
}

std::string primary_phase_name(const EbsdData& data)
{
    for(const auto& phase : data.phases) {
        if(phase.phase_index != data.primary_phase_index) continue;
        if(!phase.material_name.empty()) return phase.material_name;
        if(!phase.structure_name.empty()) return phase.structure_name;
    }
    return "PHASE " + std::to_string(data.primary_phase_index);
}

std::string ascii_label(const std::string& value)
{
    std::string result;
    for(unsigned char ch : value) {
        if(ch >= 128) continue;
        if(std::isalnum(ch) || ch == '-' || ch == ' ') result.push_back(static_cast<char>(ch));
    }
    const auto first = result.find_first_not_of(' ');
    if(first == std::string::npos) return {};
    const auto last = result.find_last_not_of(' ');
    return result.substr(first, last - first + 1);
}

void draw_info_box(std::vector<uint8_t>& image, const EbsdData& data, size_t points)
{
    constexpr int x = 1020;
    constexpr int y = 92;
    constexpr int width = 116;
    constexpr int height = 128;
    draw_line(image, x, y, x + width, y, {90, 90, 90});
    draw_line(image, x, y + height, x + width, y + height, {90, 90, 90});
    draw_line(image, x, y, x, y + height, {90, 90, 90});
    draw_line(image, x + width, y, x + width, y + height, {90, 90, 90});
    draw_text(image, x + 7, y + 8, "PHASE", 1);
    std::string phase_name = ascii_label(primary_phase_name(data));
    if(phase_name.empty()) phase_name = "PHASE " + std::to_string(data.primary_phase_index);
    if(phase_name.size() > 16) phase_name.resize(16);
    draw_text(image, x + 7, y + 24, phase_name, 1);
    draw_text(image, x + 7, y + 48, "POINTS", 1);
    draw_text(image, x + 7, y + 64, std::to_string(points), 1);
    draw_text(image, x + 7, y + 88, data.hcp ? "HEXAGONAL" : "CUBIC", 1);
}

void write_png(const std::filesystem::path& path, const std::vector<uint8_t>& image)
{
    if(!path.parent_path().empty()) std::filesystem::create_directories(path.parent_path());
    if(stbi_write_png(path.string().c_str(), kFigureWidth, kFigureHeight, 3, image.data(), kFigureWidth * 3) == 0) {
        throw std::runtime_error("Failed to write texture figure PNG");
    }
}

std::pair<double, double> cubic_triangle_coordinates(Vec3 direction)
{
    direction = normalize(direction);
    std::array<double, 3> components = {std::fabs(direction.x), std::fabs(direction.y), std::fabs(direction.z)};
    std::sort(components.begin(), components.end(), std::greater<>());
    const double z = components[0];
    const double x = components[1];
    const double y = components[2];
    const double denominator = 1.0 + z;
    const double stereographic_x = x / denominator;
    const double stereographic_y = y / denominator;
    constexpr double b_x = 0.4142135623730951;
    constexpr double c_x = 0.3660254037844386;
    constexpr double c_y = 0.3660254037844386;
    const double weight_c = stereographic_y / c_y;
    const double weight_b = (stereographic_x - weight_c * c_x) / b_x;
    return {weight_b, weight_c};
}

std::pair<double, double> hcp_triangle_coordinates(Vec3 direction)
{
    direction = normalize(direction);
    const double z = std::fabs(direction.z);
    double theta = std::atan2(direction.y, direction.x);
    const double sector = std::acos(-1.0) / 3.0;
    theta = std::fmod(theta, sector);
    if(theta < 0.0) theta += sector;
    if(theta > sector * 0.5) theta = sector - theta;
    const double basal = std::sqrt(std::max(0.0, 1.0 - z * z));
    const double radius = std::tan(std::atan2(basal, z) * 0.5);
    const double projected_x = radius * std::cos(theta);
    const double projected_y = radius * std::sin(theta);
    const double weight_c = projected_y / 0.5;
    const double weight_b = projected_x - std::sqrt(3.0) * 0.5 * weight_c;
    return {weight_b, weight_c};
}

} // namespace

void save_pole_figure(const std::filesystem::path& ebsd_path, const std::filesystem::path& img_path)
{
    const EbsdData data = load_ebsd_data(ebsd_path);
    constexpr int dimension = 250;
    std::array<DensityMap, 3> maps = {DensityMap(dimension), DensityMap(dimension), DensityMap(dimension)};
    const auto families = pole_families(data.hcp);
    for_each_orientation(data, [&](const Matrix3& orientation) {
        for(size_t family_index = 0; family_index < families.size(); ++family_index) {
            for(Vec3 pole : families[family_index]) {
                pole = normalize(orientation.transpose_multiply(normalize(pole)));
                if(pole.z < 0.0) pole = {-pole.x, -pole.y, -pole.z};
                const double projection_scale = std::sqrt(std::max(1e-12, 1.0 + pole.z));
                const double projected_x = pole.x / projection_scale;
                const double projected_y = pole.y / projection_scale;
                if(projected_x * projected_x + projected_y * projected_y > 1.000001) continue;
                maps[family_index].add(
                    (projected_x + 1.0) * 0.5 * (dimension - 1),
                    (1.0 - projected_y) * 0.5 * (dimension - 1),
                    1.0 / static_cast<double>(families[family_index].size())
                );
            }
        }
    });
    for(DensityMap& map : maps) gaussian_blur(map, 4.5);
    const double maximum = common_maximum(maps);
    std::vector<uint8_t> image(static_cast<size_t>(kFigureWidth) * kFigureHeight * 3, 255);
    draw_centered_text(image, 515, 20, "POLE FIGURE", 4);

    constexpr std::array<int, 3> origins_x = {55, 350, 645};
    constexpr int origin_y = 88;
    const auto labels = pole_labels(data.hcp);
    for(size_t map_index = 0; map_index < maps.size(); ++map_index) {
        draw_centered_text(image, origins_x[map_index] + dimension / 2, 62, labels[map_index], 2);
        for(int y = 0; y < dimension; ++y) {
            for(int x = 0; x < dimension; ++x) {
                const double nx = (x - (dimension - 1) * 0.5) / ((dimension - 1) * 0.5);
                const double ny = (y - (dimension - 1) * 0.5) / ((dimension - 1) * 0.5);
                if(nx * nx + ny * ny > 1.0) continue;
                set_rgb(image, origins_x[map_index] + x, origin_y + y, density_color(maps[map_index].at(x, y) / maximum));
            }
        }
        const int cx = origins_x[map_index] + dimension / 2;
        const int cy = origin_y + dimension / 2;
        const int radius = dimension / 2;
        for(int angle_index = 0; angle_index < 1440; ++angle_index) {
            const double angle = angle_index * 2.0 * std::acos(-1.0) / 1440.0;
            set_rgb(image, cx + static_cast<int>(std::llround(radius * std::cos(angle))), cy + static_cast<int>(std::llround(radius * std::sin(angle))), {20, 20, 20});
        }
        draw_line(image, origins_x[map_index], cy, origins_x[map_index] + dimension - 1, cy, {35, 35, 35});
        draw_line(image, cx, origin_y, cx, origin_y + dimension - 1, {35, 35, 35});
    }
    draw_color_bar(image, 945, 105, 28, 210);
    draw_info_box(image, data, indexed_point_count(data));
    write_png(img_path, image);
}

void save_inverse_pole_figure(const std::filesystem::path& ebsd_path, const std::filesystem::path& img_path)
{
    const EbsdData data = load_ebsd_data(ebsd_path);
    constexpr int dimension = 240;
    std::array<DensityMap, 3> maps = {DensityMap(dimension), DensityMap(dimension), DensityMap(dimension)};
    constexpr std::array<Vec3, 3> sample_directions = {Vec3{1, 0, 0}, Vec3{0, 1, 0}, Vec3{0, 0, 1}};
    for_each_orientation(data, [&](const Matrix3& orientation) {
        for(size_t direction_index = 0; direction_index < sample_directions.size(); ++direction_index) {
            const Vec3 crystal_direction = orientation.multiply(sample_directions[direction_index]);
            const auto [weight_b, weight_c] = data.hcp
                ? hcp_triangle_coordinates(crystal_direction)
                : cubic_triangle_coordinates(crystal_direction);
            const double weight_a = 1.0 - weight_b - weight_c;
            if(weight_a < -1e-6 || weight_b < -1e-6 || weight_c < -1e-6) continue;
            maps[direction_index].add(weight_c * (dimension - 1), (weight_b + weight_c) * (dimension - 1));
        }
    });
    for(DensityMap& map : maps) gaussian_blur(map, 5.0);
    const double maximum = common_maximum(maps);
    std::vector<uint8_t> image(static_cast<size_t>(kFigureWidth) * kFigureHeight * 3, 255);
    draw_centered_text(image, 515, 20, "INVERSE POLE FIGURE", 4);

    constexpr std::array<int, 3> origins_x = {65, 355, 645};
    constexpr int origin_y = 88;
    constexpr std::array<const char*, 3> direction_labels = {"X", "Y", "Z"};
    for(size_t map_index = 0; map_index < maps.size(); ++map_index) {
        draw_centered_text(image, origins_x[map_index] + dimension / 2, 62, direction_labels[map_index], 2);
        for(int y = 0; y < dimension; ++y) {
            for(int x = 0; x <= y; ++x) {
                set_rgb(image, origins_x[map_index] + x, origin_y + y, density_color(maps[map_index].at(x, y) / maximum));
            }
        }
        draw_line(image, origins_x[map_index], origin_y, origins_x[map_index], origin_y + dimension - 1, {20, 20, 20});
        draw_line(image, origins_x[map_index], origin_y + dimension - 1, origins_x[map_index] + dimension - 1, origin_y + dimension - 1, {20, 20, 20});
        draw_line(image, origins_x[map_index], origin_y, origins_x[map_index] + dimension - 1, origin_y + dimension - 1, {20, 20, 20});
        draw_text(image, origins_x[map_index], origin_y - 13, data.hcp ? "[0001]" : "[001]", 1);
        draw_text(image, origins_x[map_index], origin_y + dimension + 5, data.hcp ? "[10-10]" : "[101]", 1);
        const std::string right_label = data.hcp ? "[11-20]" : "[111]";
        draw_text(image, origins_x[map_index] + dimension - text_width(right_label, 1), origin_y + dimension + 5, right_label, 1);
    }
    draw_color_bar(image, 945, 105, 28, 210);
    draw_info_box(image, data, indexed_point_count(data));
    write_png(img_path, image);
}
