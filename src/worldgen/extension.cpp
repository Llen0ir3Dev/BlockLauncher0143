#include <nexus/worldgen/extension.hpp>

namespace nexus::worldgen {

void ExtensionRegistry::add(std::shared_ptr<GenerationExtension> extension) {
    if (!extension) return;
    for (auto& existing : extensions_) {
        if (existing->name() == extension->name()) {
            existing = std::move(extension);
            sort_by_priority();
            return;
        }
    }
    extensions_.push_back(std::move(extension));
    sort_by_priority();
}

bool ExtensionRegistry::remove(const std::string& name) {
    for (auto it = extensions_.begin(); it != extensions_.end(); ++it) {
        if ((*it)->name() == name) {
            extensions_.erase(it);
            return true;
        }
    }
    return false;
}

void ExtensionRegistry::clear() { extensions_.clear(); }

void ExtensionRegistry::sort_by_priority() {
    std::stable_sort(extensions_.begin(), extensions_.end(),
                     [](const auto& a, const auto& b) { return a->priority() < b->priority(); });
}

} // namespace nexus::worldgen
