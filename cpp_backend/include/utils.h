#pragma once

#include <vector>
#include <EbsdLib/Core/Quaternion.hpp>
#include <nlohmann/json.hpp>

nlohmann::json features(const std::string &cpr_file_path);