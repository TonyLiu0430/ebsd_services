#pragma once

#include <filesystem>

void save_band_contrast_map(
    const std::filesystem::path& ebsd_path,
    const std::filesystem::path& img_path,
    bool include_scale_bar = true
);
