#pragma once

#include <string>

#include <nlohmann/json.hpp>

namespace nexus {

class Config {
public:
    Config() : data_(nlohmann::json::object()) {}
    explicit Config(const nlohmann::json& data) : data_(data) {}

    bool load(const std::string& path, std::string& error);
    bool save(const std::string& path, std::string& error) const;

    bool has(const std::string& key) const;
    nlohmann::json get(const std::string& key, const nlohmann::json& fallback = nullptr) const;
    std::string get_string(const std::string& key, const std::string& fallback = "") const;
    int get_int(const std::string& key, int fallback = 0) const;
    bool get_bool(const std::string& key, bool fallback = false) const;
    double get_double(const std::string& key, double fallback = 0.0) const;

    void set(const std::string& key, const nlohmann::json& value);
    void merge(const Config& other);
    const nlohmann::json& raw() const { return data_; }
    nlohmann::json& raw() { return data_; }

private:
    nlohmann::json data_;
};

} // namespace nexus
