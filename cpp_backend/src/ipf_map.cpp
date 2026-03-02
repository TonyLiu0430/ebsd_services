#include <EbsdLib/IO/EbsdImporter.h>
#include <EbsdLib/IO/HKL/CprReader.h>
#include <vector>
#include <memory>
#include <EbsdLib/Core/OrientationTransformation.hpp>
#include <EbsdLib/LaueOps/LaueOps.h>
#include <algorithm>
#include <ranges>
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb/stb_image_write.h>
#include <map>

using std::vector;
using std::pair;
using std::swap;
using std::cout;
using std::endl;

vector<vector<std::tuple<uint8_t, uint8_t, uint8_t>>> ipf_map(const vector<vector<QuatF>> &orientations) {
    int x = orientations.size();
    if(x == 0) return {};
    int y = orientations[0].size();
    
    vector<vector<std::tuple<uint8_t, uint8_t, uint8_t>>> img(x, vector<std::tuple<uint8_t, uint8_t, uint8_t>>(y));
    auto ops = LaueOps::GetAllOrientationOps()[1]; // CubicOps
    double refDir[3] = {0.0, 0.0, 1.0}; // z

    for(int i = 0; i < x; i++ ) {
        for(int j = 0; j < y; j++ ) {
            std::vector<float> eulen = OrientationTransformation::qu2eu<QuatF, std::vector<float>>(orientations[i][j]);
            EbsdLib::Rgb c = ops->generateIPFColor(eulen[0], eulen[1], eulen[2], refDir[0], refDir[1], refDir[2], false);
            img[i][j] = {static_cast<uint8_t>((c >> 16) & 0xFF), static_cast<uint8_t>((c >> 8) & 0xFF), static_cast<uint8_t>(c & 0xFF)};
        }
    }
    return img;
}

// auto pixels = ipf_map(orientations);
    // std::vector<uint8_t> img;
    // int w = xDim, h = yDim;
    // img.reserve((size_t)w * h * 3);

    // for (const auto& row : pixels) {
    //     for (const auto& px : row) {
    //         auto [r, g, b] = px;

    //         img.push_back(static_cast<uint8_t>(r));
    //         img.push_back(static_cast<uint8_t>(g));
    //         img.push_back(static_cast<uint8_t>(b));
    //     }
    // }
    // int ok = stbi_write_png("test.png", w, h, 3, img.data(), w * 3);
    // if(!ok) std::cout << "failed" << std::endl;