#include <iostream>
#include <array>
#include <algorithm>
#include <cmath>
#include <map>
#include <stdexcept>
#include "utils.h"
#include "ebsd_data.h"
#include "grains.h"
#include "orientations.h"
#include <EbsdLib/Core/OrientationTransformation.hpp>
#include <EbsdLib/LaueOps/LaueOps.h>
#include <memory>
#include <vector>

namespace {

struct NeighborSample {
    int id;
    int weight;
    QuatF quat;
};

int find_root(std::vector<int>& parent, int u) {
    if(parent[u] == u) {
        return u;
    }
    return parent[u] = find_root(parent, parent[u]);
}

void unite(std::vector<int>& parent, int a, int b) {
    a = find_root(parent, a);
    b = find_root(parent, b);
    if(a != b) {
        parent[b] = a;
    }
}

float misorientation_angle_rad(const QuatF& a, const QuatF& b) {
    static const auto ops = LaueOps::GetAllOrientationOps()[1]; // CubicOps
    OrientationF misor = ops->calculateMisorientation(a, b);
    float ax, ay, az, angle;
    misor.toAxisAngle(ax, ay, az, angle);
    return angle;
}

int select_cluster_medoid(const std::vector<NeighborSample>& samples, const std::vector<int>& members) {
    int best_idx = members[0];
    double best_cost = std::numeric_limits<double>::infinity();

    for(const int candidate : members) {
        double cost = 0.0;
        for(const int other : members) {
            cost += static_cast<double>(samples[other].weight) *
                    static_cast<double>(misorientation_angle_rad(samples[candidate].quat, samples[other].quat));
        }

        if(cost < best_cost) {
            best_cost = cost;
            best_idx = candidate;
        }
    }

    return best_idx;
}

template <typename T>
bool is_hcp_lattice(const std::vector<T>& lattice)
{
    if(lattice.size() < 6) {
        return false;
    }
    const double a = lattice[0];
    const double b = lattice[1];
    const double alpha = lattice[3];
    const double beta = lattice[4];
    const double gamma = lattice[5];
    if(a <= 0.0 || b <= 0.0) {
        return false;
    }
    const double abRelDiff = std::fabs(a - b) / ((a + b) * 0.5);
    return abRelDiff <= 0.03
        && std::fabs(alpha - 90.0) <= 3.0
        && std::fabs(beta - 90.0) <= 3.0
        && std::fabs(gamma - 120.0) <= 3.0;
}

nlohmann::json orientation_labels_for_family_set(OrientationFamilySet familySet)
{
    if(familySet == OrientationFamilySet::Hcp) {
        return {"[0001]", "[10-10]", "[11-20]"};
    }
    return {"[001]", "[011]", "[111]"};
}

std::tuple<double, double, double> safe_grain_orientation_ratio(
    const std::vector<Grain>& grains,
    double threshold,
    OrientationFamilySet familySet
)
{
    if(grains.empty()) return {0.0, 0.0, 0.0};
    try {
        Orientations_finder finder(grains);
        return finder.ratio(threshold, {0.0, 0.0, 1.0}, familySet);
    } catch(const std::exception&) {
        return {0.0, 0.0, 0.0};
    }
}

} // namespace

nlohmann::json features(const std::string &ebsd_file_path, int min_grain_size) {
    EbsdData data = load_ebsd_data(ebsd_file_path);
    int xDim = data.x_dim;
    int yDim = data.y_dim;
    int totalPoints = xDim * yDim;
    auto phase = data.phase.data();
    auto euler1 = data.euler1.data();
    auto euler2 = data.euler2.data();
    auto euler3 = data.euler3.data();
    const OrientationFamilySet orientationFamilySet = data.hcp ? OrientationFamilySet::Hcp : OrientationFamilySet::Cubic;
    denoindex(euler1, euler2, euler3, phase, xDim, yDim, 50);
    GrainSegmenter grainSegmenter(euler1, euler2, euler3, xDim, yDim);
    std::vector<Grain> grains = grainSegmenter.find_grains(min_grain_size);
    if(std::fabs(data.x_step - data.y_step) > 1e-9) {
        throw std::invalid_argument("X and Y step sizes are not equal");
    }
    double step_size = data.x_step;
    std::vector<double> grain_sizes;
    double pixel_area = step_size * step_size;
    for(auto &g : grains) {
        double area = g.size * pixel_area;
        double equivalent_radius = std::sqrt(area / M_PI);
        grain_sizes.push_back(equivalent_radius * 2); // equivalent diameter
    }
    // Orientations_finder orientations_finder(grains);
    Orientations_finder_by_pixel orientations_finder(euler1, euler2, euler3, phase, xDim, yDim);
    auto ratio20 = orientations_finder.ratio(20, {0.0, 0.0, 1.0}, orientationFamilySet);
    auto ratio15 = orientations_finder.ratio(15, {0.0, 0.0, 1.0}, orientationFamilySet);
    auto grainRatio20 = safe_grain_orientation_ratio(grains, 20, orientationFamilySet);
    auto grainRatio15 = safe_grain_orientation_ratio(grains, 15, orientationFamilySet);
    nlohmann::json j = {
        {"grains", grain_sizes},
        {"noindexed", noindexed(phase, xDim, yDim)},
        {"orientation_family", orientationFamilySet == OrientationFamilySet::Hcp ? "hcp" : "cubic"},
        {"orientation_labels", orientation_labels_for_family_set(orientationFamilySet)},
        {"orientation_ratio(20%)", {std::get<0>(ratio20), std::get<1>(ratio20), std::get<2>(ratio20)}},
        {"orientation_ratio(15%)", {std::get<0>(ratio15), std::get<1>(ratio15), std::get<2>(ratio15)}},
        {"orientation_ratio_by_grains(20%)", {std::get<0>(grainRatio20), std::get<1>(grainRatio20), std::get<2>(grainRatio20)}},
        {"orientation_ratio_by_grains(15%)", {std::get<0>(grainRatio15), std::get<1>(grainRatio15), std::get<2>(grainRatio15)}}
    };
    return j;
}

