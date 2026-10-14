#include "texture_figures.h"

#include <stb/stb_image_write.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <stdexcept>
#include <string>
#include <tuple>
#include <unordered_map>
#include <utility>
#include <vector>

namespace {

constexpr int kGridSize = 251;
constexpr int kFigureWidth = 1260;
constexpr int kFigureHeight = 423;
constexpr size_t kMaximumOrientations = 60000;
constexpr double kPi = 3.141592653589793238462643383279502884;
constexpr double kMinimumMrd = 0.1;

struct Vec3 {
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
};

struct Matrix3 {
    std::array<double, 9> v{};

    Vec3 multiply(Vec3 p) const
    {
        return {
            v[0] * p.x + v[1] * p.y + v[2] * p.z,
            v[3] * p.x + v[4] * p.y + v[5] * p.z,
            v[6] * p.x + v[7] * p.y + v[8] * p.z,
        };
    }

    Vec3 transpose_multiply(Vec3 p) const
    {
        return {
            v[0] * p.x + v[3] * p.y + v[6] * p.z,
            v[1] * p.x + v[4] * p.y + v[7] * p.z,
            v[2] * p.x + v[5] * p.y + v[8] * p.z,
        };
    }
};

Matrix3 bunge_matrix(float phi1, float phi, float phi2)
{
    const double c1 = std::cos(phi1), s1 = std::sin(phi1);
    const double c = std::cos(phi), s = std::sin(phi);
    const double c2 = std::cos(phi2), s2 = std::sin(phi2);
    return {{
        c1 * c2 - s1 * s2 * c, s1 * c2 + c1 * s2 * c, s2 * s,
        -c1 * s2 - s1 * c2 * c, -s1 * s2 + c1 * c2 * c, c2 * s,
        s1 * s, -c1 * s, c,
    }};
}

Vec3 normalized(Vec3 p)
{
    const double length = std::sqrt(p.x * p.x + p.y * p.y + p.z * p.z);
    if(!std::isfinite(length) || length <= 1e-12) return {};
    return {p.x / length, p.y / length, p.z / length};
}

size_t point_count(const EbsdData& data)
{
    const size_t count = data.phase.size();
    if(data.euler1.size() != count || data.euler2.size() != count || data.euler3.size() != count) {
        throw std::invalid_argument("Euler-angle and phase arrays have different sizes");
    }
    return count;
}

template <typename Callback>
size_t for_each_orientation(const EbsdData& data, Callback callback)
{
    const size_t count = point_count(data);
    size_t valid_count = 0;
    for(uint8_t phase : data.phase) {
        if(static_cast<int>(phase) == data.primary_phase_index) ++valid_count;
    }
    if(valid_count == 0) throw std::invalid_argument("EBSD scan has no indexed orientations in its primary phase");

    // This is intentionally identical to numpy.linspace(..., dtype=int), used
    // by the former Python implementation when a scan has more than 60k points.
    std::vector<size_t> selected;
    if(valid_count <= kMaximumOrientations) {
        selected.reserve(valid_count);
        for(size_t index = 0; index < count; ++index) {
            if(static_cast<int>(data.phase[index]) == data.primary_phase_index) selected.push_back(index);
        }
    } else {
        std::vector<size_t> valid;
        valid.reserve(valid_count);
        for(size_t index = 0; index < count; ++index) {
            if(static_cast<int>(data.phase[index]) == data.primary_phase_index) valid.push_back(index);
        }
        selected.reserve(kMaximumOrientations);
        for(size_t index = 0; index < kMaximumOrientations; ++index) {
            const double position = static_cast<double>(index) * (valid.size() - 1) / (kMaximumOrientations - 1);
            selected.push_back(valid[static_cast<size_t>(position)]);
        }
    }

    size_t used = 0;
    for(size_t index : selected) {
        if(!std::isfinite(data.euler1[index]) || !std::isfinite(data.euler2[index]) || !std::isfinite(data.euler3[index])) continue;
        callback(bunge_matrix(data.euler1[index], data.euler2[index], data.euler3[index]));
        ++used;
    }
    if(used == 0) throw std::invalid_argument("EBSD scan has no finite orientations in its primary phase");
    return used;
}

TextureDensityMap density_map(int width = kGridSize, int height = kGridSize)
{
    TextureDensityMap result;
    result.width = width;
    result.height = height;
    result.values.assign(static_cast<size_t>(width) * height, 0.0);
    result.mask.assign(static_cast<size_t>(width) * height, 0);
    return result;
}

constexpr int kSphereSteps = 45; // ceil(90 / 2 degree sampling resolution)
constexpr int kSphereAzimuthCount = 180;

std::vector<Vec3> equal_area_upper_sphere()
{
    std::vector<Vec3> result;
    result.reserve(1 + kSphereSteps * kSphereAzimuthCount);
    result.push_back({0.0, 0.0, 1.0});
    for(int polar_index = 1; polar_index <= kSphereSteps; ++polar_index) {
        const double z = 1.0 - static_cast<double>(polar_index) / kSphereSteps;
        const double radius = std::sqrt(std::max(0.0, 1.0 - z * z));
        for(int azimuth_index = 0; azimuth_index < kSphereAzimuthCount; ++azimuth_index) {
            const double azimuth = 2.0 * kPi * azimuth_index / kSphereAzimuthCount;
            result.push_back({radius * std::cos(azimuth), radius * std::sin(azimuth), z});
        }
    }
    return result;
}

size_t sphere_index(int polar_index, int azimuth_index)
{
    if(polar_index <= 0) return 0;
    azimuth_index %= kSphereAzimuthCount;
    if(azimuth_index < 0) azimuth_index += kSphereAzimuthCount;
    return 1 + static_cast<size_t>(polar_index - 1) * kSphereAzimuthCount + azimuth_index;
}

size_t nearest_sphere_index(Vec3 direction, const std::vector<Vec3>& sphere)
{
    direction = normalized(direction);
    if(direction.z < 0.0) direction = {-direction.x, -direction.y, -direction.z};
    const int central_polar = std::clamp(
        static_cast<int>(std::llround((1.0 - direction.z) * kSphereSteps)),
        0,
        kSphereSteps
    );
    double azimuth = std::atan2(direction.y, direction.x);
    if(azimuth < 0.0) azimuth += 2.0 * kPi;
    const int central_azimuth = static_cast<int>(std::llround(
        azimuth * kSphereAzimuthCount / (2.0 * kPi)
    ));

    size_t best_index = 0;
    double best_dot = -std::numeric_limits<double>::infinity();
    for(int polar_index = std::max(0, central_polar - 3);
        polar_index <= std::min(kSphereSteps, central_polar + 3);
        ++polar_index) {
        if(polar_index == 0) {
            const double dot = direction.z;
            if(dot > best_dot) {
                best_dot = dot;
                best_index = 0;
            }
            continue;
        }
        for(int azimuth_offset = -2; azimuth_offset <= 2; ++azimuth_offset) {
            const size_t index = sphere_index(polar_index, central_azimuth + azimuth_offset);
            const Vec3& candidate = sphere[index];
            const double dot = direction.x * candidate.x + direction.y * candidate.y + direction.z * candidate.z;
            if(dot > best_dot) {
                best_dot = dot;
                best_index = index;
            }
        }
    }
    return best_index;
}

struct WeightedSpherePoint {
    Vec3 direction;
    double weight;
};

class SphereNeighborhoodIndex {
public:
    SphereNeighborhoodIndex(std::vector<WeightedSpherePoint> points, double radius)
    : points_(std::move(points))
    , radius_(radius)
    , cells_per_axis_(std::max(1, static_cast<int>(std::floor(2.0 / radius))))
    , cell_size_(2.0 / cells_per_axis_)
    , buckets_(static_cast<size_t>(cells_per_axis_) * cells_per_axis_ * cells_per_axis_)
    {
        for(size_t index = 0; index < points_.size(); ++index) {
            buckets_[bucket_id(points_[index].direction)].push_back(index);
        }
    }

