#pragma once

#include "Easing.hpp"
#include "RoundedBox.hpp"
#include "ScrollArea.hpp"
#include "SettingsRows.hpp"

#include <Geode/Geode.hpp>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace lazer {

class GDOptions;

// osu!'s settings panel (osu.Game/Overlays/SettingsPanel.cs): slides in from
// the left under the toolbar, icon sidebar that expands on hover, sticky
// header with search, sections that scroll smoothly.
class SettingsOverlay : public cocos2d::CCNode, public cocos2d::CCTouchDelegate {
public:
    // `topInset`: height reserved at the top (the toolbar stays usable).
    static SettingsOverlay* create(float topInset);

    // Build the content. `sections` is filled by the caller via addSection / add*.
    void beginSection(std::string const& name, char const* icon);
    void addSubsection(std::string const& title);
    void addRow(SettingsRow* row);
    void finish();

    // Keep an object alive as long as the overlay (e.g. the hidden GD options layer).
    void keepAlive(std::shared_ptr<void> obj) { m_keepAlive.push_back(std::move(obj)); }

    float rowWidth() const { return m_rowWidth; }
    float k() const { return m_k; }

    void open();
    void close();
    bool isOpen() const { return m_open; }
    // Escape: clears the search first, then closes. Returns true if handled.
    bool back();

    void update(float dt) override;
    void onEnter() override;
    void onExit() override;
    bool ccTouchBegan(cocos2d::CCTouch* touch, cocos2d::CCEvent*) override;
    void ccTouchMoved(cocos2d::CCTouch* touch, cocos2d::CCEvent*) override;
    void ccTouchEnded(cocos2d::CCTouch* touch, cocos2d::CCEvent*) override;
    void ccTouchCancelled(cocos2d::CCTouch* touch, cocos2d::CCEvent*) override;

protected:
    bool init(float topInset);
    void layout();
    void applyFilter(std::string const& query, bool resetScroll = true);
    // Rows whose applicability changed since the last layout (see SettingsRow::setShownIf).
    bool applicabilityChanged();
    SettingsRow* rowAt(cocos2d::CCPoint world);
    int sidebarButtonAt(cocos2d::CCPoint world);
    void updateTooltip(float dt, SettingsRow* hovered);

    struct Section {
        std::string name;
        char const* icon;
        std::vector<SettingsRow*> rows; // header first
        RoundedBox* background = nullptr;
        cocos2d::CCNode* sidebarButton = nullptr;
        cocos2d::CCLabelBMFont* sidebarIcon = nullptr;
        cocos2d::CCLabelBMFont* sidebarLabel = nullptr;
        float top = 0;
        bool visible = true;
    };

    float m_k = 1;
    float m_topInset = 0;
    float m_sidebarWidth = 0;
    float m_sidebarExpandedWidth = 0;
    float m_panelWidth = 0;
    float m_rowWidth = 0;
    float m_margin = 0;
    float m_headerHeight = 0;

    bool m_open = false;
    std::vector<Section> m_sections;
    std::vector<bool> m_applicable; // last seen SettingsRow::applicable() of every row
    int m_currentSection = 0;

    cocos2d::CCLayerColor* m_dim = nullptr;
    cocos2d::CCNode* m_root = nullptr;
    cocos2d::CCNode* m_sidebar = nullptr;
    RoundedBox* m_sidebarBg = nullptr;
    RoundedBox* m_sidebarSelection = nullptr;
    ScrollArea* m_scroll = nullptr;
    geode::TextInput* m_search = nullptr;
    cocos2d::CCNode* m_tooltip = nullptr;

    Tweened<float> m_slide {0.f};      // 0 = hidden, 1 = shown
    Tweened<float> m_sidebarExpand {0.f};
    Tweened<float> m_selectionY {0.f};

    // Touch state.
    SettingsRow* m_pressedRow = nullptr;
    SettingsRow* m_hoveredRow = nullptr;
    int m_pressedSidebar = -1;
    int m_hoveredSidebar = -1;
    bool m_scrollDragging = false;
    cocos2d::CCPoint m_touchStart;
    cocos2d::CCPoint m_lastTouch;
    float m_dragVelocity = 0;

    std::vector<std::shared_ptr<void>> m_keepAlive;
    SettingsRow* m_tooltipRow = nullptr;
    float m_tooltipTimer = 0;
};

} // namespace lazer
