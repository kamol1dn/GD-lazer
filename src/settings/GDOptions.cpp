#include "GDOptions.hpp"

#include <Geode/modify/MoreOptionsLayer.hpp>
#include <algorithm>
#include <unordered_set>

using namespace geode::prelude;

namespace lazer {

namespace {
    // Non-null only while GDOptions is building its hidden layer.
    std::vector<GDOptions::Toggle>* g_capture = nullptr;
    std::unordered_set<CCMenuItemToggler*>* g_seen = nullptr;

    void collectTogglers(CCNode* node, std::vector<CCMenuItemToggler*>& out) {
        if (auto t = typeinfo_cast<CCMenuItemToggler*>(node)) out.push_back(t);
        for (auto child : CCArrayExt<CCNode*>(node->getChildren())) collectTogglers(child, out);
    }
}

class $modify(CaptureMoreOptionsLayer, MoreOptionsLayer) {
    void addToggle(char const* label, char const* key, char const* description) {
        // GD counts toggles per page as it adds them; the page they land on is
        // the one being filled right now.
        int page = m_pageCount;
        MoreOptionsLayer::addToggle(label, key, description);
        if (!g_capture) return;

        // Find the toggler this call just created.
        std::vector<CCMenuItemToggler*> all;
        collectTogglers(this, all);
        CCMenuItemToggler* created = nullptr;
        for (auto t : all) {
            if (!g_seen->contains(t)) {
                g_seen->insert(t);
                created = t;
            }
        }
        // GD hard-wraps labels for its narrow pages ("Disable Trigger\nOrb Scale").
        std::string text = label ? label : "";
        std::replace(text.begin(), text.end(), '\n', ' ');
        g_capture->push_back({
            text, key ? key : "", description ? description : "", page, created,
        });
    }
};

GDOptions::GDOptions() {
    std::unordered_set<CCMenuItemToggler*> seen;
    g_capture = &m_toggles;
    g_seen = &seen;
    m_layer = MoreOptionsLayer::create();
    g_capture = nullptr;
    g_seen = nullptr;
    if (!m_layer) return;

    // Page titles: switch the hidden layer through its pages and read the header.
    int pages = 0;
    for (auto const& t : m_toggles) pages = std::max(pages, t.page + 1);
    for (int i = 0; i < pages; i++) {
        m_layer->goToPage(i);
        std::string name = m_layer->m_categoryLabel ? m_layer->m_categoryLabel->getString() : "";
        m_pageNames.push_back(name.empty() ? fmt::format("Page {}", i + 1) : name);
    }
    m_layer->goToPage(0);

    for (auto const& t : m_toggles) {
        log::debug("GD option: page {} ({}) key {} '{}'", t.page, m_pageNames[t.page], t.key, t.label);
    }
}

bool GDOptions::isOn(Toggle const& t) const {
    // The toggler's own state already accounts for options GD shows inverted.
    if (t.toggler) return t.toggler->isToggled();
    return GameManager::get()->getGameVariable(t.key.c_str());
}

bool GDOptions::toggle(Toggle const& t) {
    if (t.toggler) {
        t.toggler->activate();
    } else {
        GameManager::get()->toggleGameVariable(t.key.c_str());
    }
    return isOn(t);
}

} // namespace lazer