    double gaussian_sum(Vec3 query, double sigma, bool include_antipodal) const
    {
        double result = gaussian_sum_near(query, sigma);
        if(include_antipodal) {
            result += gaussian_sum_near({-query.x, -query.y, -query.z}, sigma);
        }
        return result;
    }

private:
    int coordinate_cell(double value) const
    {
        return std::clamp(static_cast<int>(std::floor((value + 1.0) / cell_size_)), 0, cells_per_axis_ - 1);
    }

    size_t bucket_id(int x, int y, int z) const
    {
        return (static_cast<size_t>(z) * cells_per_axis_ + y) * cells_per_axis_ + x;
    }

    size_t bucket_id(Vec3 point) const
    {
        return bucket_id(coordinate_cell(point.x), coordinate_cell(point.y), coordinate_cell(point.z));
    }

    double gaussian_sum_near(Vec3 query, double sigma) const
    {
        const int center_x = coordinate_cell(query.x);
        const int center_y = coordinate_cell(query.y);
        const int center_z = coordinate_cell(query.z);
        const int cell_radius = std::max(1, static_cast<int>(std::ceil(radius_ / cell_size_)));
        const double radius_squared = radius_ * radius_ + 1e-15;
        double sum = 0.0;
        for(int z = std::max(0, center_z - cell_radius); z <= std::min(cells_per_axis_ - 1, center_z + cell_radius); ++z) {
            for(int y = std::max(0, center_y - cell_radius); y <= std::min(cells_per_axis_ - 1, center_y + cell_radius); ++y) {
                for(int x = std::max(0, center_x - cell_radius); x <= std::min(cells_per_axis_ - 1, center_x + cell_radius); ++x) {
                    for(size_t point_index : buckets_[bucket_id(x, y, z)]) {
                        const auto& point = points_[point_index];
                        const double dx = point.direction.x - query.x;
                        const double dy = point.direction.y - query.y;
                        const double dz = point.direction.z - query.z;
                        if(dx * dx + dy * dy + dz * dz > radius_squared) continue;
                        const double dot = point.direction.x * query.x + point.direction.y * query.y + point.direction.z * query.z;
                        const double angle = std::acos(std::clamp(dot, -1.0, 1.0));
                        sum += std::exp(-0.5 * (angle / sigma) * (angle / sigma)) * point.weight;
                    }
                }
            }
        }
        return sum;
    }

