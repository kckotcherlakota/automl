// Shared types, validation and I/O helpers for the trading-agent C++ tools.
#pragma once

#include <cmath>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace tca {

using json = nlohmann::json;

// Exit codes shared by every tool (documented in CLAUDE.md).
enum ExitCode : int {
  kOk = 0,           // success / approved
  kInternal = 1,     // unexpected internal error
  kBadInput = 2,     // malformed or invalid input
  kRejected = 3,     // valid input, but the request was rejected (risk / execution)
};

struct ValidationError : std::runtime_error {
  using std::runtime_error::runtime_error;
};

struct Bar {
  int64_t t = 0;  // bar OPEN time, epoch seconds UTC
  double o = 0, h = 0, l = 0, c = 0, v = 0;
};

enum class AssetClass { Stock, Crypto };

AssetClass parse_asset_class(const std::string& s);
std::string to_string(AssetClass a);

inline bool finite_pos(double x) { return std::isfinite(x) && x > 0.0; }

// Parses {"bars":[{"t","o","h","l","c","v"}...]} and validates every bar:
// finite positive prices, l <= min(o,c) <= max(o,c) <= h, v >= 0,
// strictly increasing timestamps.
std::vector<Bar> parse_bars(const json& j);
json bars_to_json(const std::vector<Bar>& bars);

// Removes a trailing bar whose close time (t + tf_sec) is after `now`
// (i.e. an incomplete candle) and returns how many bars were dropped.
int drop_incomplete(std::vector<Bar>& bars, int64_t tf_sec, int64_t now);

// Timeframe strings: "1m","5m","15m","1h","1d" -> seconds.
int64_t timeframe_seconds(const std::string& tf);

// --- US Eastern time helpers (DST rules since 2007) ---------------------
// Returns the UTC offset in seconds for America/New_York at epoch `t`.
int et_offset_seconds(int64_t t);
// Local ET calendar date as YYYYMMDD and minutes since local midnight.
int et_date(int64_t t);
int et_minute_of_day(int64_t t);
// Day of week for ET date (0=Sunday).
int et_weekday(int64_t t);

// Helpers for reading JSON with validation.
double get_num(const json& j, const char* key);
double get_num_or(const json& j, const char* key, double dflt);
std::string get_str(const json& j, const char* key);
std::string get_str_or(const json& j, const char* key, const std::string& dflt);

// Reads JSON from file path, or stdin when path is "-" / empty.
json read_json_input(const std::string& path);

// Standard CLI wrapper: parses --input <path> (default stdin), runs fn,
// prints the returned JSON, maps exceptions to exit codes.
int run_tool(int argc, char** argv, const char* name,
             int (*fn)(const json& in, json& out));

}  // namespace tca
