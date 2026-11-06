cmake_minimum_required(VERSION 3.20)
project(claude_trading_agent CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()
add_compile_options(-Wall -Wextra -Wpedantic)

find_package(nlohmann_json 3 REQUIRED)
find_package(SQLite3 REQUIRED)

add_library(tcacore STATIC
  cpp/src/core/common.cpp cpp/src/core/indicators.cpp cpp/src/core/signals.cpp
  cpp/src/core/risk.cpp cpp/src/core/execution.cpp cpp/src/core/backtest.cpp cpp/src/core/journal.cpp)
target_include_directories(tcacore PUBLIC cpp/include)
target_link_libraries(tcacore PUBLIC nlohmann_json::nlohmann_json SQLite::SQLite3)

foreach(tool market_analyzer signal_engine risk_engine order_optimizer market_regime
             backtester drawdown_manager trade_journal)
  add_executable(${tool} cpp/src/tools/${tool}.cpp)
  target_link_libraries(${tool} PRIVATE tcacore)
endforeach()

enable_testing()
find_package(GTest REQUIRED)
add_executable(unit_tests cpp/tests/test_indicators.cpp cpp/tests/test_signals.cpp
  cpp/tests/test_risk.cpp cpp/tests/test_execution.cpp cpp/tests/test_backtest.cpp cpp/tests/test_journal.cpp)
target_link_libraries(unit_tests PRIVATE tcacore GTest::gtest GTest::gtest_main)
include(GoogleTest)
gtest_discover_tests(unit_tests)

find_package(benchmark QUIET)
if(benchmark_FOUND)
  add_executable(benchmark cpp/benchmarks/benchmark.cpp)
  target_link_libraries(benchmark PRIVATE tcacore benchmark::benchmark)
endif()