    std::vector<WeightedSpherePoint> points_;
    double radius_;
    int cells_per_axis_;
    double cell_size_;
    std::vector<std::vector<size_t>> buckets_;
};

std::vector<WeightedSpherePoint> occupied_sphere_points(
    const std::vector<Vec3>& sphere,
    const std::vector<double>& weights
)
{
    std::vector<WeightedSpherePoint> result;
    for(size_t index = 0; index < sphere.size(); ++index) {
        if(weights[index] > 0.0) result.push_back({sphere[index], weights[index]});
    }
    return result;
}

void normalize_density(TextureDensityMap& map)
{
    double sum = 0.0;
    size_t count = 0;
    for(size_t index = 0; index < map.values.size(); ++index) {
        if(!map.mask[index]) {
            map.values[index] = std::numeric_limits<double>::quiet_NaN();
            continue;
        }
        sum += map.values[index];
        ++count;
    }
    const double mean = count > 0 ? sum / count : 0.0;
    if(mean > 0.0) {
        for(size_t index = 0; index < map.values.size(); ++index) {
            if(map.mask[index]) map.values[index] /= mean;
        }
    }
}

void add_bilinear(TextureDensityMap& map, double x, double y, double weight = 1.0)
{
    if(x < 0.0 || y < 0.0 || x > map.width - 1.0 || y > map.height - 1.0) return;
    const int x0 = static_cast<int>(std::floor(x));
    const int y0 = static_cast<int>(std::floor(y));
    const int x1 = std::min(x0 + 1, map.width - 1);
    const int y1 = std::min(y0 + 1, map.height - 1);
    const double fx = x - x0;
    const double fy = y - y0;
    map.values[static_cast<size_t>(y0) * map.width + x0] += weight * (1.0 - fx) * (1.0 - fy);
    map.values[static_cast<size_t>(y0) * map.width + x1] += weight * fx * (1.0 - fy);
    map.values[static_cast<size_t>(y1) * map.width + x0] += weight * (1.0 - fx) * fy;
    map.values[static_cast<size_t>(y1) * map.width + x1] += weight * fx * fy;
}

std::vector<double> gaussian_kernel(double sigma)
{
    const int radius = std::max(1, static_cast<int>(std::ceil(4.0 * sigma)));
    std::vector<double> kernel(static_cast<size_t>(radius * 2 + 1));
    double sum = 0.0;
    for(int offset = -radius; offset <= radius; ++offset) {
        const double value = std::exp(-0.5 * offset * offset / (sigma * sigma));
        kernel[static_cast<size_t>(offset + radius)] = value;
        sum += value;
    }
    for(double& value : kernel) value /= sum;
    return kernel;
}

std::vector<double> blur(const std::vector<double>& source, int width, int height, double sigma)
{
    const auto kernel = gaussian_kernel(sigma);
    const int radius = static_cast<int>(kernel.size() / 2);
    std::vector<double> temporary(source.size(), 0.0);
    std::vector<double> result(source.size(), 0.0);
    for(int y = 0; y < height; ++y) {
        for(int x = 0; x < width; ++x) {
            double value = 0.0;
            for(int offset = -radius; offset <= radius; ++offset) {
                const int sx = x + offset;
                if(sx >= 0 && sx < width) value += source[static_cast<size_t>(y) * width + sx] * kernel[static_cast<size_t>(offset + radius)];
            }
            temporary[static_cast<size_t>(y) * width + x] = value;
        }
    }
    for(int y = 0; y < height; ++y) {
        for(int x = 0; x < width; ++x) {
            double value = 0.0;
            for(int offset = -radius; offset <= radius; ++offset) {
                const int sy = y + offset;
                if(sy >= 0 && sy < height) value += temporary[static_cast<size_t>(sy) * width + x] * kernel[static_cast<size_t>(offset + radius)];
            }
            result[static_cast<size_t>(y) * width + x] = value;
        }
    }
    return result;
}

void smooth_and_normalize(TextureDensityMap& map, double sigma)
{
    std::vector<double> numeric_mask(map.mask.begin(), map.mask.end());
    const auto smoothed = blur(map.values, map.width, map.height, sigma);
    const auto support = blur(numeric_mask, map.width, map.height, sigma);
    double sum = 0.0;
    size_t count = 0;
    for(size_t index = 0; index < map.values.size(); ++index) {
        if(map.mask[index] == 0) {
            map.values[index] = std::numeric_limits<double>::quiet_NaN();
            continue;
        }
        map.values[index] = support[index] > 1e-12 ? smoothed[index] / support[index] : 0.0;
        sum += map.values[index];
        ++count;
    }
    const double mean = count > 0 ? sum / count : 0.0;
    if(mean > 0.0) {
        for(size_t index = 0; index < map.values.size(); ++index) {
            if(map.mask[index]) map.values[index] /= mean;
        }
    }
}

std::array<std::vector<Vec3>, 3> pole_families(bool hcp)
{
    if(hcp) {
        std::array<std::vector<Vec3>, 3> result;
        result[0] = {{0, 0, 1}};
        const double root3 = std::sqrt(3.0);
        result[1] = {{1, 0, 0}, {0.5, root3 / 2.0, 0}, {-0.5, root3 / 2.0, 0}};
        result[2] = {{root3 / 2.0, 0.5, 0}, {0, 1, 0}, {-root3 / 2.0, 0.5, 0}};
        return result;
    }
    return {{
        {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}},
        {{1, 1, 0}, {1, -1, 0}, {1, 0, 1}, {1, 0, -1}, {0, 1, 1}, {0, 1, -1}},
        {{1, 1, 1}, {1, 1, -1}, {1, -1, 1}, {1, -1, -1}},
    }};
}

