#pragma once

#include "../core/ScrollArea.hpp"
#include "WaveOverlay.hpp"

#include <Geode/Geode.hpp>
#include <string>
#include <vector>

namespace lazer {

// GD's achievements as an osu!-style overlay: a progress summary, category
// tabs, an all / unlocked / in progress / locked filter and search, over a
// scrolling grid of medal cards. Cards (and their GD reward icons) are only
// built once they scroll into view, since there are ~550 of them.
class AchievementsOverlay : public WaveOverlay {
public:
    static AchievementsOverlay* create(float topInset);

    bool ccTouchBegan(cocos2d::CCTouch* touch, cocos2d::CCEvent* e) override;
    void ccTouchMoved(cocos2d::CCTouch* touch, cocos2d::CCEvent* e) override;
    void ccTouchEnded(cocos2d::CCTouch* touch, cocos2d::CCEvent* e) override;

protected:
    enum class Filter { All, Unlocked, InProgress, Locked };

    struct Entry {
        std::string id;
        std::string title;
        std::string description;
        std::string icon;   // GD unlock, e.g. "ship_12"; empty = none
        std::string search; // lower-case title + description
        int category = 0;
        bool earned = false;
        int percent = 0;
        cocos2d::CCNode* card = nullptr; // built on first view
        RoundedBox* cardBg = nullptr;
    };

    // A clickable text / pill (tabs and filter buttons).
    struct Chip {
        cocos2d::CCNode* node;
        cocos2d::CCLabelBMFont* label;
        RoundedBox* bg; // filter pills only
        int value;
        bool hovered = false;
    };

    bool init(float topInset);
    void onOpened() override;
    void onUpdate(float dt) override;

    void loadEntries();
    void buildSummary();
    void buildTabs();
    void buildFilters();
    void applyFilter();
    cocos2d::CCNode* buildCard(Entry& e);
    void layoutVisibleCards();
    void selectCategory(int category);
    void selectFilter(Filter filter);

    std::vector<Entry> m_entries;
    std::vector<size_t> m_shown; // indices into m_entries, in grid order
    int m_category = 0;
    Filter m_filter = Filter::All;
    std::string m_query;

    float m_pad = 0;
    float m_topHeight = 0;
    cocos2d::CCSize m_cardSize;
    int m_columns = 1;
    float m_gap = 0;

    ScrollArea* m_scroll = nullptr;
    ScrollDragger m_drag;
    geode::TextInput* m_search = nullptr;
    cocos2d::CCLabelBMFont* m_countLabel = nullptr;
    cocos2d::CCLabelBMFont* m_totalLabel = nullptr;
    cocos2d::CCLabelBMFont* m_percentLabel = nullptr;
    RoundedBox* m_progressFill = nullptr;
    float m_progressWidth = 0;
    cocos2d::CCLabelBMFont* m_emptyLabel = nullptr;

    std::vector<Chip> m_tabs;
    std::vector<Chip> m_filters;
    RoundedBox* m_tabUnderline = nullptr;
    Tweened<float> m_underlineX {0.f};
    Tweened<float> m_underlineW {0.f};
    Chip* m_pressedChip = nullptr;
    Entry* m_hoveredCard = nullptr;
    int m_earned = 0;
};

} // namespace lazer
