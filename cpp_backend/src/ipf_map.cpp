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
#include "EbsdLib/Math/Matrix3X1.hpp"
#include "EbsdLib/Core/Orientation.hpp"
#include <EbsdLib/LaueOps/LaueOps.h>
#include "utils.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <stdexcept>
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

LaueOps::Pointer select_ipf_ops(CprReader& reader, const uint8_t* phase = nullptr, int totalPoints = 0)
{
    int primaryPhaseIndex = 0;
    if(phase != nullptr && totalPoints > 0) {
        std::map<int, int> phaseCounts;
        for(int i = 0; i < totalPoints; ++i) {
            const int phaseIndex = static_cast<int>(phase[i]);
            if(phaseIndex > 0) phaseCounts[phaseIndex] += 1;
        }
        int primaryPhaseCount = 0;
        for(const auto& item : phaseCounts) {
            if(item.second > primaryPhaseCount) {
                primaryPhaseIndex = item.first;
                primaryPhaseCount = item.second;
            }
        }
    }

    for(const auto& phaseInfo : reader.getPhaseVector()) {
        if(!phaseInfo) continue;
        if(primaryPhaseIndex > 0 && phaseInfo->getPhaseIndex() != primaryPhaseIndex) continue;
        if(is_hcp_lattice(phaseInfo->getLatticeConstants())) {
            return LaueOps::GetOrientationOpsFromSpaceGroupNumber(static_cast<size_t>(phaseInfo->getSpaceGroup()));
        }
    }
    return LaueOps::GetAllOrientationOps()[1]; // CubicOps fallback
}

bool reader_has_hcp_lattice(CprReader& reader)
{
    for(const auto& phaseInfo : reader.getPhaseVector()) {
        if(phaseInfo && is_hcp_lattice(phaseInfo->getLatticeConstants())) return true;
    }
    return false;
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

} // namespace

vector<vector<std::tuple<uint8_t, uint8_t, uint8_t>>> gen_ipf_map(const vector<vector<QuatF>> &orientations, const std::filesystem::path& cpr_path, std::array<double, 3> ref_dir) {
    int x = orientations.size();
    if(x == 0) return {};
    int y = orientations[0].size();
    
    vector<vector<std::tuple<uint8_t, uint8_t, uint8_t>>> img(x, vector<std::tuple<uint8_t, uint8_t, uint8_t>>(y));
    CprReader headerReader;
    headerReader.setFileName(cpr_path);
    if(headerReader.readHeaderOnly() < 0) {
        throw std::runtime_error("Failed to read CPR header for IPF map: " + headerReader.getErrorMessage());
    }
    const bool isHcp = reader_has_hcp_lattice(headerReader);
    auto ops = select_ipf_ops(headerReader);
    const std::array<double, 3> refDir = normalize_ref_dir(ref_dir);
    const EbsdLib::Matrix3X1D refDirVec(refDir[0], refDir[1], refDir[2]);

    for(int i = 0; i < x; i++ ) {
        for(int j = 0; j < y; j++ ) {
            if(std::isnan(orientations[i][j].w())) {
                img[i][j] = {0, 0, 0};
                continue;
            }
            std::vector<float> eulen = OrientationTransformation::qu2eu<QuatF, std::vector<float>>(orientations[i][j]);
            if(isHcp) {
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

void save_ipf_map(const std::filesystem::path& cpr_path, const std::filesystem::path& img_path, std::array<double, 3> ref_dir, double denoindex_threshold) {
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
    denoindex(euler1, euler2, euler3, phase, xDim, yDim, denoindex_threshold);
    
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


    auto pixels = gen_ipf_map(orientations, cpr_path, ref_dir);
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
    auto ops = LaueOps::GetAllOrientationOps()[1]; // CubicOps
    save_ipf_legend_for_ops(img_path, dim, ops, {0.0, 0.0, 1.0}, {1.0 / std::sqrt(2.0), 0.0, 1.0 / std::sqrt(2.0)}, {1.0 / std::sqrt(3.0), 1.0 / std::sqrt(3.0), 1.0 / std::sqrt(3.0)});
}

void save_ipf_legend(const std::filesystem::path& cpr_path, const std::filesystem::path& img_path, int size) {
    CprReader reader;
    reader.setFileName(cpr_path);
    if(reader.readHeaderOnly() < 0) {
        throw std::runtime_error("Failed to read CPR header for IPF legend: " + reader.getErrorMessage());
    }
    const int dim = std::max(80, size);
    auto ops = select_ipf_ops(reader);
    bool isHcp = false;
    for(const auto& phaseInfo : reader.getPhaseVector()) {
        if(phaseInfo && is_hcp_lattice(phaseInfo->getLatticeConstants())) {
            isHcp = true;
            break;
        }
    }
    if(isHcp) {
        save_hcp_ipf_legend(img_path, dim);
        return;
    }
    save_ipf_legend(img_path, size);
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