std::array<TextureDensityMap, 3> pole_densities(const EbsdData& data)
{
    std::array<TextureDensityMap, 3> maps = {density_map(), density_map(), density_map()};
    const auto sphere = equal_area_upper_sphere();
    std::array<std::vector<double>, 3> sphere_weights = {
        std::vector<double>(sphere.size(), 0.0),
        std::vector<double>(sphere.size(), 0.0),
        std::vector<double>(sphere.size(), 0.0),
    };
    for(auto& map : maps) {
        for(int y = 0; y < kGridSize; ++y) {
            for(int x = 0; x < kGridSize; ++x) {
                const double px = -1.0 + (x + 0.5) * 2.0 / kGridSize;
                const double py = -1.0 + (y + 0.5) * 2.0 / kGridSize;
                map.mask[static_cast<size_t>(y) * kGridSize + x] = px * px + py * py <= 1.0;
            }
        }
    }
    const auto families = pole_families(data.hcp);
    for_each_orientation(data, [&](const Matrix3& orientation) {
        for(size_t family = 0; family < families.size(); ++family) {
            for(Vec3 crystal : families[family]) {
                Vec3 sample = normalized(orientation.transpose_multiply(normalized(crystal)));
                // Channel 5 display basis: right=-X, up=-Z, projection axis=+Y.
                Vec3 channel{-sample.x, -sample.z, sample.y};
                if(channel.z < 0.0) channel = {-channel.x, -channel.y, -channel.z};
                sphere_weights[family][nearest_sphere_index(channel, sphere)] += 1.0;
            }
        }
    });

    constexpr double fwhm_calibration = 1.025;
    const double sigma = (10.0 * fwhm_calibration * kPi / 180.0)
        / (2.0 * std::sqrt(2.0 * std::log(2.0)));
    const double cutoff_chord = 2.0 * std::sin(2.0 * sigma);
    for(size_t family = 0; family < maps.size(); ++family) {
        const SphereNeighborhoodIndex neighborhood(
            occupied_sphere_points(sphere, sphere_weights[family]),
            cutoff_chord
        );
        auto& map = maps[family];
        for(int y = 0; y < kGridSize; ++y) {
            for(int x = 0; x < kGridSize; ++x) {
                const size_t index = static_cast<size_t>(y) * kGridSize + x;
                if(!map.mask[index]) continue;
                const double plot_x = -1.0 + (x + 0.5) * 2.0 / kGridSize;
                const double plot_y = -1.0 + (y + 0.5) * 2.0 / kGridSize;
                const double radius_squared = plot_x * plot_x + plot_y * plot_y;
                const double radius = std::sqrt(std::max(0.0, radius_squared));
                const double z = 1.0 - radius_squared;
                const double equatorial = std::sqrt(std::max(0.0, 1.0 - z * z));
                const Vec3 query = radius > 1e-12
                    ? Vec3{plot_x * equatorial / radius, plot_y * equatorial / radius, z}
                    : Vec3{0.0, 0.0, z};
                map.values[index] = neighborhood.gaussian_sum(query, sigma, true);
            }
        }
        normalize_density(map);
    }
    return maps;
}

