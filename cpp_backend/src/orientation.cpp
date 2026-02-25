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
#include "ebsd.h"

using std::vector;
using std::pair;
using std::swap;
using std::cout;
using std::endl;
using std::operator""s;


std::tuple<double, double, double> orientation_ratio(const float* euler1,
                                    const float* euler2,
                                    const float* euler3,
                                    size_t nPoints,
                                    const uint8_t* phase,
                                    uint8_t targetPhase,
                                    std::array<double, 3> sampleDir)
{
  if(euler1 == nullptr || euler2 == nullptr || euler3 == nullptr || nPoints == 0) {
    return {0.0, 0.0, 0.0};
  }

  // normalize sampleDir
  {
    const double n = std::sqrt(sampleDir[0]*sampleDir[0] + sampleDir[1]*sampleDir[1] + sampleDir[2]*sampleDir[2]);
    if(n <= 0.0) return {0.0, 0.0, 0.0};
    sampleDir[0] /= n; sampleDir[1] /= n; sampleDir[2] /= n;
  }

  // Eulers: N tuples, 3 comps
  EbsdLib::FloatArrayType::comp_dims_type cDims = {3};
  auto eulers = EbsdLib::FloatArrayType::CreateArray(nPoints, cDims, "Eulers", true);

  for(size_t i = 0; i < nPoints; ++i) {
    (*eulers)[3 * i + 0] = euler1[i]; // phi1
    (*eulers)[3 * i + 1] = euler2[i]; // Phi
    (*eulers)[3 * i + 2] = euler3[i]; // phi2
  }

  // 取 CubicOps（如果你不想假設 index，可改成 dynamic_cast 掃描）
  auto ops = LaueOps::GetAllOrientationOps()[1];
  auto cubic = std::dynamic_pointer_cast<CubicOps>(ops);
  if(!cubic) {
    return {0.0, 0.0, 0.0};
  }

  // 用內建函式產生 <001>/<011>/<111> family 極點座標
  auto xyz001 = EbsdLib::FloatArrayType::CreateArray(1, cDims, "xyz001", true);
  auto xyz011 = EbsdLib::FloatArrayType::CreateArray(1, cDims, "xyz011", true);
  auto xyz111 = EbsdLib::FloatArrayType::CreateArray(1, cDims, "xyz111", true);

  cubic->generateSphereCoordsFromEulers(eulers.get(), xyz001.get(), xyz011.get(), xyz111.get());

  // 各 family 對稱數量（不要硬編）
  auto symSizes = cubic->getNumSymmetry();
  const size_t s001 = static_cast<size_t>(symSizes[0]);
  const size_t s011 = static_cast<size_t>(symSizes[1]);
  const size_t s111 = static_cast<size_t>(symSizes[2]);

  auto dot3 = [](double x, double y, double z, const std::array<double,3>& d) {
    return x*d[0] + y*d[1] + z*d[2];
  };

  size_t c001 = 0, c011 = 0, c111 = 0, total = 0;

  for(size_t i = 0; i < nPoints; ++i)
  {
    if(phase != nullptr && phase[i] != targetPhase) {
      continue;
    }

    double best001 = -2.0, best011 = -2.0, best111 = -2.0;

    for(size_t k = 0; k < s001; ++k) {
      const size_t idx = (i * s001 + k) * 3;
      const double d = dot3((*xyz001)[idx + 0], (*xyz001)[idx + 1], (*xyz001)[idx + 2], sampleDir);
      if(d > best001) best001 = d;
    }
    for(size_t k = 0; k < s011; ++k) {
      const size_t idx = (i * s011 + k) * 3;
      const double d = dot3((*xyz011)[idx + 0], (*xyz011)[idx + 1], (*xyz011)[idx + 2], sampleDir);
      if(d > best011) best011 = d;
    }
    for(size_t k = 0; k < s111; ++k) {
      const size_t idx = (i * s111 + k) * 3;
      const double d = dot3((*xyz111)[idx + 0], (*xyz111)[idx + 1], (*xyz111)[idx + 2], sampleDir);
      if(d > best111) best111 = d;
    }

    if(best001 >= best011 && best001 >= best111) ++c001;
    else if(best011 >= best001 && best011 >= best111) ++c011;
    else ++c111;

    ++total;
  }

  if(total == 0) return {0.0, 0.0, 0.0};

  return {
    static_cast<double>(c001) / static_cast<double>(total),
    static_cast<double>(c011) / static_cast<double>(total),
    static_cast<double>(c111) / static_cast<double>(total)
  };
}