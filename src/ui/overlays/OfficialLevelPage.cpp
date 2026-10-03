#include "OfficialLevelPage.hpp"
#include "WaveOverlay.hpp"
#include "../core/Text.hpp"
#include <Geode/Geode.hpp>
using namespace geode::prelude;
namespace lazer {
namespace {
class OfficialPage : public WaveOverlay {
public:
    std::function<void()> play;
    bool playing = false;
    static OfficialPage* create(levels::Entry const& e, std::function<void()> action) {
        auto page = new OfficialPage();
        if (!page->build(e)) { delete page; return nullptr; }
        page->play = std::move(action); page->autorelease(); return page;
    }
    bool build(levels::Entry const& e) {
        if (!WaveOverlay::init(0, theme::Scheme{190}, icon::CIRCLE_PLAY,
            e.name, "by RobTop · official level")) return false;
        auto size = bodySize();
        float pad = 50 * m_k;
        float bandH = 110 * m_k;
        auto band = RoundedBox::create({size.width - 2 * pad, bandH}, 8 * m_k, m_scheme.background3());
        band->setPosition({size.width / 2, size.height - bandH / 2 - 20 * m_k}); body()->addChild(band);
        auto face = GJDifficultySprite::create(e.difficulty, GJDifficultyName::Short);
        face->setScale(65 * m_k / face->getContentSize().height);
        face->setPosition({65 * m_k, bandH / 2}); band->addChild(face);
        auto song = makeText(e.songTitle.empty() ? "official soundtrack" : e.songTitle, Weight::SemiBold, 22 * m_k);
        song->setAnchorPoint({0,0.5f}); song->setPosition({120 * m_k, bandH * 0.65f});
        float maxSongW = band->getContentSize().width - 180 * m_k;
        if (song->getScaledContentSize().width > maxSongW) song->setScale(song->getScale() * maxSongW / song->getScaledContentSize().width);
        band->addChild(song);
        auto artist = makeText(e.songArtist.empty() ? "RobTop's main levels" : e.songArtist, Weight::Regular, 15 * m_k);
        artist->setAnchorPoint({0,0.5f}); artist->setPosition({120 * m_k, bandH * 0.35f}); band->addChild(artist);
        float top = size.height - bandH - 50 * m_k;
        float columnW = (size.width - 2 * pad) / 2;
        int index = 0;
        auto stat = [&](char const* glyph, std::string label, std::string value) {
            int col = index % 2, row = index / 2; ++index;
            auto node = InfoRow::create(columnW - 20 * m_k, m_k, glyph,
                [label] { return label; }, [value] { return value; });
            node->setPosition({pad + col * columnW, top - (row + 1) * 80 * m_k});
            body()->addChild(node);
        };
        stat(icon::CHART, "normal mode", fmt::format("{}%", e.level->m_normalPercent.value()));
        stat(icon::BOLT, "practice mode", fmt::format("{}%", e.level->m_practicePercent));
        stat(icon::COINS, "secret coins", fmt::format("{} / {}", e.coinsCollected, e.coins));
        stat(e.platformer ? icon::MOON : icon::STAR, e.platformer ? "moons" : "stars", std::to_string(e.stars));
        stat(icon::ROTATE, "attempts", std::to_string(e.level->m_attempts.value()));
        stat(icon::ARROW_UP, "jumps", std::to_string(e.level->m_jumps.value()));
        if (e.platformer) {
            std::string time = e.bestTime > 0 ? fmt::format("{}:{:02}.{:03}", e.bestTime / 60000, e.bestTime / 1000 % 60, e.bestTime % 1000) : "not completed";
            stat(icon::CLOCK, "best time", time);
        }
        float buttonW = 180 * m_k;
        auto backButton = ButtonRow::create("back", buttonW, m_k, [this] { close(); });
        backButton->setPosition({pad, 15 * m_k}); body()->addChild(backButton); addInteractive(backButton);
        auto playButton = ButtonRow::create("play level", buttonW, m_k, [this] { playing = true; close(); });
        playButton->setColor(m_scheme.highlight1());
        playButton->setPosition({size.width - pad - buttonW, 15 * m_k}); body()->addChild(playButton); addInteractive(playButton);
        for (auto pair : {std::pair{backButton, icon::CHEVRON_LEFT}, std::pair{playButton, icon::PLAY}}) {
            auto glyph = makeIcon(pair.second, 14 * m_k);
            anchorOnGlyph(glyph);
            glyph->setPosition({24 * m_k, 21 * m_k}); pair.first->addChild(glyph, 1);
        }
        return true;
    }
    void onClosed() override {
        auto action = playing ? play : std::function<void()>{};
        Ref<CCScene> scene = CCDirector::get()->getRunningScene();
        Loader::get()->queueInMainThread([scene, action] {
            if (CCDirector::get()->getRunningScene() != scene.data()) return;
            CCDirector::get()->popScene();
            if (action) action();
        });
    }
};
class OfficialSceneLayer : public CCLayer {
public:
    OfficialPage* page = nullptr;
    void keyBackClicked() override { if (page) page->back(); }
};
}
void showOfficialLevelPage(levels::Entry const& entry, std::function<void()> play) {
    auto page = OfficialPage::create(entry, std::move(play));
    if (!page) return;
    auto scene = CCScene::create();
    auto layer = new OfficialSceneLayer();
    if (!layer->init()) { delete layer; return; }
    layer->autorelease(); layer->setKeypadEnabled(true); layer->page = page;
    auto background = CCLayerColor::create({15,18,23,255}); layer->addChild(background);
    layer->addChild(page,1); scene->addChild(layer); page->open();
    CCDirector::get()->pushScene(scene);
}
}
