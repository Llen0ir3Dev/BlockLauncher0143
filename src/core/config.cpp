#include <nexus/core/config.hpp>

#include <fstream>

#include <nexus/core/fs.hpp>

namespace nexus {

bool Config::load(const std::string& path, std::string& error) {
    std::string text;
    if (!fs::read_file(path, text)) {
        error = "cannot read config: " + path;
        return false;
    }
    try {
        data_ = nlohmann::json::parse(text);
        if (!data_.is_object()) {
            error = "config root must be a JSON object";
            return false;
        }
    } catch (const std::exception& ex) {
        error = std::string("config parse error: ") + ex.what();
        return false;
    }
    return true;
}

bool Config::save(const std::string& path, std::string& error) const {
    try {
        if (!fs::write_file(path, data_.dump(2) + "\n")) {
            error = "cannot write config: " + path;
            return false;
        }
    } catch (const std::exception& ex) {
        error = std::string("config serialization error: ") + ex.what();
        return false;
    }
    return true;
}

bool Config::has(const std::string& key) const {
    return data_.contains(key);
}

nlohmann::json Config::get(const std::string& key, const nlohmann::json& fallback) const {
    const auto it = data_.find(key);
    return it == data_.end() ? fallback : *it;
}

std::string Config::get_string(const std::string& key, const std::string& fallback) const {
    const auto it = data_.find(key);
    if (it == data_.end() || !it->is_string()) return fallback;
    return it->get<std::string>();
}

int Config::get_int(const std::string& key, int fallback) const {
    const auto it = data_.find(key);
    if (it == data_.end() || !it->is_number_integer()) return fallback;
    return it->get<int>();
}

bool Config::get_bool(const std::string& key, bool fallback) const {
    const auto it = data_.find(key);
    if (it == data_.end() || !it->is_boolean()) return fallback;
    return it->get<bool>();
}

double Config::get_double(const std::string& key, double fallback) const {
    const auto it = data_.find(key);
    if (it == data_.end() || !it->is_number()) return fallback;
    return it->get<double>();
}

void Config::set(const std::string& key, const nlohmann::json& value) {
    data_[key] = value;
}

void Config::merge(const Config& other) {
    for (auto it = other.data_.begin(); it != other.data_.end(); ++it) {
        data_[it.key()] = it.value();
    }
}

} // namespace nexus
