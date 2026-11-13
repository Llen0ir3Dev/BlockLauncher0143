#pragma once

#include <cstdio>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include <nexus/core/fs.hpp>

namespace nexus_test {

inline int failures = 0;
inline int checks = 0;

inline void record(bool ok, const char* expression, const char* file, int line,
                   const std::string& detail = "") {
    ++checks;
    if (ok) return;
    ++failures;
    std::cerr << "FAIL " << file << ":" << line << "  " << expression;
    if (!detail.empty()) std::cerr << "\n       " << detail;
    std::cerr << "\n";
}

inline std::string make_temp(const std::string& prefix) {
    return nexus::fs::make_temp_dir(prefix);
}

inline int finish(const char* name) {
    if (failures == 0) {
        std::cout << "PASS " << name << " (" << checks << " checks)\n";
        return 0;
    }
    std::cerr << "FAILED " << name << ": " << failures << "/" << checks << " checks failed\n";
    return 1;
}

struct TestCase {
    const char* name;
    void (*fn)();
};

inline int run_all(const char* suite, const std::vector<TestCase>& tests) {
    for (const auto& test : tests) {
        std::cout << "-- " << test.name << "\n";
        try {
            test.fn();
        } catch (const std::exception& ex) {
            ++failures;
            std::cerr << "EXCEPTION in " << test.name << ": " << ex.what() << "\n";
        }
    }
    return finish(suite);
}

} // namespace nexus_test

#define CHECK(condition) \
    ::nexus_test::record(static_cast<bool>(condition), #condition, __FILE__, __LINE__)

#define CHECK_EQ(actual, expected)                                                     \
    do {                                                                               \
        const auto& nexus_test_actual = (actual);                                      \
        const auto& nexus_test_expected = (expected);                                  \
        ::nexus_test::record(nexus_test_actual == nexus_test_expected,                 \
                             #actual " == " #expected, __FILE__, __LINE__,             \
                             "actual: " + ::nexus_test::to_text(nexus_test_actual) +   \
                                 ", expected: " +                                      \
                                 ::nexus_test::to_text(nexus_test_expected));          \
    } while (0)

namespace nexus_test {

template <typename T>
inline std::string to_text(const T& value) {
    std::ostringstream out;
    out << value;
    return out.str();
}

inline std::string to_text(const std::string& value) { return value; }

} // namespace nexus_test
