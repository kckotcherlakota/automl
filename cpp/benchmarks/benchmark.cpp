// Google Benchmark for local computation latency (NOT exchange/broker latency).
#include <benchmark/benchmark.h>
#include <random>
#include "tca/backtest.hpp"
#include "tca/risk.hpp"
using namespace tca;

static std::vector<Bar> synth(size_t n) {
  std::mt19937 rng(7); std::normal_distribution<double> d(0, 0.002);
  std::vector<Bar> v; double px = 100; int64_t t = 1700000000;
  for (size_t i = 0; i < n; ++i) {
    double o = px, c = px * (1 + d(rng));
    double h = std::max(o, c) * (1 + std::abs(d(rng)) / 2), l = std::min(o, c) * (1 - std::abs(d(rng)) / 2);
    v.push_back({t, o, h, l, c, 1000 + static_cast<double>(rng() % 1000)}); px = c; t += 900;
  }
  return v;
}

static void BM_IncrementalEmaUpdate(benchmark::State& s) {
  Ema e(20); double x = 100;
  for (auto _ : s) benchmark::DoNotOptimize(e.update(x += 0.01));
}
BENCHMARK(BM_IncrementalEmaUpdate);

static void BM_BuildContext(benchmark::State& s) {
  auto bars = synth(static_cast<size_t>(s.range(0)));
  for (auto _ : s) benchmark::DoNotOptimize(build_context(bars, AssetClass::Crypto, 900));
  s.SetItemsProcessed(s.iterations() * s.range(0));
}
BENCHMARK(BM_BuildContext)->Arg(1000)->Arg(10000);

static void BM_EvaluateAllStrategies(benchmark::State& s) {
  auto c = build_context(synth(2000), AssetClass::Crypto, 900);
  StrategyParams p;
  for (auto _ : s)
    for (const auto& id : all_strategies())
      if (strategy_applies(id, c.ac)) benchmark::DoNotOptimize(evaluate(id, c, 1999, p));
}
BENCHMARK(BM_EvaluateAllStrategies);

static void BM_RiskCheck(benchmark::State& s) {
  RiskConfig cfg; AccountState a; a.equity = 10000; a.buying_power = 10000; a.now = 1; a.operating_mode = "PAPER";
  a.high_water_mark = a.start_day_equity = a.start_week_equity = a.start_month_equity = 10000;
  Proposal p; p.symbol = "BTC-USD"; p.ac = AssetClass::Crypto; p.entry = 75000; p.stop = 74000; p.qty_increment = 1e-8; p.min_qty = 1e-8;
  for (auto _ : s) benchmark::DoNotOptimize(check_and_size(cfg, a, p));
}
BENCHMARK(BM_RiskCheck);

static void BM_Backtest10k(benchmark::State& s) {
  auto c = build_context(synth(10000), AssetClass::Crypto, 900);
  BacktestConfig cfg;
  for (auto _ : s) benchmark::DoNotOptimize(run_backtest(c, cfg, 200, 10000));
}
BENCHMARK(BM_Backtest10k)->Unit(benchmark::kMillisecond);

BENCHMARK_MAIN();
