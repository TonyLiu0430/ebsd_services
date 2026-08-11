// #include <EbsdLib/IO/EbsdImporter.h>
#include "ebsd_data.h"
// #include <vector>
// #include <memory>
// #include <EbsdLib/Core/OrientationTransformation.hpp>
// #include <EbsdLib/LaueOps/LaueOps.h>
// #include <algorithm>
// #include <ranges>
// #define STB_IMAGE_WRITE_IMPLEMENTATION
// #include <stb/stb_image_write.h>
// #include <map>
#include <EbsdLib/Core/OrientationTransformation.hpp>
#include "EbsdLib/Math/Matrix3X1.hpp"
#include "EbsdLib/Core/Orientation.hpp"
#include <EbsdLib/LaueOps/LaueOps.h>
#include "utils.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>
#include <memory>
#include <map>
#ifndef STB_IMAGE_WRITE_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION
#endif
#include <stb/stb_image_write.h>

using std::vector;
using std::pair;
using std::swap;

void save_ipf_legend_for_ops(const std::filesystem::path& img_path, int dim, LaueOps::Pointer ops, std::array<double, 3> dirA, std::array<double, 3> dirB, std::array<double, 3> dirC);

namespace {

std::array<double, 3> normalize_ref_dir(std::array<double, 3> ref_dir) {
    const double norm = std::sqrt(ref_dir[0] * ref_dir[0] + ref_dir[1] * ref_dir[1] + ref_dir[2] * ref_dir[2]);
    if(!std::isfinite(norm) || norm <= 1e-12) {
        throw std::invalid_argument("IPF reference direction vector must not be zero");
    }
    return {ref_dir[0] / norm, ref_dir[1] / norm, ref_dir[2] / norm};
}

template <typename T>
bool is_hcp_lattice(const std::vector<T>& lattice)
{
    if(lattice.size() < 6) return false;
    const double a = lattice[0];
    const double b = lattice[1];
    const double alpha = lattice[3];
    const double beta = lattice[4];
    const double gamma = lattice[5];
    if(a <= 0.0 || b <= 0.0) return false;
    const double abRelDiff = std::fabs(a - b) / ((a + b) * 0.5);
    return abRelDiff <= 0.03
        && std::fabs(alpha - 90.0) <= 3.0
        && std::fabs(beta - 90.0) <= 3.0
        && std::fabs(gamma - 120.0) <= 3.0;
}

struct HcpSectorPoint {
    double x = 0.0;
    double y = 0.0;
    double top = 0.0;
    double left = 0.0;
    double right = 0.0;
};

std::tuple<uint8_t, uint8_t, uint8_t> hsv_to_rgb(double h, double s, double v)
{
    h = h - std::floor(h);
    s = std::clamp(s, 0.0, 1.0);
    v = std::clamp(v, 0.0, 1.0);
    const double c = v * s;
    const double hp = h * 6.0;
    const double x = c * (1.0 - std::fabs(std::fmod(hp, 2.0) - 1.0));
    double r1 = 0.0, g1 = 0.0, b1 = 0.0;
    if(hp < 1.0) { r1 = c; g1 = x; }
    else if(hp < 2.0) { r1 = x; g1 = c; }
    else if(hp < 3.0) { g1 = c; b1 = x; }
    else if(hp < 4.0) { g1 = x; b1 = c; }
    else if(hp < 5.0) { r1 = x; b1 = c; }
    else { r1 = c; b1 = x; }
    const double m = v - c;
    return {
        static_cast<uint8_t>(std::clamp((r1 + m) * 255.0, 0.0, 255.0)),
        static_cast<uint8_t>(std::clamp((g1 + m) * 255.0, 0.0, 255.0)),
        static_cast<uint8_t>(std::clamp((b1 + m) * 255.0, 0.0, 255.0)),
    };
}

HcpSectorPoint hcp_project_to_mtex_sector(EbsdLib::Matrix3X1D p)
{
    p = p.normalize();
    const double z = std::fabs(p[2]);
    double theta = std::atan2(p[1], p[0]);
    const double sector = EbsdLib::Constants::k_PiD / 3.0;
    theta = std::fmod(theta, sector);
    if(theta < 0.0) theta += sector;
    if(theta > sector * 0.5) theta = sector - theta;

    const double basal = std::sqrt(std::max(0.0, 1.0 - z * z));
    const double polar = std::atan2(basal, z);
    const double radius = std::tan(polar * 0.5);
    const double px = radius * std::cos(theta);
    const double py = radius * std::sin(theta);

    const double xTop = 0.0;
    const double yTop = 0.0;
    const double xLeft = 1.0;
    const double yLeft = 0.0;
    const double xRight = std::sqrt(3.0) * 0.5;
    const double yRight = 0.5;
    const double denom = (yLeft - yRight) * (xTop - xRight) + (xRight - xLeft) * (yTop - yRight);
    const double top = ((yLeft - yRight) * (px - xRight) + (xRight - xLeft) * (py - yRight)) / denom;
    const double left = ((yRight - yTop) * (px - xRight) + (xTop - xRight) * (py - yRight)) / denom;
    const double right = 1.0 - top - left;
    return {px, py, top, left, right};
}

std::tuple<uint8_t, uint8_t, uint8_t> hcp_mtex_hsv_color(const HcpSectorPoint& point)
{
    const EbsdLib::Matrix3X1D centerDir(
        1.0 + std::sqrt(3.0) * 0.5,
        0.5,
        1.0
    );
    const HcpSectorPoint center = hcp_project_to_mtex_sector(centerDir);

    const double wTop = std::clamp(point.top, 0.0, 1.0);
    const double wLeft = std::clamp(point.left, 0.0, 1.0);
    const double wRight = std::clamp(point.right, 0.0, 1.0);

    const double hueX = wTop - 0.5 * wLeft - 0.5 * wRight;
    const double hueY = (std::sqrt(3.0) * 0.5) * (wLeft - wRight);
    double hue = std::atan2(hueY, hueX) / (2.0 * EbsdLib::Constants::k_PiD);
    if(hue < 0.0) hue += 1.0;

    double tBoundary = std::numeric_limits<double>::infinity();
    const std::array<double, 3> pWeights = {wTop, wLeft, wRight};
    const std::array<double, 3> cWeights = {center.top, center.left, center.right};
    for(size_t i = 0; i < pWeights.size(); ++i) {
        if(pWeights[i] < cWeights[i] - 1e-12) {
            tBoundary = std::min(tBoundary, cWeights[i] / (cWeights[i] - pWeights[i]));
        }
    }
    double saturation = std::isfinite(tBoundary) && tBoundary > 1e-12 ? 1.0 / tBoundary : 1.0;
    saturation = std::pow(std::clamp(saturation, 0.0, 1.0), 0.85);

    return hsv_to_rgb(hue, saturation, 1.0);
}

std::tuple<uint8_t, uint8_t, uint8_t> hcp_ipf_color_from_direction(EbsdLib::Matrix3X1D p)
{
    return hcp_mtex_hsv_color(hcp_project_to_mtex_sector(p));
}

void save_hcp_ipf_legend(const std::filesystem::path& img_path, int dim)
{
    const double margin = dim * 0.08;
    const std::array<double, 2> vLeft = {margin, dim - margin};
    const std::array<double, 2> vRight = {dim - margin, dim - margin};
    const std::array<double, 2> vTop = {dim * 0.5, margin};
    const double denom =
        (vRight[1] - vTop[1]) * (vLeft[0] - vTop[0]) +
        (vTop[0] - vRight[0]) * (vLeft[1] - vTop[1]);

    std::vector<uint8_t> img(static_cast<size_t>(dim) * dim * 4, 0);
    for(int y = 0; y < dim; ++y) {
        for(int x = 0; x < dim; ++x) {
            const double px = x + 0.5;
            const double py = y + 0.5;
            double wLeft = ((vRight[1] - vTop[1]) * (px - vTop[0]) + (vTop[0] - vRight[0]) * (py - vTop[1])) / denom;
            double wRight = ((vTop[1] - vLeft[1]) * (px - vTop[0]) + (vLeft[0] - vTop[0]) * (py - vTop[1])) / denom;
            double wTop = 1.0 - wLeft - wRight;
            if(wLeft < -0.002 || wRight < -0.002 || wTop < -0.002) continue;
            wLeft = std::clamp(wLeft, 0.0, 1.0);
            wRight = std::clamp(wRight, 0.0, 1.0);
            wTop = std::clamp(wTop, 0.0, 1.0);
            EbsdLib::Matrix3X1D direction(
                wLeft + wRight * std::sqrt(3.0) * 0.5,
                wRight * 0.5,
                wTop
            );
            auto [r, g, b] = hcp_ipf_color_from_direction(direction);
            const size_t idx = (static_cast<size_t>(y) * dim + x) * 4;
            img[idx] = r;
            img[idx + 1] = g;
            img[idx + 2] = b;
            img[idx + 3] = 255;
        }
    }

    std::filesystem::create_directories(img_path.parent_path());
    int ok = stbi_write_png(img_path.string().c_str(), dim, dim, 4, img.data(), dim * 4);
    if(!ok) std::cout << "failed" << std::endl;
}

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

void set_rgb_pixel(std::vector<uint8_t>& image, int width, int height, int x, int y, uint8_t value)
{
    if(x < 0 || y < 0 || x >= width || y >= height) return;
    const size_t index = (static_cast<size_t>(y) * width + x) * 3;
    image[index] = value;
    image[index + 1] = value;
    image[index + 2] = value;
}

void fill_rgb_rect(std::vector<uint8_t>& image, int width, int height, int x, int y, int rect_width, int rect_height, uint8_t value)
{
    for(int py = y; py < y + rect_height; ++py) {
        for(int px = x; px < x + rect_width; ++px) {
            set_rgb_pixel(image, width, height, px, py, value);
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
                fill_rgb_rect(
                    image,
                    width,
                    height,
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

vector<vector<std::tuple<uint8_t, uint8_t, uint8_t>>> gen_ipf_map(const vector<vector<QuatF>> &orientations, unsigned int orientation_ops_index, bool is_hcp, std::array<double, 3> ref_dir) {
    int x = orientations.size();
    if(x == 0) return {};
    int y = orientations[0].size();
    
    vector<vector<std::tuple<uint8_t, uint8_t, uint8_t>>> img(x, vector<std::tuple<uint8_t, uint8_t, uint8_t>>(y));
    const auto all_ops = LaueOps::GetAllOrientationOps();
    auto ops = orientation_ops_index < all_ops.size() ? all_ops[orientation_ops_index] : all_ops[1];
    const std::array<double, 3> refDir = normalize_ref_dir(ref_dir);
    const EbsdLib::Matrix3X1D refDirVec(refDir[0], refDir[1], refDir[2]);

    for(int i = 0; i < x; i++ ) {
        for(int j = 0; j < y; j++ ) {
            if(std::isnan(orientations[i][j].w())) {
                img[i][j] = {0, 0, 0};
                continue;
            }
            std::vector<float> eulen = OrientationTransformation::qu2eu<QuatF, std::vector<float>>(orientations[i][j]);
            if(is_hcp) {
                OrientationD eu(eulen[0], eulen[1], eulen[2]);
                OrientationD om = OrientationTransformation::eu2om<OrientationD, OrientationD>(eu);
                EbsdLib::Matrix3X3D mat(om.data());
                EbsdLib::Matrix3X1D p = mat * refDirVec;
                img[i][j] = hcp_ipf_color_from_direction(p);
                continue;
            }
            EbsdLib::Rgb c = ops->generateIPFColor(eulen[0], eulen[1], eulen[2], refDir[0], refDir[1], refDir[2], false);
            img[i][j] = {static_cast<uint8_t>((c >> 16) & 0xFF), static_cast<uint8_t>((c >> 8) & 0xFF), static_cast<uint8_t>(c & 0xFF)};
        }
    }
    return img;
}

void save_ipf_map(const std::filesystem::path& ebsd_path, const std::filesystem::path& img_path, std::array<double, 3> ref_dir, double denoindex_threshold, double denoindex_misorientation_threshold, bool include_scale_bar) {
    EbsdData data = load_ebsd_data(ebsd_path);
    int xDim = data.x_dim;
    int yDim = data.y_dim;
    auto phase = data.phase.data();
    auto euler1 = data.euler1.data();
    auto euler2 = data.euler2.data();
    auto euler3 = data.euler3.data();
    denoindex(euler1, euler2, euler3, phase, xDim, yDim, denoindex_threshold, denoindex_misorientation_threshold);
    
    vector<vector<QuatF>> orientations(yDim, vector<QuatF>(xDim));
    for (int y = 0; y < yDim; ++y) {
        for (int x = 0; x < xDim; ++x) {
            int p = y * xDim + x;
            std::vector<float> euler = {euler1[p], euler2[p], euler3[p]};
            auto qu = OrientationTransformation::eu2qu<std::vector<float>, QuatF>(euler);
            orientations[y][x] = qu;
            if(phase[p] == 0) {
                orientations[y][x] = QuatF(NAN, NAN, NAN, NAN);
            }
        }
    }


    auto pixels = gen_ipf_map(orientations, data.primary_orientation_ops_index, data.hcp, ref_dir);
    const int minimum_output_dimension = 900;
    const int shortest_dimension = std::max(1, std::min(xDim, yDim));
    const int upscale = std::clamp(
        static_cast<int>(std::ceil(static_cast<double>(minimum_output_dimension) / shortest_dimension)),
        1,
        8
    );
    const int map_width = xDim * upscale;
    const int map_height = yDim * upscale;
    const int scale_region_height = include_scale_bar ? std::max(76, 24 * upscale) : 0;
    const int output_height = map_height + scale_region_height;
    std::vector<uint8_t> img(static_cast<size_t>(map_width) * output_height * 3, 255);

    for (int source_y = 0; source_y < yDim; ++source_y) {
        for (int source_x = 0; source_x < xDim; ++source_x) {
            const auto& px = pixels[source_y][source_x];
            auto [r, g, b] = px;
            for(int dy = 0; dy < upscale; ++dy) {
                for(int dx = 0; dx < upscale; ++dx) {
                    const int target_x = source_x * upscale + dx;
                    const int target_y = source_y * upscale + dy;
                    const size_t index = (static_cast<size_t>(target_y) * map_width + target_x) * 3;
                    img[index] = r;
                    img[index + 1] = g;
                    img[index + 2] = b;
                }
            }
        }
    }

    if(include_scale_bar) {
        const double x_step_um = data.x_step;
        const double map_width_um = x_step_um * xDim;
        const double bar_um = nice_scale_length(map_width_um * 0.28);
        if(bar_um > 0.0 && x_step_um > 0.0) {
            const int bar_width = std::clamp(
                static_cast<int>(std::llround((bar_um / x_step_um) * upscale)),
                1,
                std::max(1, map_width - 40)
            );
            const int bar_height = std::max(3, upscale);
            const int tick_height = std::max(10, 4 * upscale);
            const int bar_x = (map_width - bar_width) / 2;
            const int bar_y = map_height + std::max(14, 5 * upscale);
            fill_rgb_rect(img, map_width, output_height, bar_x, bar_y, bar_width, bar_height, 20);
            fill_rgb_rect(img, map_width, output_height, bar_x, bar_y - tick_height / 2, bar_height, tick_height, 20);
            fill_rgb_rect(img, map_width, output_height, bar_x + bar_width - bar_height, bar_y - tick_height / 2, bar_height, tick_height, 20);

            const std::string label = format_scale_length(bar_um);
            const int text_scale = std::max(2, std::min(4, upscale));
            const int text_width = static_cast<int>(label.size()) * 6 * text_scale - text_scale;
            draw_text(
                img,
                map_width,
                output_height,
                std::max(0, (map_width - text_width) / 2),
                bar_y + tick_height,
                label,
                text_scale
            );
        }
    }

    std::filesystem::create_directories(img_path.parent_path());
    int ok = stbi_write_png(img_path.string().c_str(), map_width, output_height, 3, img.data(), map_width * 3);
    if(!ok) std::cout << "failed" << std::endl;
}

void save_ipf_legend(const std::filesystem::path& img_path, int size) {
    const int dim = std::max(80, size);
    auto ops = LaueOps::GetAllOrientationOps()[1]; // CubicOps
    save_ipf_legend_for_ops(img_path, dim, ops, {0.0, 0.0, 1.0}, {1.0 / std::sqrt(2.0), 0.0, 1.0 / std::sqrt(2.0)}, {1.0 / std::sqrt(3.0), 1.0 / std::sqrt(3.0), 1.0 / std::sqrt(3.0)});
}

void save_ipf_legend(const std::filesystem::path& ebsd_path, const std::filesystem::path& img_path, int size) {
    EbsdData data = load_ebsd_data(ebsd_path, true);
    const int dim = std::max(80, size);
    const auto all_ops = LaueOps::GetAllOrientationOps();
    auto ops = data.primary_orientation_ops_index < all_ops.size()
        ? all_ops[data.primary_orientation_ops_index]
        : all_ops[1];
    if(data.hcp) {
        save_hcp_ipf_legend(img_path, dim);
        return;
    }
    save_ipf_legend_for_ops(img_path, dim, ops, {0.0, 0.0, 1.0}, {1.0 / std::sqrt(2.0), 0.0, 1.0 / std::sqrt(2.0)}, {1.0 / std::sqrt(3.0), 1.0 / std::sqrt(3.0), 1.0 / std::sqrt(3.0)});
}

void save_ipf_legend_for_ops(const std::filesystem::path& img_path, int dim, LaueOps::Pointer ops, std::array<double, 3> dirA, std::array<double, 3> dirB, std::array<double, 3> dirC) {
    const double margin = dim * 0.08;
    const std::array<double, 2> v001 = {margin, dim - margin};
    const std::array<double, 2> v101 = {dim - margin, dim - margin};
    const std::array<double, 2> v111 = {dim * 0.5, margin};
    const double denom =
        (v101[1] - v111[1]) * (v001[0] - v111[0]) +
        (v111[0] - v101[0]) * (v001[1] - v111[1]);

    std::vector<uint8_t> img(static_cast<size_t>(dim) * dim * 4, 0);

    for(int y = 0; y < dim; ++y) {
        for(int x = 0; x < dim; ++x) {
            const double px = x + 0.5;
            const double py = y + 0.5;
            const double w001 = ((v101[1] - v111[1]) * (px - v111[0]) + (v111[0] - v101[0]) * (py - v111[1])) / denom;
            const double w101 = ((v111[1] - v001[1]) * (px - v111[0]) + (v001[0] - v111[0]) * (py - v111[1])) / denom;
            const double w111 = 1.0 - w001 - w101;
            if(w001 < -0.002 || w101 < -0.002 || w111 < -0.002) continue;

            std::array<double, 3> dir = {
                w001 * dirA[0] + w101 * dirB[0] + w111 * dirC[0],
                w001 * dirA[1] + w101 * dirB[1] + w111 * dirC[1],
                w001 * dirA[2] + w101 * dirB[2] + w111 * dirC[2],
            };
            dir = normalize_ref_dir(dir);

            EbsdLib::Rgb c = ops->generateIPFColor(0.0, 0.0, 0.0, dir[0], dir[1], dir[2], false);
            const size_t idx = (static_cast<size_t>(y) * dim + x) * 4;
            img[idx] = static_cast<uint8_t>((c >> 16) & 0xFF);
            img[idx + 1] = static_cast<uint8_t>((c >> 8) & 0xFF);
            img[idx + 2] = static_cast<uint8_t>(c & 0xFF);
            img[idx + 3] = 255;
        }
    }

    std::filesystem::create_directories(img_path.parent_path());
    int ok = stbi_write_png(img_path.string().c_str(), dim, dim, 4, img.data(), dim * 4);
    if(!ok) std::cout << "failed" << std::endl;
}
