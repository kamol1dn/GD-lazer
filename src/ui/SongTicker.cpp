#include "SongTicker.hpp"

#include "Easing.hpp"
#include "Text.hpp"
#include "Theme.hpp"

#include <Geode/Geode.hpp>

using namespace geode::prelude;

namespace lazer {

namespace {
    constexpr float FADE_MS = 800;  // SongTicker.fade_duration
    constexpr float HOLD_MS = 4000;
}

SongTicker* SongTicker::create(float k) {
    auto ret = new SongTicker();
    if (ret->init(k)) {
        ret->autorelease();
        return ret;
    }
    delete ret;
    return nullptr;
}

bool SongTicker::init(float k) {
    if (!CCNodeRGBA::init()) return false;
    m_k = k;
    this->setCascadeOpacityEnabled(true);
    this->setAnchorPoint({1, 1});

    // Right-aligned stack: title, artist, then which level it's from.
    m_title = makeText("", Weight::Regular, 24 * k);
    m_title->setAnchorPoint({1, 1});
    this->addChild(m_title);
    m_artist = makeText("", Weight::Regular, 16 * k);
    m_artist->setAnchorPoint({1, 1});
    this->addChild(m_artist);
    m_level = makeText("", Weight::Regular, 13 * k);
    m_level->setAnchorPoint({1, 1});
    m_level->setColor(theme::LIGHT1);
    this->addChild(m_level);

    this->setOpacity(0);
    this->scheduleUpdate();
    return true;
}

void SongTicker::show(MusicPlayer::Track const* track) {
    if (!track) return hide();
    m_title->setString(track->title.c_str());
    m_artist->setString(track->artist.c_str());
    std::string level;
    if (!track->levels.empty()) {
        auto const& l = track->levels.front();
        level = l.creator.empty() ? l.name : fmt::format("{} by {}", l.name, l.creator);
        if (track->levels.size() > 1) level += fmt::format(" (+{} more)", track->levels.size() - 1);
    }
    m_level->setString(level.c_str());

    float spacing = 3 * m_k;
    float y = 0;
    for (auto label : {m_title, m_artist, m_level}) {
        label->setPosition({0, y});
        y -= label->getScaledContentSize().height + spacing;
    }
    m_timeMs = 0;
}

void SongTicker::hide() {
    if (m_timeMs >= 0 && m_timeMs < FADE_MS / 2 + HOLD_MS) m_timeMs = FADE_MS / 2 + HOLD_MS;
}

void SongTicker::update(float dt) {
    if (m_timeMs < 0) return;
    m_timeMs += dt * 1000.f;
    // FadeInFromZero(fade / 2), Delay(4000), FadeOut(fade).
    float alpha;
    if (m_timeMs < FADE_MS / 2) alpha = m_timeMs / (FADE_MS / 2);
    else if (m_timeMs < FADE_MS / 2 + HOLD_MS) alpha = 1;
    else alpha = 1 - (m_timeMs - FADE_MS / 2 - HOLD_MS) / FADE_MS;
    if (alpha <= 0) {
        alpha = 0;
        m_timeMs = -1;
    }
    this->setOpacity(static_cast<GLubyte>(std::clamp(alpha, 0.f, 1.f) * 255));
}

} // namespace lazer
