#include "tca/indicators.hpp"

#include <algorithm>

namespace tca {

Ema::Ema(int period) : period_(period), k_(2.0 / (period + 1.0)) {
  if (period < 1) throw ValidationError("EMA period must be >= 1");
}
double Ema::update(double x) {
  ++n_;
  if (n_ < period_) { sum_ += x; return kNaN; }
  if (n_ == period_) { sum_ += x; v_ = sum_ / period_; return v_; }
  v_ = x * k_ + v_ * (1 - k_);
  return v_;
}

Sma::Sma(int period) : period_(period) {
  if (period < 1) throw ValidationError("SMA period must be >= 1");
}
double Sma::update(double x) {
  q_.push_back(x); sum_ += x;
  if (static_cast<int>(q_.size()) > period_) { sum_ -= q_.front(); q_.pop_front(); }
  return static_cast<int>(q_.size()) == period_ ? sum_ / period_ : kNaN;
}

Rsi::Rsi(int period) : period_(period) {
  if (period < 1) throw ValidationError("RSI period must be >= 1");
}
double Rsi::update(double c) {
  if (!have_prev_) { prev_ = c; have_prev_ = true; return kNaN; }
  double ch = c - prev_; prev_ = c;
  double g = ch > 0 ? ch : 0, l = ch < 0 ? -ch : 0;
  ++n_;
  if (n_ <= period_) {
    ag_ += g; al_ += l;
    if (n_ < period_) return kNaN;
    ag_ /= period_; al_ /= period_;
  } else {
    ag_ = (ag_ * (period_ - 1) + g) / period_;
    al_ = (al_ * (period_ - 1) + l) / period_;
  }
  if (al_ == 0) return ag_ == 0 ? 50.0 : 100.0;
  return 100.0 - 100.0 / (1.0 + ag_ / al_);
}

Atr::Atr(int period) : period_(period) {
  if (period < 1) throw ValidationError("ATR period must be >= 1");
}
double Atr::update(const Bar& b) {
  double tr = have_prev_ ? std::max({b.h - b.l, std::fabs(b.h - prev_close_), std::fabs(b.l - prev_close_)}) : b.h - b.l;
  have_prev_ = true; prev_close_ = b.c; ++n_;
  if (n_ < period_) { sum_ += tr; return kNaN; }
  if (n_ == period_) { sum_ += tr; atr_ = sum_ / period_; return atr_; }
  atr_ = (atr_ * (period_ - 1) + tr) / period_;
  return atr_;
}

Adx::Adx(int period) : period_(period) {
  if (period < 1) throw ValidationError("ADX period must be >= 1");
}
double Adx::update(const Bar& b) {
  if (!have_prev_) { prev_ = b; have_prev_ = true; return kNaN; }
  double up = b.h - prev_.h, dn = prev_.l - b.l;
  double pdm = (up > dn && up > 0) ? up : 0, mdm = (dn > up && dn > 0) ? dn : 0;
  double tr = std::max({b.h - b.l, std::fabs(b.h - prev_.c), std::fabs(b.l - prev_.c)});
  prev_ = b; ++n_;
  if (n_ <= period_) {
    tr_s_ += tr; pdm_s_ += pdm; mdm_s_ += mdm;
    if (n_ < period_) return kNaN;
  } else {
    tr_s_ = tr_s_ - tr_s_ / period_ + tr;
    pdm_s_ = pdm_s_ - pdm_s_ / period_ + pdm;
    mdm_s_ = mdm_s_ - mdm_s_ / period_ + mdm;
  }
  pdi_ = tr_s_ > 0 ? 100 * pdm_s_ / tr_s_ : 0;
  mdi_ = tr_s_ > 0 ? 100 * mdm_s_ / tr_s_ : 0;
  double dx = (pdi_ + mdi_) > 0 ? 100 * std::fabs(pdi_ - mdi_) / (pdi_ + mdi_) : 0;
  if (dx_n_ < period_) {
    dx_sum_ += dx; ++dx_n_;
    if (dx_n_ < period_) return kNaN;
    adx_ = dx_sum_ / period_;
    return adx_;
  }
  adx_ = (adx_ * (period_ - 1) + dx) / period_;
  return adx_;
}

double SessionVwap::update(const Bar& b, int session_key) {
  if (session_key != key_) { key_ = session_key; pv_ = 0; vol_ = 0; last_ = kNaN; }
  double tp = (b.h + b.l + b.c) / 3.0;
  pv_ += tp * b.v; vol_ += b.v;
  last_ = vol_ > 0 ? pv_ / vol_ : (std::isnan(last_) ? tp : last_);
  return last_;
}

double RollingExtreme::prior_then_push(double x) {
  while (!dq_.empty() && dq_.front().first <= idx_ - period_) dq_.pop_front();
  double res = (idx_ >= period_ && !dq_.empty()) ? dq_.front().second : kNaN;
  while (!dq_.empty() && (is_max_ ? dq_.back().second <= x : dq_.back().second >= x)) dq_.pop_back();
  dq_.emplace_back(idx_, x);
  ++idx_;
  return res;
}

std::vector<double> ema_series(const std::vector<Bar>& bars, int period) {
  Ema e(period); std::vector<double> out; out.reserve(bars.size());
  for (const auto& b : bars) out.push_back(e.update(b.c));
  return out;
}
std::vector<double> sma_volume_series(const std::vector<Bar>& bars, int period) {
  Sma s(period); std::vector<double> out; out.reserve(bars.size());
  for (const auto& b : bars) out.push_back(s.update(b.v));
  return out;
}
std::vector<double> rsi_series(const std::vector<Bar>& bars, int period) {
  Rsi r(period); std::vector<double> out; out.reserve(bars.size());
  for (const auto& b : bars) out.push_back(r.update(b.c));
  return out;
}
std::vector<double> atr_series(const std::vector<Bar>& bars, int period) {
  Atr a(period); std::vector<double> out; out.reserve(bars.size());
  for (const auto& b : bars) out.push_back(a.update(b));
  return out;
}
std::vector<double> adx_series(const std::vector<Bar>& bars, int period) {
  Adx a(period); std::vector<double> out; out.reserve(bars.size());
  for (const auto& b : bars) out.push_back(a.update(b));
  return out;
}
std::vector<double> vwap_series(const std::vector<Bar>& bars, AssetClass ac) {
  SessionVwap v; std::vector<double> out; out.reserve(bars.size());
  for (const auto& b : bars) {
    int key = ac == AssetClass::Stock ? et_date(b.t) : static_cast<int>(b.t / 86400);
    out.push_back(v.update(b, key));
  }
  return out;
}
std::vector<double> vwap_series(const std::vector<Bar>& bars, AssetClass ac, int64_t tf_sec) {
  if (tf_sec < 86400) return vwap_series(bars, ac);
  const size_t n = 20;
  std::vector<double> out(bars.size(), kNaN);
  double pv = 0, vol = 0;
  for (size_t i = 0; i < bars.size(); ++i) {
    const double tp = (bars[i].h + bars[i].l + bars[i].c) / 3.0;
    pv += tp * bars[i].v; vol += bars[i].v;
    if (i >= n) {
      const double tq = (bars[i - n].h + bars[i - n].l + bars[i - n].c) / 3.0;
      pv -= tq * bars[i - n].v; vol -= bars[i - n].v;
    }
    if (i + 1 >= n) out[i] = vol > 0 ? pv / vol : tp;
  }
  return out;
}
std::vector<double> prior_high_series(const std::vector<Bar>& bars, int period) {
  RollingExtreme r(period, true); std::vector<double> out; out.reserve(bars.size());
  for (const auto& b : bars) out.push_back(r.prior_then_push(b.h));
  return out;
}
std::vector<double> prior_low_series(const std::vector<Bar>& bars, int period) {
  RollingExtreme r(period, false); std::vector<double> out; out.reserve(bars.size());
  for (const auto& b : bars) out.push_back(r.prior_then_push(b.l));
  return out;
}
std::vector<double> relvol_series(const std::vector<Bar>& bars, int period) {
  std::vector<double> out(bars.size(), kNaN);
  double sum = 0;
  for (size_t i = 0; i < bars.size(); ++i) {
    if (static_cast<int>(i) >= period) {
      double avg = sum / period;
      out[i] = avg > 0 ? bars[i].v / avg : kNaN;
      sum -= bars[i - period].v;
    }
    sum += bars[i].v;
  }
  return out;
}
std::vector<double> bb_width_series(const std::vector<Bar>& bars, int period) {
  std::vector<double> out(bars.size(), kNaN);
  double s = 0, s2 = 0;
  for (size_t i = 0; i < bars.size(); ++i) {
    s += bars[i].c; s2 += bars[i].c * bars[i].c;
    if (static_cast<int>(i) >= period) { s -= bars[i - period].c; s2 -= bars[i - period].c * bars[i - period].c; }
    if (static_cast<int>(i) >= period - 1) {
      double m = s / period, var = std::max(0.0, s2 / period - m * m);
      out[i] = m > 0 ? 4.0 * std::sqrt(var) / m : kNaN;
    }
  }
  return out;
}

std::vector<Bar> aggregate(const std::vector<Bar>& bars, int64_t from_sec, int64_t to_sec) {
  if (to_sec % from_sec != 0) throw ValidationError("target timeframe must be a multiple of source");
  std::vector<Bar> out;
  if (bars.empty()) return out;
  const int64_t last_end = bars.back().t + from_sec;
  Bar cur{}; int64_t bucket = -1;
  auto flush = [&] { if (bucket >= 0 && bucket + to_sec <= last_end) out.push_back(cur); };
  for (const auto& b : bars) {
    int64_t k = b.t - ((b.t % to_sec) + to_sec) % to_sec;
    if (k != bucket) { flush(); bucket = k; cur = b; cur.t = k; continue; }
    cur.h = std::max(cur.h, b.h); cur.l = std::min(cur.l, b.l); cur.c = b.c; cur.v += b.v;
  }
  flush();
  return out;
}

}  // namespace tca
