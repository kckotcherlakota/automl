// fastsim: multi-threaded 0DTE strategy simulator (SPY/QQQ, 5-minute bars).
//
// Usage: fastsim <bars.csv> <configs.txt> <trades_out.csv> [threads]
//
// bars.csv  (written by fast.py): sym,day,bar_min,open,high,low,close,volume,
//           vwap,ema9,ema21,vol20,iv_front,iv_back   (sorted by sym,day,bar_min)
// configs   : one config per line, "key=value" tokens separated by spaces.
// output    : one row per trade, tagged with the config id.
//
// Option prices are Black-Scholes (r=0) with the day's IV held constant, plus
// a bid/ask model -- identical to options_model.py / engine.py / condor.py,
// which serve as the slow reference implementation.
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <map>
#include <mutex>
#include <random>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

static const double MPY = 390.0 * 252.0;  // trading minutes per year

struct Bar {
  int bm;  // bar start, minutes after midnight ET
  double o, h, l, c, v, vwap, e9, e21, v20;
};
struct Day {
  int sym, day;
  double ivf, ivb;
  std::vector<Bar> bars;
};

// ---------------------------------------------------------------- pricing
static inline double ncdf(double x) { return 0.5 * std::erfc(-x * M_SQRT1_2); }
static inline double tyears(double mins) { return std::max(mins, 1.0) / MPY; }
static inline double bs(double S, double K, double T, double sig, bool call) {
  double v = sig * std::sqrt(T);
  double d1 = (std::log(S / K) + 0.5 * v * v) / v, d2 = d1 - v;
  return call ? S * ncdf(d1) - K * ncdf(d2) : K * ncdf(-d2) - S * ncdf(-d1);
}
static inline double bsdelta(double S, double K, double T, double sig, bool call) {
  double v = sig * std::sqrt(T);
  double d1 = (std::log(S / K) + 0.5 * v * v) / v;
  return call ? ncdf(d1) : ncdf(d1) - 1.0;
}
static inline double hs(double mid, int sym) {
  return sym == 0 ? std::max(0.01, 0.0075 * mid) : std::max(0.015, 0.0125 * mid);
}
static inline double bidp(double m, int sym) { return std::max(0.0, m - hs(m, sym)); }
static inline double askp(double m, int sym) { return m + hs(m, sym); }
static double pick_strike(double S, double T, double sig, bool call, double tdelta) {
  double base = std::round(S), best = base, err = 9;
  for (int k = -40; k <= 40; ++k) {
    double K = base + k;
    if (K <= 0) continue;
    double e = std::fabs(std::fabs(bsdelta(S, K, T, sig, call)) - tdelta);
    if (e < err) { err = e; best = K; }
  }
  return best;
}

// ---------------------------------------------------------------- config
struct Cfg {
  std::map<std::string, std::string> kv;
  double d(const char* k, double def) const {
    auto it = kv.find(k);
    return it == kv.end() ? def : std::atof(it->second.c_str());
  }
  std::string s(const char* k, const char* def) const {
    auto it = kv.find(k);
    return it == kv.end() ? def : it->second;
  }
};

struct Trade {
  int id, sym, day, entry_min, exit_min;
  std::string kind, side, reason;
  double entry_px, exit_px, capital, pnl;
  int n;
};

// ---------------------------------------------------------------- spreads
struct Leg { double K; bool call; int q; };  // q=-1 short, +1 long

static double close_cost(const std::vector<Leg>& legs, double S, double T, double iv, int sym) {
  double c = 0;
  for (auto& L : legs) {
    double m = bs(S, L.K, T, iv, L.call);
    double h = m > 0.005 ? hs(m, sym) : 0.0;
    c += L.q < 0 ? (m + h) : -std::max(0.0, m - h);
  }
  return c;
}
static double intrinsic_cost(const std::vector<Leg>& legs, double S) {
  double v = 0;
  for (auto& L : legs) v += -L.q * (L.call ? std::max(0.0, S - L.K) : std::max(0.0, L.K - S));
  return v;
}
static void add_vertical(std::vector<Leg>& legs, double S, double T, double iv, bool call,
                         double sdelta, double ldelta, double wing) {
  double ks = pick_strike(S, T, iv, call, sdelta);
  double kl;
  if (wing > 0) kl = call ? ks + wing : ks - wing;
  else {
    kl = pick_strike(S, T, iv, call, ldelta);
    kl = call ? std::max(kl, ks + 1) : std::min(kl, ks - 1);
  }
  legs.push_back({ks, call, -1});
  legs.push_back({kl, call, +1});
}

