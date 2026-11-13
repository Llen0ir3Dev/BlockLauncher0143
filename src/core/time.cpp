#include <nexus/core/time.hpp>

#include <ctime>
#include <iomanip>
#include <sstream>

#include <nexus/core/log.hpp>

namespace nexus {

uint64_t now_ms() {
    using namespace std::chrono;
    return static_cast<uint64_t>(duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count());
}

uint64_t now_us() {
    using namespace std::chrono;
    return static_cast<uint64_t>(duration_cast<microseconds>(system_clock::now().time_since_epoch()).count());
}

std::string iso8601_now() {
    const std::time_t now = std::time(nullptr);
    std::tm tm{};
#if defined(_WIN32)
    gmtime_s(&tm, &now);
#else
    gmtime_r(&now, &tm);
#endif
    std::ostringstream ss;
    ss << std::put_time(&tm, "%Y-%m-%dT%H:%M:%SZ");
    return ss.str();
}

double monotonic_seconds() {
    using namespace std::chrono;
    static const auto start = steady_clock::now();
    return duration<double>(steady_clock::now() - start).count();
}

FrameTimer::FrameTimer() : last_(std::chrono::steady_clock::now()) {}

double FrameTimer::tick() {
    const auto now = std::chrono::steady_clock::now();
    delta_ = std::chrono::duration<double>(now - last_).count();
    last_ = now;
    accumulated_ += delta_;
    ++frames_;
    ++frame_;
    return delta_;
}

double FrameTimer::fps() const {
    if (accumulated_ <= 0.0) return 0.0;
    return static_cast<double>(frames_) / accumulated_;
}

ScopedTimer::ScopedTimer(const std::string& label) : label_(label), start_(std::chrono::steady_clock::now()) {}

ScopedTimer::~ScopedTimer() {
    const double ms = elapsed_ms();
    NEXUS_LOGF(LogLevel::Debug, "%s took %.3f ms", label_.c_str(), ms);
}

double ScopedTimer::elapsed_ms() const {
    const auto now = std::chrono::steady_clock::now();
    return std::chrono::duration<double, std::milli>(now - start_).count();
}

} // namespace nexus