std::pair<double, double> hcp_ipf_coordinates(Vec3 direction)
{
    direction = normalized(direction);
    direction.z = std::fabs(direction.z);
    double angle = std::atan2(direction.y, direction.x);
    const double sector = kPi / 3.0;
    angle = std::fmod(angle, sector);
    if(angle < 0.0) angle += sector;
    if(angle > sector / 2.0) angle = sector - angle;
    const double radius = std::sqrt(std::max(0.0, 1.0 - direction.z));
    return {radius * std::sin(angle), -radius * std::cos(angle)};
}

struct IpfBounds {
    double left;
    double right;
    double bottom;
    double top;
};

IpfBounds ipf_bounds(bool hcp)
{
    if(hcp) return {-0.015, 0.515, -0.7221067811865476, 0.015};
    return {-0.015, std::sqrt((1.0 - 1.0 / std::sqrt(3.0)) / 2.0) + 0.015,
            -std::sqrt(1.0 - 1.0 / std::sqrt(2.0)) - 0.015, 0.015};
}

bool inside_ipf(double plot_x, double plot_y, bool hcp)
{
    const double projected_x = -plot_y;
    const double projected_y = plot_x;
    const double radius2 = projected_x * projected_x + projected_y * projected_y;
    if(radius2 > 1.0) return false;
    const double z = 1.0 - radius2;
    const double radius = std::sqrt(std::max(0.0, radius2));
    const double equatorial = std::sqrt(std::max(0.0, 1.0 - z * z));
    const double x = radius > 1e-12 ? projected_x * equatorial / radius : 0.0;
    const double y = radius > 1e-12 ? projected_y * equatorial / radius : 0.0;
    if(hcp) {
        return y >= -1e-10 && x >= std::sqrt(3.0) * y - 1e-10 && z >= -1e-10;
    }
    return y >= -1e-10 && x >= y - 1e-10 && z >= x - 1e-10;
}

std::array<TextureDensityMap, 3> ipf_densities(const EbsdData& data)
{
    std::array<TextureDensityMap, 3> maps = {density_map(), density_map(), density_map()};
    const IpfBounds bounds = ipf_bounds(data.hcp);
    for(auto& map : maps) {
        for(int y = 0; y < kGridSize; ++y) {
            const double plot_y = bounds.bottom + (bounds.top - bounds.bottom) * y / (kGridSize - 1);
            for(int x = 0; x < kGridSize; ++x) {
                const double plot_x = bounds.left + (bounds.right - bounds.left) * x / (kGridSize - 1);
                map.mask[static_cast<size_t>(y) * kGridSize + x] = inside_ipf(plot_x, plot_y, data.hcp);
            }
        }
    }
    constexpr std::array<Vec3, 3> sample_directions = {Vec3{1, 0, 0}, Vec3{0, 1, 0}, Vec3{0, 0, 1}};

    if(!data.hcp) {
        const auto sphere = equal_area_upper_sphere();
        std::array<std::vector<double>, 3> sphere_weights = {
            std::vector<double>(sphere.size(), 0.0),
            std::vector<double>(sphere.size(), 0.0),
            std::vector<double>(sphere.size(), 0.0),
        };
        constexpr std::array<std::array<int, 3>, 6> permutations = {{
            {{0, 1, 2}}, {{0, 2, 1}}, {{1, 0, 2}},
            {{1, 2, 0}}, {{2, 0, 1}}, {{2, 1, 0}},
        }};
        constexpr std::array<double, 2> signs = {-1.0, 1.0};
        for_each_orientation(data, [&](const Matrix3& orientation) {
            for(size_t direction = 0; direction < sample_directions.size(); ++direction) {
                const Vec3 crystal = normalized(orientation.multiply(sample_directions[direction]));
                const std::array<double, 3> raw = {
                    std::fabs(crystal.x), std::fabs(crystal.y), std::fabs(crystal.z)
                };
                for(const auto& permutation : permutations) {
                    for(double sign_x : signs) {
                        for(double sign_y : signs) {
                            const Vec3 signed_direction = {
                                sign_x * raw[permutation[0]],
                                sign_y * raw[permutation[1]],
                                raw[permutation[2]],
                            };
                            sphere_weights[direction][nearest_sphere_index(signed_direction, sphere)] += 1.0;
                        }
                    }
                }
            }
        });

        constexpr double fwhm_calibration = 1.085;
        const double sigma = (10.0 * fwhm_calibration * kPi / 180.0)
            / (2.0 * std::sqrt(2.0 * std::log(2.0)));
        const double cutoff_chord = 2.0 * std::sin(2.0 * sigma);
        for(size_t direction = 0; direction < maps.size(); ++direction) {
            const SphereNeighborhoodIndex neighborhood(
                occupied_sphere_points(sphere, sphere_weights[direction]),
                cutoff_chord
            );
            auto& map = maps[direction];
            for(int y = 0; y < kGridSize; ++y) {
                const double plot_y = bounds.bottom + (bounds.top - bounds.bottom) * y / (kGridSize - 1);
                for(int x = 0; x < kGridSize; ++x) {
                    const size_t index = static_cast<size_t>(y) * kGridSize + x;
                    if(!map.mask[index]) continue;
                    const double plot_x = bounds.left + (bounds.right - bounds.left) * x / (kGridSize - 1);
                    const double projected_x = -plot_y;
                    const double projected_y = plot_x;
                    const double radius_squared = projected_x * projected_x + projected_y * projected_y;
                    const double radius = std::sqrt(std::max(0.0, radius_squared));
                    const double z = 1.0 - radius_squared;
                    const double equatorial = std::sqrt(std::max(0.0, 1.0 - z * z));
                    const Vec3 query = radius > 1e-12
                        ? Vec3{projected_x * equatorial / radius, projected_y * equatorial / radius, z}
                        : Vec3{0.0, 0.0, z};
                    map.values[index] = neighborhood.gaussian_sum(query, sigma, false);
                }
            }
            normalize_density(map);
        }
        return maps;
    }

    for_each_orientation(data, [&](const Matrix3& orientation) {
        for(size_t direction = 0; direction < sample_directions.size(); ++direction) {
            const Vec3 crystal = orientation.multiply(sample_directions[direction]);
            const auto [plot_x, plot_y] = hcp_ipf_coordinates(crystal);
            add_bilinear(
                maps[direction],
                (plot_x - bounds.left) / (bounds.right - bounds.left) * (kGridSize - 1),
                (plot_y - bounds.bottom) / (bounds.top - bounds.bottom) * (kGridSize - 1)
            );
        }
    });
    for(auto& map : maps) smooth_and_normalize(map, 18.0);
    return maps;
}

