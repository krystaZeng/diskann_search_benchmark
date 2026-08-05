#pragma once

#include <exception>
#include <iostream>
#include <string>

namespace diskann_pipeline::test {

inline int failure_count = 0;

inline void fail(const char* expression, const char* file, int line,
                 const std::string& detail = {}) {
  ++failure_count;
  std::cerr << file << ':' << line << ": assertion failed: " << expression;
  if (!detail.empty()) {
    std::cerr << " (" << detail << ')';
  }
  std::cerr << '\n';
}

template <typename Function>
void expect_throws(Function&& function, const char* expression, const char* file,
                   int line) {
  try {
    function();
  } catch (const std::exception&) {
    return;
  } catch (...) {
    return;
  }
  fail(expression, file, line, "expected an exception");
}

inline int finish() {
  if (failure_count != 0) {
    std::cerr << failure_count << " assertion(s) failed\n";
  }
  return failure_count == 0 ? 0 : 1;
}

}  // namespace diskann_pipeline::test

#define DP_EXPECT_TRUE(expression)                                             \
  do {                                                                         \
    if (!(expression)) {                                                       \
      ::diskann_pipeline::test::fail(#expression, __FILE__, __LINE__);        \
    }                                                                          \
  } while (false)

#define DP_EXPECT_EQ(actual, expected)                                         \
  do {                                                                         \
    const auto& dp_actual = (actual);                                           \
    const auto& dp_expected = (expected);                                       \
    if (!(dp_actual == dp_expected)) {                                          \
      ::diskann_pipeline::test::fail(#actual " == " #expected, __FILE__,      \
                                     __LINE__);                                 \
    }                                                                          \
  } while (false)

#define DP_EXPECT_THROW(expression)                                             \
  ::diskann_pipeline::test::expect_throws(                                     \
      [&]() { static_cast<void>(expression); }, #expression, __FILE__,         \
      __LINE__)

