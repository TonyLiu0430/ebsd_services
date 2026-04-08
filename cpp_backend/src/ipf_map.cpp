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
#include <vector>
#ifndef STB_IMAGE_WRITE_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION
#endif
#include <stb/stb_image_write.h>

using std::vector;
using std::pair;
using std::swap;

vector<vector<std::tuple<uint8_t, uint8_t, uint8_t>>> gen_ipf_map(const vector<vector<QuatF>> &orientations) {
    int x = orientations.size();
    if(x == 0) return {};
    int y = orientations[0].size();
    
    vector<vector<std::tuple<uint8_t, uint8_t, uint8_t>>> img(x, vector<std::tuple<uint8_t, uint8_t, uint8_t>>(y));
    auto ops = LaueOps::GetAllOrientationOps()[1]; // CubicOps
    double refDir[3] = {0.0, 0.0, 1.0}; // z

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

void save_ipf_map(const std::filesystem::path& cpr_path, const std::filesystem::path& img_path) {
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


    auto pixels = gen_ipf_map(orientations);
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