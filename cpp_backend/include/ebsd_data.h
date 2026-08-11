#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

struct EbsdPhaseMetadata {
    int phase_index = 0;
    std::string structure_name;
    std::string material_name;
    std::vector<float> lattice_constants;
    int laue_group = 0;
    int space_group = 0;
    unsigned int orientation_ops_index = 1;
};

struct EbsdData {
    int x_dim = 0;
    int y_dim = 0;
    double x_step = 0.0;
    double y_step = 0.0;
    std::vector<float> euler1;
    std::vector<float> euler2;
    std::vector<float> euler3;
    std::vector<uint8_t> phase;
    std::vector<EbsdPhaseMetadata> phases;
    int primary_phase_index = 0;
    unsigned int primary_orientation_ops_index = 1;
    bool hcp = false;
};

bool is_supported_ebsd_extension(const std::filesystem::path& path);
EbsdData load_ebsd_data(const std::filesystem::path& path, bool header_only = false);
