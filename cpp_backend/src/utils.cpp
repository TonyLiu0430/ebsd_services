#include <iostream>
#include <stdexcept>
#include "utils.h"
#include "grains.h"
#include "orientations.h"
#include "EbsdLib/IO/HKL/CprReader.h"
#include <memory>
#include <vector>

nlohmann::json features(const std::string &cpr_file_path) {
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
    GrainSegmenter grainSegmenter(euler1, euler2, euler3, xDim, yDim);
    std::vector<Grain> grains = grainSegmenter.find_grains(10);
    if(reader->getXStep() != reader->getYStep()) {
        throw std::invalid_argument("X and Y step sizes are not equal");
    }
    double step_size = reader->getXStep();
    Orientations_finder orientations_finder(grains, step_size);
    std::vector<double> grain_sizes;
    double pixel_area = step_size * step_size;
    for(auto &g : grains) {
        double area = g.size * pixel_area;
        double equivalent_radius = std::sqrt(area / M_PI);
        grain_sizes.push_back(equivalent_radius * 2); // equivalent diameter
    }
    auto ratio20 = orientations_finder.ratio(20);
    auto ratio15 = orientations_finder.ratio(15);
    nlohmann::json j = {
        {"grains", grain_sizes},
        {"orientation_ratio(20%)", {std::get<0>(ratio20), std::get<1>(ratio20), std::get<2>(ratio20)}},
        {"orientation_ratio(15%)", {std::get<0>(ratio15), std::get<1>(ratio15), std::get<2>(ratio15)}}
    };
    return j;
}