std::pair<double, double> density_limits(const std::array<TextureDensityMap, 3>& maps, bool fixed_minimum)
{
    double low = std::numeric_limits<double>::infinity();
    double high = 0.0;
    for(const auto& map : maps) {
        for(size_t index = 0; index < map.values.size(); ++index) {
            if(!map.mask[index] || !std::isfinite(map.values[index])) continue;
            low = std::min(low, map.values[index]);
            high = std::max(high, map.values[index]);
        }
    }
    if(!std::isfinite(low)) low = 0.0;
    if(fixed_minimum) low = kMinimumMrd;
    if(high <= low) high = low + std::numeric_limits<double>::epsilon();
    return {low, high};
}

std::array<uint8_t, 3> density_color(double normalized_value)
{
    normalized_value = std::clamp(normalized_value, 0.0, 1.0);
    constexpr std::array<std::array<double, 4>, 4> stops = {{
        {0.00, 0.0, 0.0, 255.0},
        {0.34, 0.0, 255.0, 0.0},
        {0.72, 255.0, 255.0, 0.0},
        {1.00, 255.0, 0.0, 0.0},
    }};
    for(size_t index = 1; index < stops.size(); ++index) {
        if(normalized_value > stops[index][0]) continue;
        const auto& a = stops[index - 1];
        const auto& b = stops[index];
        const double t = (normalized_value - a[0]) / (b[0] - a[0]);
        return {
            static_cast<uint8_t>(std::llround(a[1] + (b[1] - a[1]) * t)),
            static_cast<uint8_t>(std::llround(a[2] + (b[2] - a[2]) * t)),
            static_cast<uint8_t>(std::llround(a[3] + (b[3] - a[3]) * t)),
        };
    }
    return {255, 0, 0};
}

void set_pixel(TextureFigure& figure, int x, int y, std::array<uint8_t, 3> color)
{
    if(x < 0 || y < 0 || x >= figure.width || y >= figure.height) return;
    const size_t offset = (static_cast<size_t>(y) * figure.width + x) * 3;
    figure.rgb[offset] = color[0];
    figure.rgb[offset + 1] = color[1];
    figure.rgb[offset + 2] = color[2];
}

void line(TextureFigure& figure, int x0, int y0, int x1, int y1, std::array<uint8_t, 3> color)
{
    const int dx = std::abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
    const int dy = -std::abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
    int error = dx + dy;
    while(true) {
        set_pixel(figure, x0, y0, color);
        if(x0 == x1 && y0 == y1) break;
        const int twice = 2 * error;
        if(twice >= dy) { error += dy; x0 += sx; }
        if(twice <= dx) { error += dx; y0 += sy; }
    }
}

