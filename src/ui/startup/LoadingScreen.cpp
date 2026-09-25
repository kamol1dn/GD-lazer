// GD's loading screen, the osu! way (osu.Game/Screens/Loader.cs): plain black
// with a small spinner in the bottom right, so the game opens straight into
// the intro. GD's loading keeps running underneath; we only cover it.
//
// The mod is early-loaded for this (mod.json "early-load"), which also means
// none of our resources are loaded yet: everything here is drawn in code.

#include "../../audio/MusicPlayer.hpp"
#include "../core/Easing.hpp"
#include "../core/RoundedBox.hpp"

#include <Geode/Geode.hpp>
#include <Geode/modify/LoadingLayer.hpp>

using namespace geode::prelude;

namespace lazer {

namespace {
    // LoadingSpinner.cs
    constexpr float TRANSITION_DURATION = 500;
    constexpr float SPIN_DURATION = 3150;
    constexpr float BEAT_LENGTH = 1000; // no music yet: the default 60 bpm
    constexpr float SHOW_DELAY = 200;   // Loader: spinner.Show after 200 ms

    // White rounded box turning a quarter each beat, with a black arc spinning inside.
    class LoadingSpinner : public CCNode {
    public:
        static LoadingSpinner* create(float size) {
            auto ret = new LoadingSpinner();
            ret->init(size);
            ret->autorelease();
            return ret;
        }

        void update(float dt) override {
            float ms = dt * 1000.f;
            m_timeMs += ms;

            if (!m_shown && m_timeMs >= SHOW_DELAY) {
                m_shown = true;
                m_scale.to(1, TRANSITION_DURATION, Easing::OutQuint);
                m_alpha.to(1, TRANSITION_DURATION, Easing::OutQuint);
            }
            m_sinceBeat += ms;
            if (m_sinceBeat >= BEAT_LENGTH) {
                m_sinceBeat -= BEAT_LENGTH;
                m_target += 90;
                m_rotation.to(m_target, BEAT_LENGTH, Easing::InOutQuart);
            }
            m_scale.update(dt);
            m_alpha.update(dt);
            m_rotation.update(dt);

            m_main->setScale(m_scale);
            m_main->setRotation(m_rotation);
            auto alpha = static_cast<GLubyte>(std::clamp(m_alpha.get(), 0.f, 1.f) * 255);
            m_box->setOpacity(alpha);

            // The glyph spins on its own, one turn per 3150 ms.
            float spin = std::fmod(m_timeMs / SPIN_DURATION, 1.f) * 360.f;
            m_arc->setRotation(spin);
            m_arc->setVisible(alpha > 0);
            drawArc(alpha / 255.f);
        }

    private:
        void init(float size) {
            CCNode::init();
            m_size = size;
            m_main = CCNode::create();
            this->addChild(m_main);

            // MainContents.CornerRadius = DrawWidth / 4
            m_box = RoundedBox::create({size, size}, size / 4, {255, 255, 255, 255});
            m_box->setOpacity(0);
            m_main->addChild(m_box);

            m_arc = CCDrawNode::create();
            m_main->addChild(m_arc, 1);

            m_scale.set(0.6f);
            m_alpha.set(0);
            this->scheduleUpdate();
        }

        // Font Awesome's circle-notch, drawn: a thick 3/4 ring.
        void drawArc(float alpha) {
            m_arc->clear();
            constexpr float PI = 3.14159265f;
            float r = m_size * 0.6f * 0.5f * 0.8f; // glyph at 0.6 of the box
            float thick = r * 0.3f;
            int segments = 36;
            ccColor4F black {0, 0, 0, alpha};
            for (int i = 0; i < segments; i++) {
                float a0 = PI / 2 + 1.5f * PI * i / segments;
                float a1 = PI / 2 + 1.5f * PI * (i + 1) / segments;
                m_arc->drawSegment(CCPoint(std::cos(a0), std::sin(a0)) * r, CCPoint(std::cos(a1), std::sin(a1)) * r,
                                   thick / 2, black);
            }
        }

        float m_size = 0;
        CCNode* m_main = nullptr;
        RoundedBox* m_box = nullptr;
        CCDrawNode* m_arc = nullptr;
        float m_timeMs = 0;
        float m_sinceBeat = 0;
        float m_target = 0;
        bool m_shown = false;
        Tweened<float> m_scale {0.6f};
        Tweened<float> m_alpha {0.f};
        Tweened<float> m_rotation {0.f};
    };
}

class $modify(LazerLoadingLayer, LoadingLayer) {
    bool init(bool fromReload) {
        if (!LoadingLayer::init(fromReload)) return false;
        auto mod = Mod::get();
        if (!mod->getSettingValue<bool>("enabled")) return true;
        // GD starts the menu song as loading ends: keep it for the intro.
        if (!fromReload && mod->getSettingValue<bool>("intro")) MusicPlayer::get().holdForIntro();

        auto win = CCDirector::get()->getWinSize();
        float k = win.height / 768.f;

        auto cover = CCLayerColor::create({0, 0, 0, 255});
        cover->setID("loading-cover"_spr);
        this->addChild(cover, 1000);

        float size = 60 * k;
        auto spinner = LoadingSpinner::create(size);
        spinner->setPosition({win.width - 40 * k - size / 2, 40 * k + size / 2});
        cover->addChild(spinner);
        return true;
    }
};

} // namespace lazer