// open a credit structure at the close of bar i and manage it
static bool run_spread(const Day& D, int i, const std::vector<Leg>& legs, const Cfg& c,
                       double equity, Trade& t, int orh_side = 0, double orh = 0, double orl = 0) {
  const auto& B = D.bars;
  int sym = D.sym;
  double iv = D.ivf, fee = c.d("fee", 0.65);
  double S0 = B[i].c, T0 = tyears(960 - (B[i].bm + 5));
  double credit = 0;
  for (auto& L : legs) {
    double m = bs(S0, L.K, T0, iv, L.call);
    credit += L.q < 0 ? bidp(m, sym) : -askp(m, sym);
  }
  if (credit <= 0.02) return false;
  double maxw = 0;
  for (size_t j = 0; j + 1 < legs.size(); j += 2) maxw = std::max(maxw, std::fabs(legs[j].K - legs[j + 1].K));
  double max_loss = maxw - credit;
  if (max_loss <= 0) return false;
  int nl = (int)legs.size();
  int fixed = (int)c.d("fixed", 0);
  int n = fixed > 0 ? fixed : (int)std::floor(equity * c.d("risk", 0.01) / (max_loss * 100 + 2 * nl * fee));
  if (n < 1) return false;
  double smult = c.d("smult", 2.0), tp = c.d("tp", -1), exitm = c.d("exit", -1);
  bool tech = c.d("tech", 0) > 0;
  double stop_cost = smult >= 0 ? credit * (1 + smult) : 1e18;
  double tp_cost = tp > 0 ? credit * (1 - tp) : -1e18;
  double exit_cost = 0;
  std::string reason;
  int ex = (int)B.size() - 1;
  for (int j = i + 1; j < (int)B.size(); ++j) {
    const Bar& b = B[j];
    double T = tyears(960 - (b.bm + 5)), To = tyears(960 - b.bm);
    double oc = close_cost(legs, b.o, To, iv, sym);
    double ch = close_cost(legs, b.h, T, iv, sym), cl = close_cost(legs, b.l, T, iv, sym);
    double cc = close_cost(legs, b.c, T, iv, sym);
    double worst = std::max(ch, cl), best = std::min(std::min(ch, cl), cc);
    if (oc >= stop_cost) { exit_cost = oc; reason = "stop_gap"; }
    else if (worst >= stop_cost) { exit_cost = stop_cost; reason = "stop"; }
    else if (best <= tp_cost) { exit_cost = tp_cost; reason = "take_profit"; }
    else if (tech && orh_side != 0 && ((orh_side > 0 && b.c < orh) || (orh_side < 0 && b.c > orl))) {
      exit_cost = cc; reason = "invalidated";
    } else if (exitm > 0 && b.bm + 5 >= exitm) { exit_cost = cc; reason = "time"; }
    if (!reason.empty()) { ex = j; break; }
  }
  if (reason.empty()) { exit_cost = intrinsic_cost(legs, B.back().c); reason = "expiry"; }
  double fees = (reason == "expiry" ? 1 : 2) * nl * fee;
  double pnl1 = (credit - exit_cost) * 100 - fees;
  t.sym = sym; t.day = D.day; t.entry_min = B[i].bm + 5; t.exit_min = reason == "expiry" ? 960 : B[ex].bm + 5;
  t.entry_px = credit; t.exit_px = exit_cost; t.capital = max_loss * 100; t.n = n;
  t.pnl = pnl1 * n; t.reason = reason;
  std::string sd;
  for (auto& L : legs) {
    char buf[32];
    std::snprintf(buf, sizeof buf, "%s%g%c", L.q < 0 ? "-" : "+", L.K, L.call ? 'C' : 'P');
    sd += buf;
  }
  t.side = sd;
  return true;
}