nlohmann::json material_metadata(const std::string &ebsd_file_path) {
    EbsdData data = load_ebsd_data(ebsd_file_path, true);
    nlohmann::json phases = nlohmann::json::array();
    for(const auto& phase : data.phases) {
        phases.push_back({
            {"phase_index", phase.phase_index},
            {"structure_name", phase.structure_name},
            {"material_name", phase.material_name},
            {"lattice_constants", phase.lattice_constants},
            {"laue_group", phase.laue_group},
            {"space_group", phase.space_group}
        });
    }

    return {{"phases", phases}};
}

int noindexed(uint8_t* phase, int xDim, int yDim) {
    int count = 0;
    for(int i = 0; i < xDim * yDim; i++) {
        if(phase[i] == 0) {
            count++;
        }
    }
    return count;
}

void denoindex(float* euler1, float* euler2, float* euler3, uint8_t* phase, int x, int y, double threshold, double misorientation_threshold) {
    if(x <= 0 || y <= 0) {
        return;
    }
    if(euler1 == nullptr || euler2 == nullptr || euler3 == nullptr || phase == nullptr) {
        throw std::invalid_argument("denoindex: input pointers must not be null");
    }
    if(!std::isfinite(misorientation_threshold) || misorientation_threshold < 0.0 || misorientation_threshold > 180.0) {
        throw std::invalid_argument("denoindex: misorientation threshold must be a finite number between 0 and 180 degrees");
    }

    const int total = x * y;
    const double ratio_threshold = std::clamp(threshold / 100.0, 0.0, 1.0);
    const float tolerance_rad = static_cast<float>(misorientation_threshold * M_PI / 180.0);
    constexpr std::array<std::pair<int, int>, 8> neighbors = {{
        {-1,  0}, {1,  0}, {0, -1}, {0,  1},
        {-1, -1}, {-1, 1}, {1, -1}, {1,  1}
    }};

    bool changed = false;
    do {
        changed = false;

        std::vector<float> prev_euler1(euler1, euler1 + total);
        std::vector<float> prev_euler2(euler2, euler2 + total);
        std::vector<float> prev_euler3(euler3, euler3 + total);
        std::vector<uint8_t> prev_phase(phase, phase + total);

        for(int i = 0; i < y; i++) {
            for(int j = 0; j < x; j++) {
                const int cur = i * x + j;
                if(prev_phase[cur] != 0) {
                    continue;
                }

                std::vector<NeighborSample> samples;
                int total_weight = 0;

                for(const auto& [dy, dx] : neighbors) {
                    const int ny = i + dy;
                    const int nx = j + dx;
                    if(ny < 0 || ny >= y || nx < 0 || nx >= x) {
                        continue;
                    }

                    const int nid = ny * x + nx;
                    if(prev_phase[nid] == 0) {
                        continue;
                    }

                    std::vector<float> eu = {prev_euler1[nid], prev_euler2[nid], prev_euler3[nid]};
                    samples.push_back({
                        .id = nid,
                        .weight = (std::abs(dy) + std::abs(dx) == 1) ? 2 : 1,
                        .quat = OrientationTransformation::eu2qu<std::vector<float>, QuatF>(eu)
                    });
                    total_weight += samples.back().weight;
                }

                if(samples.empty()) {
                    continue;
                }

                std::vector<int> parent(samples.size());
                for(int k = 0; k < static_cast<int>(samples.size()); k++) {
                    parent[k] = k;
                }

                for(int a = 0; a < static_cast<int>(samples.size()); a++) {
                    for(int b = a + 1; b < static_cast<int>(samples.size()); b++) {
                        if(misorientation_angle_rad(samples[a].quat, samples[b].quat) <= tolerance_rad) {
                            unite(parent, a, b);
                        }
                    }
                }

                std::map<int, std::vector<int>> clusters;
                for(int k = 0; k < static_cast<int>(samples.size()); k++) {
                    clusters[find_root(parent, k)].push_back(k);
                }

                const std::vector<int>* best_cluster = nullptr;
                int best_weight = -1;
                int best_size = -1;
                for(const auto& [root, members] : clusters) {
                    int cluster_weight = 0;
                    for(const int idx : members) {
                        cluster_weight += samples[idx].weight;
                    }

                    if(cluster_weight > best_weight || (cluster_weight == best_weight && static_cast<int>(members.size()) > best_size)) {
                        best_cluster = &members;
                        best_weight = cluster_weight;
                        best_size = static_cast<int>(members.size());
                    }
                }

                if(best_cluster == nullptr) {
                    continue;
                }
                if(static_cast<double>(best_weight) / total_weight < ratio_threshold) {
                    continue;
                }

                const int medoid = select_cluster_medoid(samples, *best_cluster);
                const int src = samples[medoid].id;
                phase[cur] = prev_phase[src];
                euler1[cur] = prev_euler1[src];
                euler2[cur] = prev_euler2[src];
                euler3[cur] = prev_euler3[src];
                changed = true;
            }
        }
    } while(changed);
}