const std::array<uint8_t, 7>& glyph(char ch)
{
    static const std::unordered_map<char, std::array<uint8_t, 7>> glyphs = {
        {'0',{14,17,19,21,25,17,14}}, {'1',{4,12,4,4,4,4,14}}, {'2',{14,17,1,2,4,8,31}},
        {'3',{30,1,1,14,1,1,30}}, {'4',{2,6,10,18,31,2,2}}, {'5',{31,16,16,30,1,1,30}},
        {'6',{14,16,16,30,17,17,14}}, {'7',{31,1,2,4,8,8,8}}, {'8',{14,17,17,14,17,17,14}},
        {'9',{14,17,17,15,1,1,14}}, {'X',{17,17,10,4,10,17,17}}, {'Y',{17,17,10,4,4,4,4}},
        {'Z',{31,1,2,4,8,16,31}}, {'M',{17,27,21,21,17,17,17}}, {'R',{30,17,17,30,20,18,17}},
        {'D',{30,17,17,17,17,17,30}}, {'{',{6,8,8,16,8,8,6}}, {'}',{12,2,2,1,2,2,12}},
        {'[',{14,8,8,8,8,8,14}}, {']',{14,2,2,2,2,2,14}}, {'-',{0,0,0,31,0,0,0}},
        {'.',{0,0,0,0,0,12,12}}, {' ',{0,0,0,0,0,0,0}},
    };
    static const std::array<uint8_t, 7> unknown = {0,0,0,0,0,0,0};
    const auto found = glyphs.find(ch);
    return found == glyphs.end() ? unknown : found->second;
}

void text(TextureFigure& figure, int x, int y, const std::string& value, int scale = 1)
{
    int cursor = x;
    for(char ch : value) {
        const auto& bitmap = glyph(static_cast<char>(std::toupper(static_cast<unsigned char>(ch))));
        for(int row = 0; row < 7; ++row) {
            for(int column = 0; column < 5; ++column) {
                if((bitmap[row] & (1U << (4 - column))) == 0) continue;
                for(int yy = 0; yy < scale; ++yy) for(int xx = 0; xx < scale; ++xx) set_pixel(figure, cursor + column * scale + xx, y + row * scale + yy, {0,0,0});
            }
        }
        cursor += 6 * scale;
    }
}

void paint_density(
    TextureFigure& figure,
    const TextureDensityMap& map,
    int left,
    int top,
    int width,
    int height,
    double low,
    double high,
    bool origin_lower,
    int contour_levels = 0
)
{
    for(int y = 0; y < height; ++y) {
        double source_y = static_cast<double>(y) * (map.height - 1) / std::max(1, height - 1);
        if(origin_lower) source_y = map.height - 1 - source_y;
        const int y0 = std::clamp(static_cast<int>(std::floor(source_y)), 0, map.height - 1);
        const int y1 = std::min(y0 + 1, map.height - 1);
        const double fy = source_y - y0;
        for(int x = 0; x < width; ++x) {
            const double source_x = static_cast<double>(x) * (map.width - 1) / std::max(1, width - 1);
            const int x0 = std::clamp(static_cast<int>(std::floor(source_x)), 0, map.width - 1);
            const int x1 = std::min(x0 + 1, map.width - 1);
            const double fx = source_x - x0;
            const int nearest_x = std::clamp(static_cast<int>(std::llround(source_x)), 0, map.width - 1);
            const int nearest_y = std::clamp(static_cast<int>(std::llround(source_y)), 0, map.height - 1);
            const size_t index = static_cast<size_t>(nearest_y) * map.width + nearest_x;
            if(!map.mask[index]) continue;
            const auto value_at = [&](int sample_x, int sample_y) {
                const size_t sample_index = static_cast<size_t>(sample_y) * map.width + sample_x;
                return map.mask[sample_index] ? map.values[sample_index] : map.values[index];
            };
            const double top_value = value_at(x0, y0) * (1.0 - fx) + value_at(x1, y0) * fx;
            const double bottom_value = value_at(x0, y1) * (1.0 - fx) + value_at(x1, y1) * fx;
            double normalized_value = ((top_value * (1.0 - fy) + bottom_value * fy) - low) / (high - low);
            if(contour_levels > 1) {
                normalized_value = std::floor(std::clamp(normalized_value, 0.0, 1.0) * (contour_levels - 1)) / (contour_levels - 1);
            }
            set_pixel(figure, left + x, top + y, density_color(normalized_value));
        }
    }
}