// ---------------------------------------------------------------- calendar
// short front (0DTE) + long back (next expiry, +1 session) at the same strike
static bool run_calendar(const Day& D, int i, const Cfg& c, double equity, Trade& t) {
  const auto& B = D.bars;
  int sym = D.sym;
  bool call = c.s("right", "C") == "C";
  double fee = c.d("fee", 0.65);
  double S0 = B[i].c, K = std::round(S0 + c.d("koff", 0) * (call ? 1 : -1));
  auto val = [&](double S, double minsf) {  // value received to close: sell back, buy front
    double Tf = tyears(minsf), Tb = tyears(minsf + 390);
    double mf = bs(S, K, Tf, D.ivf, call), mb = bs(S, K, Tb, D.ivb, call);
    return bidp(mb, sym) - askp(mf, sym);
  };
  double mins0 = 960 - (B[i].bm + 5);
  double mf0 = bs(S0, K, tyears(mins0), D.ivf, call), mb0 = bs(S0, K, tyears(mins0 + 390), D.ivb, call);
  double debit = askp(mb0, sym) - bidp(mf0, sym);
  if (debit <= 0.02) return false;
  int fixed = (int)c.d("fixed", 0);
  int n = fixed > 0 ? fixed : (int)std::floor(equity * c.d("risk", 0.01) / (debit * 100 + 4 * fee));
  if (n < 1) return false;
  double stop = c.d("stop", -1), tp = c.d("tp", -1), exitm = c.d("exit", 950);
  double stop_v = stop > 0 ? debit * (1 - stop) : -1e18, tp_v = tp > 0 ? debit * (1 + tp) : 1e18;
  double xv = 0; std::string reason; int ex = (int)B.size() - 1;
  for (int j = i + 1; j < (int)B.size(); ++j) {
    const Bar& b = B[j];
    double m = 960 - (b.bm + 5);
    double vo = val(b.o, m + 5), vh = val(b.h, m), vl = val(b.l, m), vc = val(b.c, m);
    double worst = std::min(vh, vl), best = std::max(std::max(vh, vl), vc);
    if (b.l <= K && K <= b.h) best = std::max(best, val(K, m));
    if (vo <= stop_v) { xv = vo; reason = "stop_gap"; }
    else if (worst <= stop_v) { xv = stop_v; reason = "stop"; }
    else if (vo >= tp_v) { xv = vo; reason = "take_profit"; }
    else if (best >= tp_v) { xv = tp_v; reason = "take_profit"; }
    else if (b.bm + 5 >= exitm) { xv = vc; reason = "time"; }
    if (!reason.empty()) { ex = j; break; }
  }
  if (reason.empty()) { xv = val(B.back().c, 1); reason = "eod"; }
  double pnl1 = (xv - debit) * 100 - 4 * fee;
  t.sym = sym; t.day = D.day; t.entry_min = B[i].bm + 5; t.exit_min = B[ex].bm + 5;
  t.entry_px = debit; t.exit_px = xv; t.capital = debit * 100; t.n = n; t.pnl = pnl1 * n;
  t.reason = reason;
  char buf[48];
  std::snprintf(buf, sizeof buf, "-%g%c0DTE/+%g%c1DTE", K, call ? 'C' : 'P', K, call ? 'C' : 'P');
  t.side = buf;
  return true;
}

// ---------------------------------------------------------------- ORB
struct Sig { int i, side; double orh, orl; };  // side +1 long/-1 short

