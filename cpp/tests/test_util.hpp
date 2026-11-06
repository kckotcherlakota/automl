#pragma once
#include <vector>
#include "tca/common.hpp"

namespace tt {
using tca::Bar;
constexpr int64_t kNov4 = 1793750400;  // 2026-11-04T00:00:00Z (Wed, EST)
constexpr int64_t kNov6 = 1793923200;  // 2026-11-06T00:00:00Z (Fri, EST)
constexpr int64_t kOpenUtc = 14 * 3600 + 30 * 60;  // 09:30 EST

// Two flat sessions then an opening-range breakout on the third session.
// If `drop_after` the price falls through the opening-range low afterwards.
inline std::vector<Bar> orb_scenario(bool drop_after) {
  std::vector<Bar> v;
  for (int d = 0; d < 2; ++d)
    for (int k = 0; k < 78; ++k) v.push_back({kNov4 + d * 86400 + kOpenUtc + k * 300, 100, 100.2, 99.8, 100, 1000});
  const int64_t s = kNov6 + kOpenUtc;
  v.push_back({s + 0, 100.1, 100.5, 100.0, 100.3, 1000});
  v.push_back({s + 300, 100.3, 100.4, 100.05, 100.2, 1000});
  v.push_back({s + 600, 100.2, 100.45, 100.1, 100.35, 1000});
  v.push_back({s + 900, 100.35, 100.48, 100.25, 100.45, 1000});
  v.push_back({s + 1200, 100.45, 100.9, 100.4, 100.85, 3000});  // breakout bar (index 160)
  if (drop_after) {
    v.push_back({s + 1500, 100.85, 100.95, 100.7, 100.75, 1200});
    v.push_back({s + 1800, 100.6, 100.65, 99.9, 99.95, 1500});    // through ORL 100.0
    for (int k = 7; k < 78; ++k) v.push_back({s + k * 300, 99.95, 100.05, 99.9, 100.0, 1000});
  }
  return v;
}
}  // namespace tt
