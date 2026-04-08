#include "orientations.h"

#include <EbsdLib/Core/OrientationTransformation.hpp>
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <EbsdLib/Core/OrientationTransformation.hpp>

#include "EbsdLib/Math/Matrix3X1.hpp"
#include "EbsdLib/Core/Orientation.hpp"

namespace {

double clampDot(double v)
{
    return std::clamp(v, 0.0, 1.0);
}

double angleBetweenUnitVectorsDeg(const EbsdLib::Matrix3X1D& a, const EbsdLib::Matrix3X1D& b)
{
    double dot = a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
    dot = std::fabs(dot); // 不分正反方向，視為同一晶向軸
    dot = clampDot(dot);
    return std::acos(dot) * 180.0 / EbsdLib::Constants::k_PiD;
}

struct FamilyCandidate
{
    int familyId; // 0={001}, 1={110}, 2={111}
    EbsdLib::Matrix3X1D dir;
};

double minAngleToFamily(const EbsdLib::Matrix3X1D& p, const std::vector<FamilyCandidate>& candidates, int familyId)
{
    double best = 180.0;
    for(const auto& c : candidates)
    {
        if(c.familyId != familyId)
        {
            continue;
        }

        double ang = angleBetweenUnitVectorsDeg(p, c.dir);
        if(ang < best)
        {
            best = ang;
        }
    }
    return best;
}

} // namespace

std::tuple<double, double, double> Orientations_finder::ratio(double misorientation_threshold, std::array<double, 3> sampleDir) {
    if(grains.empty()) {
        throw std::invalid_argument("Orientations_finder::ratio: grains is empty");
    }

    // normalize sample direction
    EbsdLib::Matrix3X1D refDir(sampleDir[0], sampleDir[1], sampleDir[2]);
    refDir = refDir.normalize();

    const double r2 = EbsdLib::Constants::k_1OverRoot2D;
    const double r3 = EbsdLib::Constants::k_1OverRoot3D;

    const std::vector<FamilyCandidate> candidates = {
            // {001}
            {0, EbsdLib::Matrix3X1D(1.0, 0.0, 0.0)},
            {0, EbsdLib::Matrix3X1D(0.0, 1.0, 0.0)},
            {0, EbsdLib::Matrix3X1D(0.0, 0.0, 1.0)},

            // {110}
            {1, EbsdLib::Matrix3X1D( r2,  r2, 0.0)},
            {1, EbsdLib::Matrix3X1D( r2, 0.0,  r2)},
            {1, EbsdLib::Matrix3X1D(0.0,  r2,  r2)},
            {1, EbsdLib::Matrix3X1D(-r2,  r2, 0.0)},
            {1, EbsdLib::Matrix3X1D(-r2, 0.0,  r2)},
            {1, EbsdLib::Matrix3X1D(0.0, -r2,  r2)},

            // {111}
            {2, EbsdLib::Matrix3X1D( r3,  r3,  r3)},
            {2, EbsdLib::Matrix3X1D(-r3,  r3,  r3)},
            {2, EbsdLib::Matrix3X1D( r3, -r3,  r3)},
            {2, EbsdLib::Matrix3X1D( r3,  r3, -r3)},
    };

    double area001 = 0.0;
    double area110 = 0.0;
    double area111 = 0.0;
    double totalArea = 0.0;
    double acceptedArea = 0.0;

    for(const auto& g : grains) {

        const double w = g.size;
        totalArea += w;

        // quaternion -> euler -> orientation matrix (keep consistent with orientation_ratio)
        std::vector<float> euler = OrientationTransformation::qu2eu<QuatF, std::vector<float>>(g.orientation);
        OrientationD eu(euler[0], euler[1], euler[2]);
        OrientationD om = OrientationTransformation::eu2om<OrientationD, OrientationD>(eu);
        EbsdLib::Matrix3X3D mat(om.data());
        EbsdLib::Matrix3X1D p = (mat * refDir).normalize();

        double ang001 = minAngleToFamily(p, candidates, 0);
        double ang110 = minAngleToFamily(p, candidates, 1);
        double ang111 = minAngleToFamily(p, candidates, 2);

        double bestAngle = ang001;
        int bestFamily = 0;
        if(ang110 < bestAngle) {
            bestAngle = ang110;
            bestFamily = 1;
        }
        if(ang111 < bestAngle) {
            bestAngle = ang111;
            bestFamily = 2;
        }

        if(bestAngle <= misorientation_threshold) {
            acceptedArea += w;
            if(bestFamily == 0){
                area001 += w;
            }
            else if(bestFamily == 1) {
                area110 += w;
            }
            else {
                area111 += w;
            }
        }
    }

    if(totalArea <= 0.0) {
        throw std::runtime_error("Orientations_finder::ratio: totalArea is zero");
    }
    if(acceptedArea <= 0.0) {
        throw std::runtime_error("No grain orientations within the misorientation threshold");
    }

    return {area001 / totalArea, area110 / totalArea, area111 / totalArea};
}