static std::vector<Sig> orb_signals(const Day& D, const Cfg& c, std::mt19937& rng) {
  std::vector<Sig> out;
  const auto& B = D.bars;
  double orh = -1e18, orl = 1e18;
  int nor = 0;
  for (auto& b : B) if (b.bm < 585) { orh = std::max(orh, b.h); orl = std::min(orl, b.l); ++nor; }
  if (nor < 3) return out;
  int wstart = (int)c.d("wstart", 585), wend = (int)c.d("wend", 690), rmax = (int)c.d("rmax", 6);
  bool retest = c.d("retest", 1) > 0, rvol = c.d("vol", 1) > 0, rvwap = c.d("vwap", 1) > 0,
       rema = c.d("ema", 1) > 0;
  double tol = c.d("tol", 0.0005);
  int armed[2] = {-1, -1};
  for (int i = 0; i < (int)B.size(); ++i) {
    const Bar& r = B[i];
    if (r.bm < wstart) continue;
    int bclose = r.bm + 5;
    bool vol_ok = !rvol || (r.v20 > 0 && r.v > r.v20);
    for (int s = 0; s < 2; ++s) {
      bool lng = s == 0;
      bool beyond = lng ? r.c > orh : r.c < orl;
      bool trend = true;
      if (rvwap) trend = trend && (lng ? r.c > r.vwap : r.c < r.vwap);
      if (rema) trend = trend && (lng ? r.e9 > r.e21 : r.e9 < r.e21);
      int& a = armed[s];
      if (a < 0) {
        if (beyond && vol_ok) {
          if (!retest) { if (trend && bclose <= wend) out.push_back({i, lng ? 1 : -1, orh, orl}); }
          else a = i;
        }
        continue;
      }
      if (i - a > rmax || !beyond) { a = (beyond && vol_ok) ? i : -1; continue; }
      bool touched = lng ? r.l <= orh * (1 + tol) : r.h >= orl * (1 - tol);
      if (touched && beyond && trend && bclose <= wend) { out.push_back({i, lng ? 1 : -1, orh, orl}); a = -1; }
    }
  }
  if (c.d("rnd", 0) > 0) {
    std::uniform_real_distribution<double> U(0, 1);
    for (auto& s : out) s.side = U(rng) < 0.5 ? 1 : -1;
  }
  return out;
}

static bool run_long_option(const Day& D, const Sig& g, const Cfg& c, double equity, Trade& t, int& exit_idx) {
  const auto& B = D.bars;
  int sym = D.sym, i = g.i;
  bool call = g.side > 0;
  double iv = D.ivf, fee = c.d("fee", 0.65);
  double S0 = B[i].c, T0 = tyears(960 - (B[i].bm + 5));
  double K = pick_strike(S0, T0, iv, call, c.d("delta", 0.575));
  double mid0 = bs(S0, K, T0, iv, call);
  double maxp = c.d("maxprem", 0);
  if (maxp > 0)
    while (askp(mid0, sym) > maxp && mid0 > 0.05) { K += call ? 1 : -1; mid0 = bs(S0, K, T0, iv, call); }
  if (mid0 < 0.05) return false;
  double entry = askp(mid0, sym);
  double stop = c.d("stop", 0.2), tgt = c.d("target", 0.4);
  int fixed = (int)c.d("fixed", 0);
  int n = fixed > 0 ? fixed : (int)std::floor(equity * c.d("risk", 0.005) / (entry * stop * 100 + 2 * fee));
  if (n < 1) return false;
  double stop_px = entry * (1 - stop), tgt_px = entry * (1 + tgt);
  bool tech = c.d("tech", 1) > 0;
  int fexit = (int)c.d("fexit", 900);
  double xp = 0; std::string reason; int ex = (int)B.size() - 1;
  for (int j = i + 1; j < (int)B.size(); ++j) {
    const Bar& b = B[j];
    double T = tyears(960 - (b.bm + 5)), To = tyears(960 - b.bm);
    double ob = bidp(bs(b.o, K, To, iv, call), sym);
    double wS = call ? b.l : b.h, bS = call ? b.h : b.l;
    double wb = bidp(bs(wS, K, T, iv, call), sym), bb = bidp(bs(bS, K, T, iv, call), sym);
    if (ob <= stop_px) { xp = ob; reason = "stop_gap"; }
    else if (wb <= stop_px) { xp = stop_px; reason = "stop"; }
    else if (ob >= tgt_px) { xp = ob; reason = "target_gap"; }
    else if (bb >= tgt_px) { xp = tgt_px; reason = "target"; }
    else {
      bool inv = tech && (call ? (b.c < g.orh || b.c < b.vwap) : (b.c > g.orl || b.c > b.vwap));
      if (inv) { xp = bidp(bs(b.c, K, T, iv, call), sym); reason = "invalidated"; }
      else if (b.bm + 5 >= fexit) { xp = bidp(bs(b.c, K, T, iv, call), sym); reason = "time"; }
    }
    if (!reason.empty()) { ex = j; break; }
  }
  if (reason.empty()) { xp = bidp(bs(B.back().c, K, tyears(1), iv, call), sym); reason = "eod"; }
  t.sym = sym; t.day = D.day; t.entry_min = B[i].bm + 5; t.exit_min = B[ex].bm + 5;
  t.entry_px = entry; t.exit_px = xp; t.capital = entry * 100 + fee; t.n = n;
  t.pnl = ((xp - entry) * 100 - 2 * fee) * n; t.reason = reason;
  char buf[24]; std::snprintf(buf, sizeof buf, "+%g%c", K, call ? 'C' : 'P'); t.side = buf;
  exit_idx = ex;
  return true;
}

