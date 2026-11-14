#include "../test_support.hpp"

#include <cstdlib>
#include <string>

#include <nexus/core/fs.hpp>
#include <nexus/core/strings.hpp>
#include <nexus/platform/game_source.hpp>

using namespace nexus_test;

namespace {

// Detach detection from the developer's real home/working tree so the test is
// deterministic and never reads a user's game files.
struct IsolatedEnv {
    std::string home;
    explicit IsolatedEnv(const std::string& root) : home(root) {
        ::setenv("HOME", home.c_str(), 1);
        ::unsetenv("NEXUS_GAME_DIR");
        ::unsetenv("APPIMAGE");
        ::unsetenv("NEXUS_HOME");
    }
};

void test_scan_directory() {
    const std::string dir = make_temp("nexus-game-scan-");
    nexus::fs::write_file(nexus::fs::join(dir, "game.apk"), "this is not a zip");
    nexus::fs::write_file(nexus::fs::join(dir, "readme.txt"), "ignore me");
    nexus::fs::write_file(nexus::fs::join(dir, "OTHER.APK"), "also not a zip");

    const auto candidates = nexus::platform::scan_directory_for_apks(dir, "test");
    CHECK_EQ(candidates.size(), static_cast<size_t>(2));
    bool saw_lower = false;
    bool saw_upper = false;
    for (const auto& candidate : candidates) {
        CHECK(candidate.present);
        CHECK(candidate.is_apk);
        CHECK(!candidate.valid);
        CHECK(!candidate.reason.empty());
        const std::string name = nexus::basename(candidate.path);
        if (name == "game.apk") saw_lower = true;
        if (name == "OTHER.APK") saw_upper = true;
    }
    CHECK(saw_lower);
    CHECK(saw_upper);
    nexus::fs::remove_all(dir);
}

void test_detect_empty_directory() {
    const std::string root = make_temp("nexus-game-home-");
    const std::string empty = make_temp("nexus-game-empty-");
    IsolatedEnv env(root);

    const auto report = nexus::platform::detect_game_sources(empty);
    CHECK(!report.found);
    CHECK(report.chosen.empty());
    CHECK(!report.error.empty());
    CHECK(report.candidates.empty());

    nexus::fs::remove_all(root);
    nexus::fs::remove_all(empty);
}

void test_detect_missing_path() {
    const std::string root = make_temp("nexus-game-home-");
    IsolatedEnv env(root);
    const std::string missing = nexus::fs::join(root, "does-not-exist");
    const auto report = nexus::platform::detect_game_sources(missing);
    CHECK(!report.found);
    CHECK(report.candidates.empty());
    nexus::fs::remove_all(root);
}

void test_env_origin() {
    const std::string root = make_temp("nexus-game-home-");
    const std::string game = make_temp("nexus-game-env-");
    IsolatedEnv env(root);
    nexus::fs::write_file(nexus::fs::join(game, "mcpe.apk"), "not a zip");
    ::setenv("NEXUS_GAME_DIR", game.c_str(), 1);

    const auto report = nexus::platform::detect_game_sources("");
    bool found_origin = false;
    for (const auto& candidate : report.candidates) {
        if (nexus::basename(candidate.path) == "mcpe.apk" &&
            candidate.origin == "$NEXUS_GAME_DIR") {
            found_origin = true;
        }
    }
    CHECK(found_origin);
    CHECK(!report.found);  // garbage APK must never be reported as usable

    ::unsetenv("NEXUS_GAME_DIR");
    nexus::fs::remove_all(root);
    nexus::fs::remove_all(game);
}

void test_json_shape() {
    const std::string root = make_temp("nexus-game-home-");
    IsolatedEnv env(root);
    const auto report = nexus::platform::detect_game_sources("");
    const auto json = report.to_json();
    CHECK(json.contains("found"));
    CHECK(json.contains("candidates"));
    CHECK(json["candidates"].is_array());
    CHECK(report.to_markdown().find("Game package detection") != std::string::npos);
    nexus::fs::remove_all(root);
}

void run() {
    test_scan_directory();
    test_detect_empty_directory();
    test_detect_missing_path();
    test_env_origin();
    test_json_shape();
}

} // namespace

int main() { return nexus_test::run_all("test_game_source", {{"all", run}}); }