std::tuple<double, double, double> Orientations_finder_by_pixel::ratio(double misorientation_threshold, std::array<double, 3> sampleDir) {
    // normalize sample direction
    EbsdLib::Matrix3X1D refDir(sampleDir[0], sampleDir[1], sampleDir[2]);
    refDir = refDir.normalize();

    const double r2 = EbsdLib::Constants::k_1OverRoot2D;
    const double r3 = EbsdLib::Constants::k_1OverRoot3D;

    const std::vector<FamilyCandidate> candidates = {
            // {001}
            {0, EbsdLib::Matrix3X1D(1.0, 0.0, 0.0)},
            {0, EbsdLib::Matrix3X1D(0.0, 1.0, 0.0)},
            {0, EbsdLib::Matrix3X1D(0.0, 0.0, 1.0)},

            // {110}
            {1, EbsdLib::Matrix3X1D( r2,  r2, 0.0)},
            {1, EbsdLib::Matrix3X1D( r2, 0.0,  r2)},
            {1, EbsdLib::Matrix3X1D(0.0,  r2,  r2)},
            {1, EbsdLib::Matrix3X1D(-r2,  r2, 0.0)},
            {1, EbsdLib::Matrix3X1D(-r2, 0.0,  r2)},
            {1, EbsdLib::Matrix3X1D(0.0, -r2,  r2)},

            // {111}
            {2, EbsdLib::Matrix3X1D( r3,  r3,  r3)},
            {2, EbsdLib::Matrix3X1D(-r3,  r3,  r3)},
            {2, EbsdLib::Matrix3X1D( r3, -r3,  r3)},
            {2, EbsdLib::Matrix3X1D( r3,  r3, -r3)},
    };

    double area001 = 0.0;
    double area110 = 0.0;
    double area111 = 0.0;
    double totalArea = 0.0;
    double acceptedArea = 0.0;

    for(int i = 0; i < Xdim * Ydim; i++) {
        if(phase[i] == 0) continue; // noindex
        int w = 1;
        
        totalArea += w;

        // quaternion -> euler -> orientation matrix (keep consistent with orientation_ratio)
        // std::vector<float> euler = OrientationTransformation::qu2eu<QuatF, std::vector<float>>(g.orientation);
        OrientationD eu(euler1[i], euler2[i], euler3[i]);
        OrientationD om = OrientationTransformation::eu2om<OrientationD, OrientationD>(eu);
        EbsdLib::Matrix3X3D mat(om.data());
        EbsdLib::Matrix3X1D p = (mat * refDir).normalize();

        double ang001 = minAngleToFamily(p, candidates, 0);
        double ang110 = minAngleToFamily(p, candidates, 1);
        double ang111 = minAngleToFamily(p, candidates, 2);

        double bestAngle = ang001;
        int bestFamily = 0;
        if(ang110 < bestAngle) {
            bestAngle = ang110;
            bestFamily = 1;
        }
        if(ang111 < bestAngle) {
            bestAngle = ang111;
            bestFamily = 2;
        }

        if(bestAngle <= misorientation_threshold) {
            acceptedArea += w;
            if(bestFamily == 0){
                area001 += w;
            }
            else if(bestFamily == 1) {
                area110 += w;
            }
            else {
                area111 += w;
            }
        }
    }

    if(totalArea <= 0.0) {
        throw std::runtime_error("Orientations_finder::ratio: totalArea is zero");
    }
    if(acceptedArea <= 0.0) {
        throw std::runtime_error("No grain orientations within the misorientation threshold");
    }

    return {area001 / totalArea, area110 / totalArea, area111 / totalArea};
}
