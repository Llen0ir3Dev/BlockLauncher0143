#pragma once

#include <chrono>
#include <cstdint>
#include <string>

namespace nexus {

uint64_t now_ms();
uint64_t now_us();
std::string iso8601_now();
double monotonic_seconds();

class FrameTimer {
public:
    FrameTimer();
    double tick();
    double delta() const { return delta_; }
    double fps() const;
    uint64_t frame() const { return frame_; }

private:
    std::chrono::steady_clock::time_point last_;
    double delta_ = 0.0;
    double accumulated_ = 0.0;
    uint64_t frames_ = 0;
    uint64_t frame_ = 0;
};

class ScopedTimer {
public:
    explicit ScopedTimer(const std::string& label);
    ~ScopedTimer();
    double elapsed_ms() const;

private:
    std::string label_;
    std::chrono::steady_clock::time_point start_;
};

} // namespace nexus
