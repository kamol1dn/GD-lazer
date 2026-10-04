#include "OfficialLevelPage.hpp"

#include "../core/Text.hpp"
#include "WaveOverlay.hpp"

#include <Geode/Geode.hpp>

using namespace geode::prelude;

namespace lazer {

namespace {
    // RobTop's levels have no GD level page (their button plays them): this
    // one shows what GD saves about them, with play and back below.
    class OfficialPage : public WaveOverlay {
    public:
        static OfficialPage* create(levels::Entry const& e, std::function<void()> play) {
            auto page = new OfficialPage();
            if (!page->build(e)) {
                delete page;
                return nullptr;
            }
            page->m_play = std::move(play);
            page->autorelease();
            return page;
        }

    protected:
        bool build(levels::Entry const& e) {
            if (!WaveOverlay::init(0, theme::Scheme{190}, icon::CIRCLE_PLAY, e.name, "by RobTop · official level")) return false;
            float k = m_k;
            auto size = bodySize();
            float pad = 50 * k;

            // The song, beside the difficulty face.
            float bandH = 110 * k;
            auto band = RoundedBox::create({size.width - 2 * pad, bandH}, 8 * k, m_scheme.background3());
            band->setPosition({size.width / 2, size.height - bandH / 2 - 20 * k});
            body()->addChild(band);
            auto face = GJDifficultySprite::create(e.difficulty, GJDifficultyName::Short);
            face->setScale(65 * k / face->getContentSize().height);
            face->setPosition({65 * k, bandH / 2});
            band->addChild(face);
            auto song = makeText(e.songTitle.empty() ? "official soundtrack" : e.songTitle, Weight::SemiBold, 22 * k);
            song->setAnchorPoint({0, 0.5f});
            song->setPosition({120 * k, bandH * 0.65f});
            float maxSongW = band->getContentSize().width - 180 * k;
            if (song->getScaledContentSize().width > maxSongW) {
                song->setScale(song->getScale() * maxSongW / song->getScaledContentSize().width);
            }
            band->addChild(song);
            auto artist = makeText(e.songArtist.empty() ? "RobTop's main levels" : e.songArtist, Weight::Regular, 15 * k);
            artist->setAnchorPoint({0, 0.5f});
            artist->setPosition({120 * k, bandH * 0.35f});
            band->addChild(artist);

            // Progress and stats, two columns.
            float top = size.height - bandH - 50 * k;
            float columnW = (size.width - 2 * pad) / 2;
            int index = 0;
            auto stat = [&](char const* glyph, std::string label, std::string value) {
                int col = index % 2, row = index / 2;
                ++index;
                auto node = InfoRow::create(columnW - 20 * k, k, glyph, [label] { return label; }, [value] { return value; });
                node->setPosition({pad + col * columnW, top - (row + 1) * 80 * k});
                body()->addChild(node);
            };
            stat(icon::CHART, "normal mode", fmt::format("{}%", e.level->m_normalPercent.value()));
            stat(icon::BOLT, "practice mode", fmt::format("{}%", e.level->m_practicePercent));
            stat(icon::COINS, "secret coins", fmt::format("{} / {}", e.coinsCollected, e.coins));
            stat(e.platformer ? icon::MOON : icon::STAR, e.platformer ? "moons" : "stars", std::to_string(e.stars));
            stat(icon::ROTATE, "attempts", std::to_string(e.level->m_attempts.value()));
            stat(icon::ARROW_UP, "jumps", std::to_string(e.level->m_jumps.value()));
            if (e.platformer) {
                std::string time = e.bestTime > 0
                    ? fmt::format("{}:{:02}.{:03}", e.bestTime / 60000, e.bestTime / 1000 % 60, e.bestTime % 1000)
                    : "not completed";
                stat(icon::CLOCK, "best time", time);
            }

            float buttonW = 180 * k;
            auto backButton = ButtonRow::create("back", buttonW, k, [this] { close(); });
            backButton->setPosition({pad, 15 * k});
            body()->addChild(backButton);
            addInteractive(backButton);
            auto playButton = ButtonRow::create("play level", buttonW, k, [this] {
                m_playing = true;
                close();
            });
            playButton->setColor(m_scheme.highlight1());
            playButton->setPosition({size.width - pad - buttonW, 15 * k});
            body()->addChild(playButton);
            addInteractive(playButton);
            for (auto [button, name] : {std::pair{backButton, icon::CHEVRON_LEFT}, std::pair{playButton, icon::PLAY}}) {
                auto glyph = makeIcon(name, 14 * k);
                anchorOnGlyph(glyph);
                glyph->setPosition({24 * k, 21 * k});
                button->addChild(glyph, 1);
            }
            return true;
        }

        // Closed: the scene goes, and play (if that's what closed it) runs on
        // song select once it's back.
        void onClosed() override {
            auto action = m_playing ? m_play : std::function<void()>{};
            Ref<CCScene> scene = CCDirector::get()->getRunningScene();
            Loader::get()->queueInMainThread([scene, action] {
                if (CCDirector::get()->getRunningScene() != scene.data()) return;
                CCDirector::get()->popScene();
                if (action) action();
            });
        }

        std::function<void()> m_play;
        bool m_playing = false;
    };

    // The page's scene: Escape / Android's back close the page.
    class OfficialSceneLayer : public CCLayer {
    public:
        OfficialPage* page = nullptr;
        void keyBackClicked() override {
            if (page) page->back();
        }
    };
}

void showOfficialLevelPage(levels::Entry const& entry, std::function<void()> play) {
    auto page = OfficialPage::create(entry, std::move(play));
    if (!page) return;
    auto layer = new OfficialSceneLayer();
    if (!layer->init()) {
        delete layer;
        return;
    }
    layer->autorelease();
    layer->setKeypadEnabled(true);
    layer->page = page;
    layer->addChild(CCLayerColor::create({15, 18, 23, 255}));
    layer->addChild(page, 1);
    auto scene = CCScene::create();
    scene->addChild(layer);
    page->open();
    CCDirector::get()->pushScene(scene);
}

} // namespace lazer
