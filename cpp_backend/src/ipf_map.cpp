// #include <EbsdLib/IO/EbsdImporter.h>
#include <EbsdLib/IO/HKL/CprReader.h>
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
#include <EbsdLib/LaueOps/LaueOps.h>
#include "utils.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <stdexcept>
#include <vector>
#ifndef STB_IMAGE_WRITE_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION
#endif
#include <stb/stb_image_write.h>

using std::vector;
using std::pair;
using std::swap;

namespace {

std::array<double, 3> normalize_ref_dir(std::array<double, 3> ref_dir) {
    const double norm = std::sqrt(ref_dir[0] * ref_dir[0] + ref_dir[1] * ref_dir[1] + ref_dir[2] * ref_dir[2]);
    if(!std::isfinite(norm) || norm <= 1e-12) {
        throw std::invalid_argument("IPF reference direction vector must not be zero");
    }
    return {ref_dir[0] / norm, ref_dir[1] / norm, ref_dir[2] / norm};
}

} // namespace

vector<vector<std::tuple<uint8_t, uint8_t, uint8_t>>> gen_ipf_map(const vector<vector<QuatF>> &orientations, std::array<double, 3> ref_dir) {
    int x = orientations.size();
    if(x == 0) return {};
    int y = orientations[0].size();
    
    vector<vector<std::tuple<uint8_t, uint8_t, uint8_t>>> img(x, vector<std::tuple<uint8_t, uint8_t, uint8_t>>(y));
    auto ops = LaueOps::GetAllOrientationOps()[1]; // CubicOps
    const std::array<double, 3> refDir = normalize_ref_dir(ref_dir);

    for(int i = 0; i < x; i++ ) {
        for(int j = 0; j < y; j++ ) {
            if(std::isnan(orientations[i][j].w())) {
                img[i][j] = {0, 0, 0};
                continue;
            }
            std::vector<float> eulen = OrientationTransformation::qu2eu<QuatF, std::vector<float>>(orientations[i][j]);
            EbsdLib::Rgb c = ops->generateIPFColor(eulen[0], eulen[1], eulen[2], refDir[0], refDir[1], refDir[2], false);
            img[i][j] = {static_cast<uint8_t>((c >> 16) & 0xFF), static_cast<uint8_t>((c >> 8) & 0xFF), static_cast<uint8_t>(c & 0xFF)};
        }
    }
    return img;
}

void save_ipf_map(const std::filesystem::path& cpr_path, const std::filesystem::path& img_path, std::array<double, 3> ref_dir) {
    std::shared_ptr<CprReader> reader = std::make_shared<CprReader>();
    reader->setFileName(cpr_path);
    if(reader->readFile() < 0) {
        std::cout << "failed" << std::endl;
    }
    int xDim = reader->getXDimension();
    int yDim = reader->getYDimension();
    int totalPoints = xDim * yDim;
    auto phase = reader->getPhasePointer();
    auto euler1 = reader->getEuler1Pointer();
    auto euler2 = reader->getEuler2Pointer();
    auto euler3 = reader->getEuler3Pointer();
    denoindex(euler1, euler2, euler3, phase, xDim, yDim, 50);
    
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


    auto pixels = gen_ipf_map(orientations, ref_dir);
    std::vector<uint8_t> img;
    int w = xDim, h = yDim;
    img.reserve((size_t)w * h * 3);

    for (const auto& row : pixels) {
        for (const auto& px : row) {
            auto [r, g, b] = px;

            img.push_back(static_cast<uint8_t>(r));
            img.push_back(static_cast<uint8_t>(g));
            img.push_back(static_cast<uint8_t>(b));
        }
    }
    std::filesystem::create_directories(img_path.parent_path());
    int ok = stbi_write_png(img_path.string().c_str(), w, h, 3, img.data(), w * 3);
    if(!ok) std::cout << "failed" << std::endl;
}

void save_ipf_legend(const std::filesystem::path& img_path, int size) {
    const int dim = std::max(80, size);
    const double margin = dim * 0.08;
    const std::array<double, 2> v001 = {margin, dim - margin};
    const std::array<double, 2> v101 = {dim - margin, dim - margin};
    const std::array<double, 2> v111 = {dim * 0.5, margin};
    const double denom =
        (v101[1] - v111[1]) * (v001[0] - v111[0]) +
        (v111[0] - v101[0]) * (v001[1] - v111[1]);

    auto ops = LaueOps::GetAllOrientationOps()[1]; // CubicOps
    std::vector<uint8_t> img(static_cast<size_t>(dim) * dim * 4, 0);

    for(int y = 0; y < dim; ++y) {
        for(int x = 0; x < dim; ++x) {
            const double px = x + 0.5;
            const double py = y + 0.5;
            const double w001 = ((v101[1] - v111[1]) * (px - v111[0]) + (v111[0] - v101[0]) * (py - v111[1])) / denom;
            const double w101 = ((v111[1] - v001[1]) * (px - v111[0]) + (v001[0] - v111[0]) * (py - v111[1])) / denom;
            const double w111 = 1.0 - w001 - w101;
            if(w001 < -0.002 || w101 < -0.002 || w111 < -0.002) continue;

            const double invSqrt2 = 1.0 / std::sqrt(2.0);
            const double invSqrt3 = 1.0 / std::sqrt(3.0);
            std::array<double, 3> dir = {
                w101 * invSqrt2 + w111 * invSqrt3,
                w111 * invSqrt3,
                w001 + w101 * invSqrt2 + w111 * invSqrt3,
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