// ---------------------------------------------------------------- driver
struct Data {
  std::vector<Day> days;              // all symbol-days
  std::map<int, std::vector<int>> by_day;  // day -> indices into days
};

static Data load(const char* path) {
  Data D;
  std::ifstream f(path);
  std::string line;
  std::getline(f, line);
  int cs = -1, cd = -1;
  while (std::getline(f, line)) {
    double x[14];
    const char* p = line.c_str();
    for (int k = 0; k < 14; ++k) { char* e; x[k] = std::strtod(p, &e); p = e + 1; }
    int sym = (int)x[0], day = (int)x[1];
    if (sym != cs || day != cd) {
      D.days.push_back(Day{sym, day, x[12], x[13], {}});
      cs = sym; cd = day;
    }
    D.days.back().bars.push_back({(int)x[2], x[3], x[4], x[5], x[6], x[7], x[8], x[9], x[10], x[11]});
  }
  for (int k = 0; k < (int)D.days.size(); ++k) D.by_day[D.days[k].day].push_back(k);
  return D;
}

static int entry_index(const Day& D, int entry_min) {
  for (int i = 0; i < (int)D.bars.size(); ++i) if (D.bars[i].bm + 5 == entry_min) return i;
  return -1;
}

static std::vector<Trade> run_config(const Data& data, const Cfg& c, int id) {
  std::vector<Trade> out;
  std::string kind = c.s("kind", "orb");
  double equity = c.d("equity", 100000);
  int symmask = (int)c.d("syms", 3);
  std::mt19937 rng((unsigned)c.d("seed", 7));
  for (auto& kv : data.by_day) {
    std::vector<const Day*> ds;
    for (int k : kv.second) if (symmask & (1 << data.days[k].sym)) ds.push_back(&data.days[k]);
    if (kind == "orb" || kind == "orb_spread") {
      struct Cand { int bm, sym; const Day* D; Sig g; };
      std::vector<Cand> cands;
      for (auto* D : ds) for (auto& g : orb_signals(*D, c, rng)) cands.push_back({D->bars[g.i].bm, D->sym, D, g});
      std::sort(cands.begin(), cands.end(), [](const Cand& a, const Cand& b) {
        return a.bm != b.bm ? a.bm < b.bm : a.sym < b.sym; });
      double day_pnl = 0; int ntr = 0, maxtr = (int)c.d("maxtr", 2);
      double dll = c.d("dll", 0.01);
      int busy[2] = {-1, -1};
      for (auto& cd : cands) {
        if (ntr >= maxtr) break;
        if (day_pnl <= -dll * equity) break;
        if (busy[cd.sym] >= cd.g.i) continue;
        Trade t; t.id = id; t.kind = kind;
        int ex = -1;
        bool ok;
        if (kind == "orb") ok = run_long_option(*cd.D, cd.g, c, equity, t, ex);
        else {
          std::vector<Leg> legs;
          const Bar& b = cd.D->bars[cd.g.i];
          double T = tyears(960 - (b.bm + 5));
          add_vertical(legs, b.c, T, cd.D->ivf, cd.g.side < 0, c.d("sdelta", 0.3), c.d("ldelta", 0.1), c.d("wing", 0));
          ok = run_spread(*cd.D, cd.g.i, legs, c, equity, t, cd.g.side, cd.g.orh, cd.g.orl);
          if (ok) {
            ex = cd.g.i;
            for (int j = cd.g.i; j < (int)cd.D->bars.size(); ++j) if (cd.D->bars[j].bm + 5 <= t.exit_min) ex = j;
          }
        }
        if (!ok) continue;
        busy[cd.sym] = ex;
        day_pnl += t.pnl; ++ntr;
        out.push_back(t);
      }
      continue;
    }
    int em = (int)c.d("entry", 585);
    for (auto* D : ds) {
      int i = entry_index(*D, em);
      if (i < 0) continue;
      Trade t; t.id = id; t.kind = kind;
      bool ok = false;
      if (kind == "calendar") ok = run_calendar(*D, i, c, equity, t);
      else {
        const Bar& b = D->bars[i];
        double T = tyears(960 - (b.bm + 5));
        std::vector<Leg> legs;
        double sd = c.d("sdelta", 0.2), ld = c.d("ldelta", 0.05), w = c.d("wing", 0);
        if (kind == "condor" || kind == "call_spread") add_vertical(legs, b.c, T, D->ivf, true, sd, ld, w);
        if (kind == "condor" || kind == "put_spread") add_vertical(legs, b.c, T, D->ivf, false, sd, ld, w);
        if (kind == "trend_spread") {
          bool bull = b.c > b.vwap && b.e9 > b.e21, bear = b.c < b.vwap && b.e9 < b.e21;
          if (bull) add_vertical(legs, b.c, T, D->ivf, false, sd, ld, w);
          else if (bear) add_vertical(legs, b.c, T, D->ivf, true, sd, ld, w);
        }
        if (!legs.empty()) ok = run_spread(*D, i, legs, c, equity, t);
      }
      if (ok) out.push_back(t);
    }
  }
  return out;
}

