#pragma once

#include "Easing.hpp"
#include "RoundedBox.hpp"

#include <Geode/cocos/include/cocos2d.h>
#include <functional>
#include <string>

namespace lazer {

// One line in the settings list (osu.Game/Overlays/Settings/*). The overlay
// lays rows out top-down, routes hover / clicks / drags to them, and uses
// searchText() for filtering.
class SettingsRow : public cocos2d::CCNode {
public:
    enum class Kind { SectionHeader, SubsectionHeader, Item };

    virtual Kind kind() const { return Kind::Item; }
    virtual void setHovered(bool) {}
    virtual void onClick(cocos2d::CCPoint local) {}
    virtual bool wantsDrag() const { return false; }
    virtual void onDrag(cocos2d::CCPoint local) {}
    // Re-read the underlying value (e.g. when the overlay opens).
    virtual void refresh() {}

    std::string const& searchText() const { return m_searchText; }
    std::string const& tooltip() const { return m_tooltip; }
    void setTooltip(std::string t) { m_tooltip = std::move(t); }

    // Rows that only make sense sometimes (e.g. "log in" while logged out)
    // hide themselves; the overlay re-checks this while it's open.
    void setShownIf(std::function<bool()> condition) { m_shownIf = std::move(condition); }
    bool applicable() const { return !m_shownIf || m_shownIf(); }

protected:
    std::string m_searchText;
    std::string m_tooltip;
    std::function<bool()> m_shownIf;
};

class SectionHeaderRow : public SettingsRow {
public:
    static SectionHeaderRow* create(std::string const& title, float width, float k);
    Kind kind() const override { return Kind::SectionHeader; }
};

class SubsectionHeaderRow : public SettingsRow {
public:
    static SubsectionHeaderRow* create(std::string const& title, float width, float k);
    Kind kind() const override { return Kind::SubsectionHeader; }
};

// Label + osu!'s pill switch ("Nub").
class ToggleRow : public SettingsRow {
public:
    static ToggleRow* create(std::string const& label, float width, float k,
                             std::function<bool()> get, std::function<bool()> toggle);
    void setHovered(bool hovered) override;
    void onClick(cocos2d::CCPoint local) override;
    void refresh() override;
    void update(float dt) override;

    // Public so the shared create() helper can call it.
    bool init(std::string const& label, float width, float k,
              std::function<bool()> get, std::function<bool()> toggle);

protected:
    void setValue(bool on, bool animate);

    std::function<bool()> m_get;
    std::function<bool()> m_toggle;
    RoundedBox* m_hoverBg = nullptr;
    RoundedBox* m_nub = nullptr;
    float m_nubWidth = 0, m_nubHeight = 0, m_border = 0;
    Tweened<float> m_widthFactor {0.75f};
    Tweened<float> m_fill {0.f};
    Tweened<float> m_borderScale {1.f};
    Tweened<float> m_hover {0.f};
};

// Label, value and a draggable bar.
class SliderRow : public SettingsRow {
public:
    static SliderRow* create(std::string const& label, float width, float k,
                             std::function<float()> get, std::function<void(float)> set,
                             std::function<std::string(float)> format);
    void setHovered(bool hovered) override;
    bool wantsDrag() const override { return true; }
    void onDrag(cocos2d::CCPoint local) override;
    void onClick(cocos2d::CCPoint local) override { onDrag(local); }
    void refresh() override;
    void update(float dt) override;

    // Public so the shared create() helper can call it.
    bool init(std::string const& label, float width, float k,
              std::function<float()> get, std::function<void(float)> set,
              std::function<std::string(float)> format);

protected:
    void layoutBar();
    void playTick();

    std::function<float()> m_get;
    std::function<void(float)> m_set;
    std::function<std::string(float)> m_format;
    float m_value = 0;
    float m_barX = 0, m_barW = 0, m_barY = 0;
    cocos2d::CCLabelBMFont* m_valueLabel = nullptr;
    RoundedBox* m_track = nullptr;
    RoundedBox* m_filled = nullptr;
    RoundedBox* m_nub = nullptr;
    Tweened<float> m_hover {0.f};
    double m_lastTickMs = -1000;
    std::string m_lastTickValue;
};

// An icon with a title and a subtitle that follow live values (account status...).
class InfoRow : public SettingsRow {
public:
    static InfoRow* create(float width, float k, char const* glyph,
                           std::function<std::string()> title, std::function<std::string()> subtitle);
    void refresh() override;
    void update(float dt) override;

    // Public so the shared create() helper can call it.
    bool init(float width, float k, char const* glyph,
              std::function<std::string()> title, std::function<std::string()> subtitle);

protected:
    std::function<std::string()> m_title;
    std::function<std::string()> m_subtitle;
    cocos2d::CCLabelBMFont* m_titleLabel = nullptr;
    cocos2d::CCLabelBMFont* m_subtitleLabel = nullptr;
    std::string m_lastTitle, m_lastSubtitle;
};

// Full-width rounded button.
class ButtonRow : public SettingsRow {
public:
    static ButtonRow* create(std::string const& label, float width, float k,
                             std::function<void()> action, bool dangerous = false);
    void setHovered(bool hovered) override;
    void onClick(cocos2d::CCPoint local) override;
    void update(float dt) override;

    void setColor(cocos2d::ccColor4B color) { m_color = color; }
    void setEnabled(bool enabled) { m_enabled = enabled; }

    // Public so the shared create() helper can call it.
    bool init(std::string const& label, float width, float k, std::function<void()> action, bool dangerous);

protected:
    std::function<void()> m_action;
    RoundedBox* m_bg = nullptr;
    cocos2d::ccColor4B m_color {};
    bool m_enabled = true;
    Tweened<float> m_hover {0.f};
    Tweened<float> m_flash {0.f};
};

} // namespace lazer
