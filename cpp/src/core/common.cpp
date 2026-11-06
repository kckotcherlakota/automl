#include "tca/common.hpp"

#include <fstream>
#include <iostream>
#include <sstream>

namespace tca {

AssetClass parse_asset_class(const std::string& s) {
  if (s == "stock" || s == "etf" || s == "equity") return AssetClass::Stock;
  if (s == "crypto") return AssetClass::Crypto;
  throw ValidationError("unknown asset_class: " + s);
}

std::string to_string(AssetClass a) { return a == AssetClass::Stock ? "stock" : "crypto"; }

std::vector<Bar> parse_bars(const json& j) {
  if (!j.contains("bars") || !j["bars"].is_array())
    throw ValidationError("input must contain a 'bars' array");
  std::vector<Bar> out;
  out.reserve(j["bars"].size());
  for (size_t i = 0; i < j["bars"].size(); ++i) {
    const auto& b = j["bars"][i];
    Bar x;
    try {
      x.t = b.at("t").get<int64_t>();
      x.o = b.at("o").get<double>();
      x.h = b.at("h").get<double>();
      x.l = b.at("l").get<double>();
      x.c = b.at("c").get<double>();
      x.v = b.at("v").get<double>();
    } catch (const std::exception&) {
      throw ValidationError("bar " + std::to_string(i) + " missing/invalid t,o,h,l,c,v");
    }
    if (!finite_pos(x.o) || !finite_pos(x.h) || !finite_pos(x.l) || !finite_pos(x.c))
      throw ValidationError("bar " + std::to_string(i) + " has non-positive/non-finite price");
    if (!std::isfinite(x.v) || x.v < 0)
      throw ValidationError("bar " + std::to_string(i) + " has invalid volume");
    if (x.l > std::min(x.o, x.c) + 1e-12 || x.h < std::max(x.o, x.c) - 1e-12)
      throw ValidationError("bar " + std::to_string(i) + " violates low<=open/close<=high");
    if (!out.empty() && x.t <= out.back().t)
      throw ValidationError("bar " + std::to_string(i) + " timestamp not strictly increasing");
    out.push_back(x);
  }
  return out;
}

json bars_to_json(const std::vector<Bar>& bars) {
  json arr = json::array();
  for (const auto& b : bars) arr.push_back({{"t", b.t}, {"o", b.o}, {"h", b.h}, {"l", b.l}, {"c", b.c}, {"v", b.v}});
  return json{{"bars", arr}};
}

int drop_incomplete(std::vector<Bar>& bars, int64_t tf_sec, int64_t now) {
  int dropped = 0;
  while (!bars.empty() && bars.back().t + tf_sec > now) {
    bars.pop_back();
    ++dropped;
  }
  return dropped;
}

int64_t timeframe_seconds(const std::string& tf) {
  if (tf == "1m") return 60;
  if (tf == "5m") return 300;
  if (tf == "15m") return 900;
  if (tf == "1h") return 3600;
  if (tf == "1d") return 86400;
  throw ValidationError("unsupported timeframe: " + tf);
}

// ---- civil date helpers (Howard Hinnant's algorithms) ----
static int64_t days_from_civil(int64_t y, unsigned m, unsigned d) {
  y -= m <= 2;
  const int64_t era = (y >= 0 ? y : y - 399) / 400;
  const unsigned yoe = static_cast<unsigned>(y - era * 400);
  const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return era * 146097 + static_cast<int64_t>(doe) - 719468;
}

static void civil_from_days(int64_t z, int64_t& y, unsigned& m, unsigned& d) {
  z += 719468;
  const int64_t era = (z >= 0 ? z : z - 146096) / 146097;
  const unsigned doe = static_cast<unsigned>(z - era * 146097);
  const unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
  y = static_cast<int64_t>(yoe) + era * 400;
  const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
  const unsigned mp = (5 * doy + 2) / 153;
  d = doy - (153 * mp + 2) / 5 + 1;
  m = mp + (mp < 10 ? 3 : -9);
  y += (m <= 2);
}

static int64_t floor_div(int64_t a, int64_t b) { return a / b - ((a % b != 0) && ((a < 0) != (b < 0))); }

// weekday for days since epoch: 1970-01-01 was Thursday (4)
static int weekday_from_days(int64_t z) { return static_cast<int>(z >= -4 ? (z + 4) % 7 : (z + 5) % 7 + 6); }

// n-th Sunday (n>=1) of month as days since epoch
static int64_t nth_sunday(int64_t y, unsigned m, int n) {
  int64_t first = days_from_civil(y, m, 1);
  int wd = weekday_from_days(first);
  int64_t first_sun = first + ((7 - wd) % 7);
  return first_sun + 7 * (n - 1);
}

int et_offset_seconds(int64_t t) {
  int64_t days = floor_div(t, 86400);
  int64_t y; unsigned m, d;
  civil_from_days(days, y, m, d);
  // DST starts 2nd Sunday March at 02:00 EST (07:00 UTC),
  // ends 1st Sunday November at 02:00 EDT (06:00 UTC).
  int64_t start = nth_sunday(y, 3, 2) * 86400 + 7 * 3600;
  int64_t end = nth_sunday(y, 11, 1) * 86400 + 6 * 3600;
  return (t >= start && t < end) ? -4 * 3600 : -5 * 3600;
}

int et_date(int64_t t) {
  int64_t local = t + et_offset_seconds(t);
  int64_t y; unsigned m, d;
  civil_from_days(floor_div(local, 86400), y, m, d);
  return static_cast<int>(y * 10000 + m * 100 + d);
}

int et_minute_of_day(int64_t t) {
  int64_t local = t + et_offset_seconds(t);
  int64_t sod = local - floor_div(local, 86400) * 86400;
  return static_cast<int>(sod / 60);
}

int et_weekday(int64_t t) {
  int64_t local = t + et_offset_seconds(t);
  return weekday_from_days(floor_div(local, 86400));
}

double get_num(const json& j, const char* key) {
  if (!j.contains(key) || !j[key].is_number()) throw ValidationError(std::string("missing numeric field: ") + key);
  double v = j[key].get<double>();
  if (!std::isfinite(v)) throw ValidationError(std::string("non-finite field: ") + key);
  return v;
}

double get_num_or(const json& j, const char* key, double dflt) {
  if (!j.contains(key) || j[key].is_null()) return dflt;
  return get_num(j, key);
}

std::string get_str(const json& j, const char* key) {
  if (!j.contains(key) || !j[key].is_string()) throw ValidationError(std::string("missing string field: ") + key);
  return j[key].get<std::string>();
}

std::string get_str_or(const json& j, const char* key, const std::string& dflt) {
  if (!j.contains(key) || j[key].is_null()) return dflt;
  return get_str(j, key);
}

json read_json_input(const std::string& path) {
  std::stringstream ss;
  if (path.empty() || path == "-") {
    ss << std::cin.rdbuf();
  } else {
    std::ifstream f(path);
    if (!f) throw ValidationError("cannot open input file: " + path);
    ss << f.rdbuf();
  }
  try {
    return json::parse(ss.str());
  } catch (const json::parse_error& e) {
    throw ValidationError(std::string("invalid JSON: ") + e.what());
  }
}

int run_tool(int argc, char** argv, const char* name, int (*fn)(const json&, json&)) {
  std::string input = "-";
  for (int i = 1; i < argc; ++i) {
    std::string a = argv[i];
    if (a == "--input" && i + 1 < argc) {
      input = argv[++i];
    } else if (a == "--help" || a == "-h") {
      std::cout << name << " --input <file.json|->   (reads JSON, writes JSON to stdout)\n"
                << "exit codes: 0 ok/approved, 1 internal error, 2 invalid input, 3 rejected\n";
      return kOk;
    } else {
      std::cerr << name << ": unknown argument " << a << "\n";
      return kBadInput;
    }
  }
  try {
    json in = read_json_input(input);
    json out;
    int rc = fn(in, out);
    std::cout << out.dump(2) << std::endl;
    return rc;
  } catch (const ValidationError& e) {
    std::cout << json{{"tool", name}, {"error", e.what()}, {"error_type", "invalid_input"}}.dump(2) << std::endl;
    return kBadInput;
  } catch (const json::exception& e) {
    std::cout << json{{"tool", name}, {"error", e.what()}, {"error_type", "invalid_input"}}.dump(2) << std::endl;
    return kBadInput;
  } catch (const std::exception& e) {
    std::cout << json{{"tool", name}, {"error", e.what()}, {"error_type", "internal"}}.dump(2) << std::endl;
    return kInternal;
  }
}

}  // namespace tca
