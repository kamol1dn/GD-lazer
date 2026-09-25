#pragma once

#include <Geode/Geode.hpp>
#include <string>
#include <vector>

namespace lazer {

// GD's own option toggles, read from a hidden MoreOptionsLayer so the list is
// always whatever this GD version (and any mods hooking addToggle) provides.
// Toggling goes through GD's own toggler, so GD's side effects still run.
class GDOptions {
public:
    struct Toggle {
        std::string label;
        std::string key;
        std::string description;
        int page = 0;
        geode::Ref<CCMenuItemToggler> toggler;
    };

    // Builds the hidden layer and captures its toggles. Cheap enough to do per overlay.
    GDOptions();

    std::vector<Toggle> const& toggles() const { return m_toggles; }
    std::vector<std::string> const& pageNames() const { return m_pageNames; }

    bool isOn(Toggle const& t) const;
    // Flips the option exactly as clicking it in GD's options would. Returns the new state.
    bool toggle(Toggle const& t);

    MoreOptionsLayer* layer() const { return m_layer; }

private:
    geode::Ref<MoreOptionsLayer> m_layer;
    std::vector<Toggle> m_toggles;
    std::vector<std::string> m_pageNames;
};

} // namespace lazer
