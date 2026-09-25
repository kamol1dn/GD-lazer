#pragma once

#include "ScrollArea.hpp"
#include "WaveOverlay.hpp"

#include <Geode/Geode.hpp>
#include <string>
#include <vector>

namespace lazer {

// GD's statistics as an osu!-style dashboard: headline currencies, grouped
// tiles, how your completed levels split by difficulty, and shards. Numbers
// roll up when the overlay opens, like osu!'s profile counters.
class StatsOverlay : public WaveOverlay {
public:
    static StatsOverlay* create(float topInset);

    bool ccTouchBegan(cocos2d::CCTouch* touch, cocos2d::CCEvent* e) override;
    void ccTouchMoved(cocos2d::CCTouch* touch, cocos2d::CCEvent* e) override;
    void ccTouchEnded(cocos2d::CCTouch* touch, cocos2d::CCEvent* e) override;

protected:
    struct Counter {
        cocos2d::CCLabelBMFont* label;
        double target;
        int decimals;
    };
    struct Bar {
        RoundedBox* fill;
        cocos2d::CCLabelBMFont* count;
        float fullWidth;
        float fraction;
        int value;
    };
    struct Tile {
        std::string label;
        double value;
        char const* sprite = nullptr; // GD sprite frame
        char const* glyph = nullptr;  // or an icon-font glyph
        int decimals = 0;
    };
    struct BarDef {
        std::string label;
        int value;
        cocos2d::ccColor4B color;
    };

    bool init(float topInset);
    void onOpened() override;
    void onUpdate(float dt) override;

    void build();
    float addSection(std::string const& title, float y);
    float addHeroRow(std::vector<Tile> const& tiles, float y);
    float addTileGrid(std::vector<Tile> const& tiles, int columns, float y);
    float addBarChart(std::string const& title, std::vector<BarDef> const& bars, float x, float width, float y);
    cocos2d::CCNode* makeTileIcon(Tile const& tile, float size);

    ScrollArea* m_scroll = nullptr;
    ScrollDragger m_drag;
    float m_pad = 0;
    float m_gap = 0;
    std::vector<Counter> m_counters;
    std::vector<Bar> m_bars;
    Tweened<float> m_rollUp {0.f};
};

} // namespace lazer