void draw_colorbar(TextureFigure& figure, int x, int y, int width, int height, double low, double high, int contour_levels = 0)
{
    for(int row = 0; row < height; ++row) {
        double normalized_value = 1.0 - static_cast<double>(row) / std::max(1, height - 1);
        if(contour_levels > 1) {
            normalized_value = std::floor(normalized_value * (contour_levels - 1)) / (contour_levels - 1);
        }
        for(int column = 0; column < width; ++column) set_pixel(figure, x + column, y + row, density_color(normalized_value));
    }
    line(figure, x - 1, y - 1, x + width, y - 1, {0,0,0});
    line(figure, x - 1, y + height, x + width, y + height, {0,0,0});
    line(figure, x - 1, y - 1, x - 1, y + height, {0,0,0});
    line(figure, x + width, y - 1, x + width, y + height, {0,0,0});
    text(figure, x + width + 5, y - 3, std::to_string(high).substr(0, 4), 2);
    text(figure, x + width + 5, y + height - 13, std::to_string(low).substr(0, 4), 2);
    text(figure, x + width + 5, y + height / 2 - 7, "MRD", 2);
}

TextureFigure blank_figure(std::array<TextureDensityMap, 3> maps, double low, double high)
{
    TextureFigure result;
    result.width = kFigureWidth;
    result.height = kFigureHeight;
    result.rgb.assign(static_cast<size_t>(result.width) * result.height * 3, 255);
    result.densities = std::move(maps);
    result.density_min = low;
    result.density_max = high;
    return result;
}

void write_png(const std::filesystem::path& path, const TextureFigure& figure)
{
    if(!path.parent_path().empty()) std::filesystem::create_directories(path.parent_path());
    if(stbi_write_png(path.string().c_str(), figure.width, figure.height, 3, figure.rgb.data(), figure.width * 3) == 0) {
        throw std::runtime_error("Failed to write texture figure PNG");
    }
}

} // namespace

TextureFigure render_pole_figure(const EbsdData& data)
{
    auto maps = pole_densities(data);
    const auto [low, high] = density_limits(maps, true);
    TextureFigure figure = blank_figure(std::move(maps), low, high);
    constexpr std::array<int, 3> left = {34, 408, 782};
    constexpr int top = 68;
    constexpr int dimension = 294;
    const auto labels = data.hcp
        ? std::array<std::string, 3>{"{0001}", "{10-10}", "{11-20}"}
        : std::array<std::string, 3>{"{100}", "{110}", "{111}"};
    for(size_t index = 0; index < 3; ++index) {
        paint_density(figure, figure.densities[index], left[index], top, dimension, dimension, low, high, true);
        const int center_x = left[index] + dimension / 2;
        const int center_y = top + dimension / 2;
        const int radius = dimension / 2;
        for(int step = 0; step < 2000; ++step) {
            const double angle = 2.0 * kPi * step / 2000.0;
            set_pixel(figure, center_x + static_cast<int>(std::llround(radius * std::cos(angle))), center_y - static_cast<int>(std::llround(radius * std::sin(angle))), {0,0,0});
        }
        line(figure, left[index], center_y, left[index] + dimension, center_y, {60,60,60});
        line(figure, center_x, top, center_x, top + dimension, {60,60,60});
        text(figure, center_x - static_cast<int>(labels[index].size()) * 6, 38, labels[index], 2);
        text(figure, left[index] + dimension - 24, center_y - 14, "X0", 2);
        text(figure, center_x - 12, top - 18, "Z0", 2);
    }
    draw_colorbar(figure, 1120, 18, 17, 391, low, high);
    return figure;
}

TextureFigure render_inverse_pole_figure(const EbsdData& data)
{
    auto maps = ipf_densities(data);
    const auto [low, high] = density_limits(maps, data.hcp);
    TextureFigure figure = blank_figure(std::move(maps), low, high);
    constexpr std::array<int, 3> left = {26, 400, 774};
    constexpr int top = 38;
    constexpr int width = 311;
    constexpr int height = 361;
    constexpr std::array<const char*, 3> labels = {"X0", "Y0", "Z0"};
    for(size_t index = 0; index < 3; ++index) {
        paint_density(figure, figure.densities[index], left[index], top, width, height, low, high, true, 32);
        text(figure, left[index] + width / 2 - 10, 17, labels[index], 2);
        text(figure, left[index] - 3, top - 18, data.hcp ? "0001" : "001", 2);
        text(figure, left[index] - 3, top + height + 2, data.hcp ? "10-10" : "101", 2);
        text(figure, left[index] + width - (data.hcp ? 58 : 34), top + static_cast<int>(height * 0.832) - 8, data.hcp ? "11-20" : "111", 2);
    }
    draw_colorbar(figure, 1120, 30, 17, 375, low, high, 32);
    return figure;
}

void save_pole_figure(const std::filesystem::path& ebsd_path, const std::filesystem::path& image_path)
{
    write_png(image_path, render_pole_figure(load_ebsd_data(ebsd_path)));
}

void save_inverse_pole_figure(const std::filesystem::path& ebsd_path, const std::filesystem::path& image_path)
{
    write_png(image_path, render_inverse_pole_figure(load_ebsd_data(ebsd_path)));
}
