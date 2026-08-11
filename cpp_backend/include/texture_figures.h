#pragma once

#include <filesystem>

void save_pole_figure(const std::filesystem::path& ebsd_path, const std::filesystem::path& img_path);

void save_inverse_pole_figure(const std::filesystem::path& ebsd_path, const std::filesystem::path& img_path);
