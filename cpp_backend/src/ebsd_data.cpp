#include "ebsd_data.h"

#include <EbsdLib/IO/HKL/CprReader.h>
#include <EbsdLib/IO/HKL/CtfReader.h>
#include <EbsdLib/IO/TSL/AngReader.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <map>
#include <stdexcept>
#include <utility>

namespace {

std::string lower_extension(const std::filesystem::path& path)
{
    std::string extension = path.extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return extension;
}

bool is_hcp_lattice(const std::vector<float>& lattice)
{
    if(lattice.size() < 6) return false;
    const double a = lattice[0];
    const double b = lattice[1];
    if(a <= 0.0 || b <= 0.0) return false;
    const double relative_difference = std::fabs(a - b) / ((a + b) * 0.5);
    return relative_difference <= 0.03
        && std::fabs(lattice[3] - 90.0) <= 3.0
        && std::fabs(lattice[4] - 90.0) <= 3.0
        && std::fabs(lattice[5] - 120.0) <= 3.0;
}

template <typename T>
void copy_floats(std::vector<float>& destination, const T* source, size_t count)
{
    if(source == nullptr && count > 0) throw std::runtime_error("EBSD reader returned a null data array");
    destination.assign(source, source + count);
}

template <typename T>
void copy_phases(std::vector<uint8_t>& destination, const T* source, size_t count)
{
    if(source == nullptr && count > 0) throw std::runtime_error("EBSD reader returned a null phase array");
    destination.resize(count);
    for(size_t index = 0; index < count; ++index) {
        destination[index] = static_cast<uint8_t>(std::clamp(static_cast<int>(source[index]), 0, 255));
    }
}

template <typename T>
void copy_band_contrast(std::vector<uint8_t>& destination, const T* source, size_t count)
{
    if(source == nullptr && count > 0) throw std::runtime_error("EBSD reader returned a null band contrast array");
    destination.resize(count);
    for(size_t index = 0; index < count; ++index) {
        destination[index] = static_cast<uint8_t>(std::clamp(static_cast<int>(source[index]), 0, 255));
    }
}

void resolve_primary_phase(EbsdData& data)
{
    std::map<int, int> counts;
    for(uint8_t phase : data.phase) {
        if(phase > 0) counts[phase] += 1;
    }
    int best_count = 0;
    for(const auto& [phase_index, count] : counts) {
        if(count > best_count) {
            data.primary_phase_index = phase_index;
            best_count = count;
        }
    }
    if(data.primary_phase_index == 0 && !data.phases.empty()) {
        data.primary_phase_index = data.phases.front().phase_index;
    }
    for(const auto& phase : data.phases) {
        if(phase.phase_index != data.primary_phase_index) continue;
        data.primary_orientation_ops_index = phase.orientation_ops_index;
        data.hcp = is_hcp_lattice(phase.lattice_constants);
        return;
    }
    data.hcp = std::any_of(data.phases.begin(), data.phases.end(), [](const auto& phase) {
        return is_hcp_lattice(phase.lattice_constants);
    });
}

void convert_degrees_to_radians(std::vector<float>& values)
{
    constexpr float degrees_to_radians = 0.01745329251994329577F;
    for(float& value : values) value *= degrees_to_radians;
}

void validate_geometry(const EbsdData& data, const std::filesystem::path& path)
{
    if(data.x_dim <= 0 || data.y_dim <= 0) {
        throw std::runtime_error("EBSD file has invalid grid dimensions: " + path.string());
    }
    if(!std::isfinite(data.x_step) || !std::isfinite(data.y_step) || data.x_step <= 0.0 || data.y_step <= 0.0) {
        throw std::runtime_error("EBSD file has invalid grid step sizes: " + path.string());
    }
}

EbsdData load_cpr(const std::filesystem::path& path, bool header_only)
{
    CprReader reader;
    reader.setFileName(path.string());
    const int result = header_only ? reader.readHeaderOnly() : reader.readFile();
    if(result < 0) throw std::runtime_error("Failed to read CPR/CRC data: " + reader.getErrorMessage());

    EbsdData data;
    data.x_dim = reader.getXDimension();
    data.y_dim = reader.getYDimension();
    data.x_step = reader.getXStep();
    data.y_step = reader.getYStep();
    validate_geometry(data, path);
    for(const auto& phase : reader.getPhaseVector()) {
        if(!phase) continue;
        data.phases.push_back({
            phase->getPhaseIndex(),
            phase->getPhaseName(),
            phase->getMaterialName(),
            phase->getLatticeConstants(),
            static_cast<int>(phase->getLaueGroup()),
            phase->getSpaceGroup(),
            phase->determineOrientationOpsIndex(),
        });
    }
    if(!header_only) {
        const size_t count = static_cast<size_t>(data.x_dim) * data.y_dim;
        copy_floats(data.euler1, reader.getEuler1Pointer(), count);
        copy_floats(data.euler2, reader.getEuler2Pointer(), count);
        copy_floats(data.euler3, reader.getEuler3Pointer(), count);
        copy_phases(data.phase, reader.getPhasePointer(), count);
        copy_band_contrast(data.band_contrast, reader.getBandContrastPointer(), count);
    }
    resolve_primary_phase(data);
    return data;
}

EbsdData load_ctf(const std::filesystem::path& path, bool header_only)
{
    CtfReader reader;
    reader.setFileName(path.string());
    const int result = header_only ? reader.readHeaderOnly() : reader.readFile();
    if(result < 0) throw std::runtime_error("Failed to read CTF data: " + reader.getErrorMessage());

    EbsdData data;
    data.x_dim = reader.getXDimension();
    data.y_dim = reader.getYDimension();
    data.x_step = reader.getXStep();
    data.y_step = reader.getYStep();
    validate_geometry(data, path);
    for(const auto& phase : reader.getPhaseVector()) {
        if(!phase) continue;
        data.phases.push_back({
            phase->getPhaseIndex(),
            phase->getPhaseName(),
            phase->getMaterialName(),
            phase->getLatticeConstants(),
            static_cast<int>(phase->getLaueGroup()),
            phase->getSpaceGroup(),
            phase->determineOrientationOpsIndex(),
        });
    }
    if(!header_only) {
        const size_t count = static_cast<size_t>(data.x_dim) * data.y_dim;
        copy_floats(data.euler1, reader.getEuler1Pointer(), count);
        copy_floats(data.euler2, reader.getEuler2Pointer(), count);
        copy_floats(data.euler3, reader.getEuler3Pointer(), count);
        convert_degrees_to_radians(data.euler1);
        convert_degrees_to_radians(data.euler2);
        convert_degrees_to_radians(data.euler3);
        copy_phases(data.phase, reader.getPhasePointer(), count);
        copy_band_contrast(data.band_contrast, reader.getBandContrastPointer(), count);
    }
    resolve_primary_phase(data);
    return data;
}

EbsdData load_ang(const std::filesystem::path& path, bool header_only)
{
    AngReader reader;
    reader.setFileName(path.string());
    const int result = header_only ? reader.readHeaderOnly() : reader.readFile();
    if(result < 0) throw std::runtime_error("Failed to read ANG data: " + reader.getErrorMessage());

    EbsdData data;
    data.x_dim = reader.getXDimension();
    data.y_dim = reader.getYDimension();
    data.x_step = reader.getXStep();
    data.y_step = reader.getYStep();
    validate_geometry(data, path);
    for(const auto& phase : reader.getPhaseVector()) {
        if(!phase) continue;
        data.phases.push_back({
            phase->getPhaseIndex(),
            phase->getFormula(),
            phase->getMaterialName(),
            phase->getLatticeConstants(),
            static_cast<int>(phase->getSymmetry()),
            0,
            phase->determineOrientationOpsIndex(),
        });
    }
    if(!header_only) {
        const size_t count = static_cast<size_t>(data.x_dim) * data.y_dim;
        copy_floats(data.euler1, reader.getPhi1Pointer(), count);
        copy_floats(data.euler2, reader.getPhiPointer(), count);
        copy_floats(data.euler3, reader.getPhi2Pointer(), count);
        copy_phases(data.phase, reader.getPhaseDataPointer(), count);
    }
    resolve_primary_phase(data);
    return data;
}

} // namespace

EbsdData load_ebsd_data(const std::filesystem::path& path, bool header_only)
{
    const std::string extension = lower_extension(path);
    if(extension == ".cpr") return load_cpr(path, header_only);
    if(extension == ".ang") return load_ang(path, header_only);
    if(extension == ".ctf") return load_ctf(path, header_only);
    throw std::invalid_argument("Unsupported EBSD format: " + extension);
}
