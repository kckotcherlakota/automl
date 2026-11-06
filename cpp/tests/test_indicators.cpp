#include <gtest/gtest.h>
#include "tca/indicators.hpp"
#include "test_util.hpp"
using namespace tca;

TEST(Indicators, EmaSeedsWithSma) {
  Ema e(3);
  EXPECT_TRUE(std::isnan(e.update(1)));
  EXPECT_TRUE(std::isnan(e.update(2)));
  EXPECT_DOUBLE_EQ(e.update(3), 2.0);
  EXPECT_DOUBLE_EQ(e.update(4), 3.0);  // 4*0.5 + 2*0.5
}

TEST(Indicators, RsiAllGainsIs100AndFlatIs50) {
  Rsi up(3), flat(3);
  double r = kNaN, f = kNaN;
  for (int i = 0; i < 10; ++i) { r = up.update(100 + i); f = flat.update(100); }
  EXPECT_DOUBLE_EQ(r, 100.0);
  EXPECT_DOUBLE_EQ(f, 50.0);
}

TEST(Indicators, AtrConstantRange) {
  Atr a(3); double v = kNaN;
  for (int i = 0; i < 10; ++i) v = a.update({i, 10, 11, 9, 10, 1});
  EXPECT_NEAR(v, 2.0, 1e-12);
}

TEST(Indicators, VwapResetsEachSession) {
  std::vector<Bar> b{{tt::kNov4 + tt::kOpenUtc, 10, 10, 10, 10, 100},
                     {tt::kNov4 + tt::kOpenUtc + 300, 20, 20, 20, 20, 100},
                     {tt::kNov6 + tt::kOpenUtc, 30, 30, 30, 30, 5}};
  auto v = vwap_series(b, AssetClass::Stock);
  EXPECT_DOUBLE_EQ(v[1], 15.0);
  EXPECT_DOUBLE_EQ(v[2], 30.0);
}

TEST(Indicators, PriorHighExcludesCurrentBar) {
  std::vector<Bar> b;
  for (int i = 0; i < 5; ++i) b.push_back({i, 1, 1.0 + i, 1, 1, 1});
  auto ph = prior_high_series(b, 3);
  EXPECT_TRUE(std::isnan(ph[2]));
  EXPECT_DOUBLE_EQ(ph[3], 3.0);  // max of highs 1,2,3 (bars 0..2), not 4
  EXPECT_DOUBLE_EQ(ph[4], 4.0);
}

TEST(Indicators, RelVolUsesPriorBarsOnly) {
  std::vector<Bar> b;
  for (int i = 0; i < 4; ++i) b.push_back({i, 1, 1, 1, 1, 100});
  b.push_back({4, 1, 1, 1, 1, 300});
  auto rv = relvol_series(b, 4);
  EXPECT_DOUBLE_EQ(rv[4], 3.0);
}

TEST(Indicators, AggregateDropsIncompleteBucket) {
  std::vector<Bar> b;
  for (int i = 0; i < 7; ++i) b.push_back({3600 * 10 + i * 900, 1, 2, 0.5, 1.5, 1});
  auto h = aggregate(b, 900, 3600);
  ASSERT_EQ(h.size(), 1u);  // second hour has only 3 of 4 bars
  EXPECT_DOUBLE_EQ(h[0].v, 4);
}

TEST(Common, EasternTimeHandlesDst) {
  EXPECT_EQ(et_offset_seconds(tt::kNov6 + 19 * 3600), -5 * 3600);   // Nov 6 2026: EST
  EXPECT_EQ(et_offset_seconds(1782950400), -4 * 3600);              // Jul 2 2026: EDT
  EXPECT_EQ(et_minute_of_day(tt::kNov6 + tt::kOpenUtc), 570);
  EXPECT_EQ(et_date(tt::kNov6 + tt::kOpenUtc), 20261106);
  EXPECT_EQ(et_weekday(tt::kNov6 + tt::kOpenUtc), 5);
}

TEST(Common, ParseBarsRejectsInvalidData) {
  json bad_hl = {{"bars", {{{"t", 1}, {"o", 10}, {"h", 9}, {"l", 8}, {"c", 10}, {"v", 1}}}}};
  EXPECT_THROW(parse_bars(bad_hl), ValidationError);
  json bad_t = {{"bars", {{{"t", 2}, {"o", 1}, {"h", 1}, {"l", 1}, {"c", 1}, {"v", 1}},
                          {{"t", 2}, {"o", 1}, {"h", 1}, {"l", 1}, {"c", 1}, {"v", 1}}}}};
  EXPECT_THROW(parse_bars(bad_t), ValidationError);
  json neg = {{"bars", {{{"t", 1}, {"o", -1}, {"h", 1}, {"l", -2}, {"c", 1}, {"v", 1}}}}};
  EXPECT_THROW(parse_bars(neg), ValidationError);
}

TEST(Common, DropIncompleteBar) {
  std::vector<Bar> b{{0, 1, 1, 1, 1, 1}, {300, 1, 1, 1, 1, 1}};
  EXPECT_EQ(drop_incomplete(b, 300, 599), 1);
  EXPECT_EQ(b.size(), 1u);
}
