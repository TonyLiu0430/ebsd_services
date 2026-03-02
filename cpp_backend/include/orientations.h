#pragma once

#include "grains.h"
#include <array>
#include <tuple>

class Orientations_finder {
private:
    std::vector<Grain> grains;
    double step_size; /* (um / pixel) / */
public:
    Orientations_finder(std::vector<Grain> _grains, double _step_size): grains(_grains), step_size(_step_size) {}
    std::tuple<double, double, double> ratio(double misorientation_threshold /* degree */, std::array<double, 3> sampleDir = {0.0, 0.0, 1.0});
};