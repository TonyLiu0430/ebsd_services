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

class Orientations_finder_by_pixel {
private:
    float *euler1, *euler2, *euler3;
    uint8_t *phase;
    int Xdim, Ydim;
public:
    Orientations_finder_by_pixel(float* euler1, float* euler2, float* euler3, uint8_t* phase, int Xdim, int Ydim): euler1(euler1), euler2(euler2), euler3(euler3), phase(phase), Xdim(Xdim), Ydim(Ydim) {}
    std::tuple<double, double, double> ratio(double misorientation_threshold /* degree */, std::array<double, 3> sampleDir = {0.0, 0.0, 1.0});
};
