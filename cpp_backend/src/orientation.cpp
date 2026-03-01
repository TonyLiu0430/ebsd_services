#include <EbsdLib/IO/EbsdImporter.h>
#include <EbsdLib/IO/HKL/CprReader.h>
#include <vector>
#include <memory>
#include <EbsdLib/Core/OrientationTransformation.hpp>
#include <EbsdLib/LaueOps/LaueOps.h>
#include <algorithm>
#include <ranges>
#include <map>
#include <EbsdLib/Core/EbsdDataArray.hpp>
#include <EbsdLib/LaueOps/CubicOps.h>
#include <string>
#include "EbsdLib/Math/Matrix3X1.hpp"
#include "ebsd.h"

using std::vector;
using std::pair;
using std::swap;
using std::cout;
using std::endl;
using std::operator""s;

double clampDot(double v) {
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

double minAngleToFamily(const EbsdLib::Matrix3X1D& p, const std::vector<FamilyCandidate>& candidates, int familyId) {
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

std::tuple<double, double, double> orientation_ratio(const float* euler1,
                                                     const float* euler2,
                                                     const float* euler3,
                                                     size_t nPoints,
                                                     std::array<double, 3> sampleDir,
                                                     double misorientation_threshold /* degree */) {
    if(euler1 == nullptr || euler2 == nullptr || euler3 == nullptr || nPoints == 0) {
        throw std::invalid_argument("Invalid input to orientation_ratio");
    }

    // normalize sample direction
    EbsdLib::Matrix3X1D refDir(sampleDir[0], sampleDir[1], sampleDir[2]);
    refDir = refDir.normalize();

    const double r2 = EbsdLib::Constants::k_1OverRoot2D;
    const double r3 = EbsdLib::Constants::k_1OverRoot3D;

    // 用「軸」的代表方向即可，因為後面用 abs(dot)
    // 注意：你前面程式裡寫 011 family，但實際內容是 cubic 的 <110> family
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

    size_t count001 = 0;
    size_t count110 = 0;
    size_t count111 = 0;
    size_t accepted = 0;

    for(size_t i = 0; i < nPoints; ++i) {
        OrientationD eu(euler1[i], euler2[i], euler3[i]);

        OrientationD om = OrientationTransformation::eu2om<OrientationD, OrientationD>(eu);
        EbsdLib::Matrix3X3D g(om.data());

        EbsdLib::Matrix3X1D p = (g * refDir).normalize();

        // 分別算到三個 family 的最小夾角
        double ang001 = minAngleToFamily(p, candidates, 0);
        double ang110 = minAngleToFamily(p, candidates, 1);
        double ang111 = minAngleToFamily(p, candidates, 2);

        // 找最近 family
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
            ++accepted;
            if(bestFamily == 0) {
                ++count001;
            }
            else if(bestFamily == 1) {
                ++count110;
            }
            else {
                ++count111;
            }
        }
    }

    if(accepted == 0) {
        throw std::runtime_error("No orientations within the misorientation threshold");
    }

    return {
      static_cast<double>(count001) / static_cast<double>(nPoints),
      static_cast<double>(count110) / static_cast<double>(nPoints),
      static_cast<double>(count111) / static_cast<double>(nPoints)};
}


// std::tuple<double, double, double> orientation_ratio(const float* euler1,
//                                     const float* euler2,
//                                     const float* euler3,
//                                     size_t nPoints,
//                                     const uint8_t* phase,
//                                     uint8_t targetPhase,
//                                     std::array<double, 3> sampleDir) {
//   if(euler1 == nullptr || euler2 == nullptr || euler3 == nullptr || nPoints == 0) {
//     return {0.0, 0.0, 0.0};
//   }

//   // normalize sampleDir
//   {
//     const double n = std::sqrt(sampleDir[0]*sampleDir[0] + sampleDir[1]*sampleDir[1] + sampleDir[2]*sampleDir[2]);
//     if(n <= 0.0) return {0.0, 0.0, 0.0};
//     sampleDir[0] /= n; sampleDir[1] /= n; sampleDir[2] /= n;
//   }

//   // Eulers: N tuples, 3 comps
//   EbsdLib::FloatArrayType::comp_dims_type cDims = {3};
//   auto eulers = EbsdLib::FloatArrayType::CreateArray(nPoints, cDims, "Eulers", true);

//   for(size_t i = 0; i < nPoints; ++i) {
//     (*eulers)[3 * i + 0] = euler1[i]; // phi1
//     (*eulers)[3 * i + 1] = euler2[i]; // Phi
//     (*eulers)[3 * i + 2] = euler3[i]; // phi2
//   }

//   // 取 CubicOps（如果你不想假設 index，可改成 dynamic_cast 掃描）
//   auto ops = LaueOps::GetAllOrientationOps()[1];
//   auto cubic = std::dynamic_pointer_cast<CubicOps>(ops);
//   if(!cubic) {
//     return {0.0, 0.0, 0.0};
//   }

//   // 用內建函式產生 <001>/<011>/<111> family 極點座標
//   auto xyz001 = EbsdLib::FloatArrayType::CreateArray(1, cDims, "xyz001", true);
//   auto xyz011 = EbsdLib::FloatArrayType::CreateArray(1, cDims, "xyz011", true);
//   auto xyz111 = EbsdLib::FloatArrayType::CreateArray(1, cDims, "xyz111", true);

//   cubic->generateSphereCoordsFromEulers(eulers.get(), xyz001.get(), xyz011.get(), xyz111.get());

//   // 各 family 對稱數量（不要硬編）
//   auto symSizes = cubic->getNumSymmetry();
//   const size_t s001 = static_cast<size_t>(symSizes[0]);
//   const size_t s011 = static_cast<size_t>(symSizes[1]);
//   const size_t s111 = static_cast<size_t>(symSizes[2]);

//   auto dot3 = [](double x, double y, double z, const std::array<double,3>& d) {
//     return x*d[0] + y*d[1] + z*d[2];
//   };

//   size_t c001 = 0, c011 = 0, c111 = 0, total = 0;

//   for(size_t i = 0; i < nPoints; ++i) {
//     if(phase != nullptr && phase[i] != targetPhase) {
//       continue;
//     }

//     double best001 = -2.0, best011 = -2.0, best111 = -2.0;

//     for(size_t k = 0; k < s001; ++k) {
//       const size_t idx = (i * s001 + k) * 3;
//       const double d = dot3((*xyz001)[idx + 0], (*xyz001)[idx + 1], (*xyz001)[idx + 2], sampleDir);
//       if(d > best001) best001 = d;
//     }
//     for(size_t k = 0; k < s011; ++k) {
//       const size_t idx = (i * s011 + k) * 3;
//       const double d = dot3((*xyz011)[idx + 0], (*xyz011)[idx + 1], (*xyz011)[idx + 2], sampleDir);
//       if(d > best011) best011 = d;
//     }
//     for(size_t k = 0; k < s111; ++k) {
//       const size_t idx = (i * s111 + k) * 3;
//       const double d = dot3((*xyz111)[idx + 0], (*xyz111)[idx + 1], (*xyz111)[idx + 2], sampleDir);
//       if(d > best111) best111 = d;
//     }

//     if(best001 >= best011 && best001 >= best111) ++c001;
//     else if(best011 >= best001 && best011 >= best111) ++c011;
//     else ++c111;

//     ++total;
//   }

//   if(total == 0) return {0.0, 0.0, 0.0};

//   return {
//     static_cast<double>(c001) / static_cast<double>(total),
//     static_cast<double>(c011) / static_cast<double>(total),
//     static_cast<double>(c111) / static_cast<double>(total)
//   };
// }