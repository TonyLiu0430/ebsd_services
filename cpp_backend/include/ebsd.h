#pragma once
#include <vector>
#include <EbsdLib/Core/Quaternion.hpp>

std::vector<int> find_grains(const std::vector<std::vector<QuatF>> &orientations, int min_grain_size = 10/*pixel*/);
std::tuple<double, double, double> orientation_ratio(const float* euler1,
                                    const float* euler2,
                                    const float* euler3,
                                    size_t nPoints,
                                    std::array<double, 3> sampleDir = {0.0, 0.0, 1.0},
                                    double misorientation_threshold = 15 /* degree */);

inline 
std::tuple<std::vector<int>, std::tuple<double, double, double>> features(const std::string &cpr_file_path) {
    std::shared_ptr<CprReader> reader = std::make_shared<CprReader>();
    reader->setFileName(cpr_file_path);
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
    if(euler1 == nullptr || euler2 == nullptr || euler3 == nullptr) {
        std::cout << "failed to read euler angles" << std::endl;
    }
    
    std::vector<std::vector<QuatF>> orientations(yDim, std::vector<QuatF>(xDim));
    for (int y = 0; y < yDim; ++y) {
        for (int x = 0; x < xDim; ++x) {
            int p = y * xDim + x;
            std::vector<float> euler = {euler1[p], euler2[p], euler3[p]};
            auto qu = OrientationTransformation::eu2qu<std::vector<float>, QuatF>(euler);
            orientations[y][x] = qu;
        }
    }
    auto grains = find_grains(orientations);
    auto orient = orientation_ratio(euler1, euler2, euler3, xDim * yDim);
    return {grains, orient};
}