int main(int argc, char** argv) {
  if (argc < 4) { std::fprintf(stderr, "usage: fastsim bars.csv configs.txt out.csv [threads]\n"); return 1; }
  Data data = load(argv[1]);
  std::vector<Cfg> cfgs;
  std::ifstream cf(argv[2]);
  std::string line;
  while (std::getline(cf, line)) {
    if (line.empty() || line[0] == '#') continue;
    Cfg c; std::istringstream ss(line); std::string tok;
    while (ss >> tok) { auto p = tok.find('='); if (p != std::string::npos) c.kv[tok.substr(0, p)] = tok.substr(p + 1); }
    cfgs.push_back(c);
  }
  int nth = argc > 4 ? std::atoi(argv[4]) : (int)std::thread::hardware_concurrency();
  std::vector<std::vector<Trade>> res(cfgs.size());
  std::atomic<int> next{0};
  std::vector<std::thread> th;
  for (int k = 0; k < nth; ++k)
    th.emplace_back([&] {
      for (int i; (i = next++) < (int)cfgs.size();) res[i] = run_config(data, cfgs[i], (int)cfgs[i].d("id", i));
    });
  for (auto& t : th) t.join();
  FILE* o = std::fopen(argv[3], "w");
  std::fprintf(o, "id,kind,sym,day,entry_min,exit_min,legs,entry_px,exit_px,contracts,capital,pnl,reason\n");
  size_t nt = 0;
  for (auto& v : res)
    for (auto& t : v) {
      std::fprintf(o, "%d,%s,%d,%d,%d,%d,%s,%.4f,%.4f,%d,%.2f,%.2f,%s\n", t.id, t.kind.c_str(), t.sym, t.day,
                   t.entry_min, t.exit_min, t.side.c_str(), t.entry_px, t.exit_px, t.n, t.capital, t.pnl,
                   t.reason.c_str());
      ++nt;
    }
  std::fclose(o);
  std::fprintf(stderr, "configs=%zu symbol-days=%zu trades=%zu\n", cfgs.size(), data.days.size(), nt);
  return 0;
}
