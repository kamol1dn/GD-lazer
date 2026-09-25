#pragma once

#include "../core/Easing.hpp"
#include "../core/RoundedBox.hpp"
#include "../core/Theme.hpp"
#include "SettingsRows.hpp"

#include <Geode/cocos/include/cocos2d.h>
#include <array>
#include <string>
#include <vector>

namespace lazer {

// osu!'s full-screen overlay (osu.Game/Overlays/FullscreenOverlay.cs +
// Graphics/Containers/WaveContainer.cs): four tinted, tilted "waves" sweep up
// the screen, the content panel rises after them, a header shows the overlay's
// icon, title and description. Subclasses fill body().
class WaveOverlay : public cocos2d::CCNode, public cocos2d::CCTouchDelegate {
public:
    void open();
    void close();
    bool isOpen() const { return m_open; }
    bool back();

    void update(float dt) override;
    void onEnter() override;
    void onExit() override;
    bool ccTouchBegan(cocos2d::CCTouch* touch, cocos2d::CCEvent*) override;
    void ccTouchEnded(cocos2d::CCTouch* touch, cocos2d::CCEvent*) override;
    void ccTouchCancelled(cocos2d::CCTouch* touch, cocos2d::CCEvent* e) override { ccTouchEnded(touch, e); }

protected:
    // `headerHeight` in osu! pixels (default: osu!'s 110).
    bool init(float topInset, theme::Scheme scheme, char const* icon,
              std::string const& title, std::string const& description, float headerHeight = 110.f);

    // Area under the header, in body() space (origin bottom-left).
    cocos2d::CCNode* body() const { return m_body; }
    cocos2d::CCSize bodySize() const { return m_body->getContentSize(); }
    // Rows placed in body() that should get hover / clicks.
    void addInteractive(SettingsRow* row) { m_interactive.push_back(row); }

    virtual void onOpened() {}
    virtual void onUpdate(float dt) {}
    // The close animation has finished and the overlay is now hidden.
    virtual void onClosed() {}

    float m_k = 1;
    theme::Scheme m_scheme {255};

private:
    SettingsRow* rowAt(cocos2d::CCPoint world);

    float m_topInset = 0;
    float m_height = 0;
    bool m_open = false;

    cocos2d::CCNode* m_waveClip = nullptr;
    std::array<RoundedBox*, 4> m_waves {};
    std::array<float, 4> m_waveFinal {};
    std::array<Tweened<float>, 4> m_waveY {};  // distance of each wave's top edge below the overlay's top
    cocos2d::CCNode* m_content = nullptr;
    cocos2d::CCNode* m_body = nullptr;
    Tweened<float> m_contentY {0.f};           // 0 = in place, 1 = one full height below

    std::vector<SettingsRow*> m_interactive;
    SettingsRow* m_hovered = nullptr;
    SettingsRow* m_pressed = nullptr;
};

} // namespace lazer
