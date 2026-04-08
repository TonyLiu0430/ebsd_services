#pragma once

#include <cstdint>
#include <vector>
#include <EbsdLib/Core/Quaternion.hpp>
#include <nlohmann/json.hpp>
#include <filesystem>

nlohmann::json features(const std::string &cpr_file_path);

std::vector<std::vector<std::tuple<uint8_t, uint8_t, uint8_t>>> gen_ipf_map(const std::vector<std::vector<QuatF>> &orientations);

void save_ipf_map(const std::filesystem::path& cpr_path, const std::filesystem::path& img_path);

/* TODO */
std::vector<std::vector<QuatF>> orientations_to_2d(float* euler1, float* euler2, float* euler3, int Xdim, int Ydim);

int noindexed(uint8_t* phase, int xDim, int yDim);

void denoindex(float* euler1, float* euler2, float* euler3, uint8_t* phase, int x, int y, double threshold /*%*/);
