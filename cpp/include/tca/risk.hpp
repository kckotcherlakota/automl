// Deterministic capital-protection rules: position sizing, exposure limits,
// loss limits, cooldowns and the NORMAL/CAUTION/RECOVERY/LOCKED state machine.
#pragma once

#include <map>
#include <string>
#include <vector>

#include "tca/common.hpp"

namespace tca {

struct RiskConfig {
  double risk_per_trade = 0.0025;        // fraction of equity (NORMAL)
  double caution_risk_per_trade = 0.0010;
  double max_trade_risk = 0.0050;
  double max_position_notional = 0.10;
  double max_stock_exposure = 0.30;
  double max_crypto_exposure = 0.15;
  double max_total_exposure = 0.40;
  double min_uncommitted = 0.60;
  int max_open_positions = 3;
  int max_trades_per_day = 6;
  double daily_loss_limit = 0.01;
  double weekly_loss_limit = 0.03;
  double monthly_loss_limit = 0.05;
  double caution_drawdown = 0.02;
  double recovery_drawdown = 0.04;
  double lock_drawdown = 0.08;
  int pause_after_losses = 3;
  int loss_cooldown_sec = 1800;
  double correlated_risk_scale = 0.5;    // multiply risk per correlated open position
  std::vector<std::string> allowed_symbols;
  std::map<std::string, std::vector<std::string>> correlation_groups;
};

RiskConfig parse_risk_config(const json& j);

enum class RiskMode { Normal = 0, Caution = 1, Recovery = 2, Locked = 3 };
std::string to_string(RiskMode m);
RiskMode parse_risk_mode(const std::string& s);

struct Position {
  std::string symbol; AssetClass ac = AssetClass::Stock;
  double qty = 0, entry = 0, stop = 0, mark = 0;
};

struct AccountState {
  double equity = 0, buying_power = 0;
  double start_day_equity = 0, start_week_equity = 0, start_month_equity = 0, high_water_mark = 0;
  int consecutive_losses = 0, trades_today = 0, unknown_orders = 0;
  int64_t last_loss_time = 0, now = 0;
  bool kill_switch = false, critical_failure = false;
  RiskMode persisted_mode = RiskMode::Normal;
  std::string operating_mode = "RESEARCH";  // RESEARCH | PAPER | LIVE
  std::vector<Position> positions;
  std::vector<Position> pending_orders;  // pending opening orders count as exposure
};

AccountState parse_account(const json& j);

struct ModeEval {
  RiskMode mode = RiskMode::Normal;
  double drawdown = 0, daily_loss = 0, weekly_loss = 0, monthly_loss = 0;
  std::vector<std::string> reasons;
  json to_json() const;
};

// Mode never de-escalates below the persisted mode (no time-based unlock).
ModeEval evaluate_mode(const RiskConfig& cfg, const AccountState& a);

struct Proposal {
  std::string symbol, strategy, side = "buy";
  AssetClass ac = AssetClass::Stock;
  double entry = 0, stop = 0, slippage_per_unit = 0, fee_per_unit = 0;
  double qty_increment = 1, min_qty = 0;
  bool research_only = false;
};

Proposal parse_proposal(const json& j);

struct RiskDecision {
  bool approved = false;
  RiskMode mode = RiskMode::Normal;
  double quantity = 0, risk_dollars = 0, notional = 0, risk_budget = 0, risk_per_unit = 0;
  std::vector<std::string> reasons;  // rejection reasons (empty when approved)
  json limits;
  json to_json() const;
};

RiskDecision check_and_size(const RiskConfig& cfg, const AccountState& a, const Proposal& p);

// Rounds DOWN to an increment; never up.
double floor_to_increment(double qty, double inc);

}  // namespace tca
