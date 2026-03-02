#pragma once

#include "grains.h"
#include <array>
#include <tuple>

class Orientations_finder {
private:
    std::vector<Grain> grains;
public:
    Orientations_finder(std::vector<Grain> _grains): grains(_grains) {}
    std::tuple<double, double, double> ratio(double misorientation_threshold /* degree */, std::array<double, 3> sampleDir = {0.0, 0.0, 1.0});
};