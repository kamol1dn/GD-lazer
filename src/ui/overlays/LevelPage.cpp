#include "LevelPage.hpp"

#include "../../audio/Sfx.hpp"
#include "../../integrations/LevelThumbnails.hpp"
#include "../../levels/LevelLibrary.hpp"
#include "../core/Text.hpp"
#include "CommentsOverlay.hpp"
#include "LevelFacts.hpp"
#include "LevelPageInternal.hpp"
#include "../select/SongSelect.hpp"

#include <Geode/Geode.hpp>
#include <algorithm>
#include <cmath>

using namespace geode::prelude;

namespace lazer {

using namespace levelpage;

namespace {
    constexpr float BUTTON_HEIGHT = 36.f;
    constexpr float BUTTON_SPACING = 8.f;
    constexpr float BUTTON_TRANSITION = 150.f;
    constexpr float COPIED_MS = 1500.f;
    constexpr float SHIMMER_SPEED = 2.6f;
    constexpr float SPIN_SPEED = 300.f;
    // The picture, dimmed, behind the whole band.
    constexpr ccColor4B COVER_DIM {110, 112, 120, 255};
    constexpr ccColor4B WHITE {255, 255, 255, 255};
    constexpr ccColor4B CLEAR {0, 0, 0, 0};
    constexpr ccColor4B GHOST {255, 255, 255, 28};
    constexpr ccColor4B GHOST_HOVER {255, 255, 255, 56};
    // osu!'s favourite button, once it's hearted.
    constexpr ccColor4B HEARTED {0xff, 0x66, 0xaa, 255};

    constexpr ccColor3B COIN_GOLD {255, 200, 60};
    constexpr ccColor3B COIN_SILVER {200, 205, 215};
    constexpr ccColor3B COIN_BRONZE {190, 130, 90};
}

bool LevelPage::wants(GJGameLevel* level, bool challenge) {
    if (!level || !Mod::get()->getSettingValue<bool>("enabled")) return false;
    // The daily, weekly and event pages have their own furniture (the timer,
    // the reward): GD keeps those.
    if (challenge) return false;
    return level->m_levelID.value() > 0;
}

LevelPage* LevelPage::create(LevelInfoLayer* owner) {
    auto ret = new LevelPage();
    if (ret->init(owner)) {
        ret->autorelease();
        return ret;
    }
    delete ret;
    return nullptr;
}

LevelPage::~LevelPage() {
    auto glm = GameLevelManager::sharedState();
    if (glm->m_leaderboardManagerDelegate == this) glm->m_leaderboardManagerDelegate = nullptr;
}

bool LevelPage::init(LevelInfoLayer* owner) {
    m_owner = owner;
    m_level = owner->m_level;
    std::string name = m_level->m_levelName;
    if (name.empty()) name = "unnamed";
    std::string creator = m_level->m_creatorName;
    if (creator.empty()) creator = "unknown";
    if (!WaveOverlay::init(0, SCHEME, icon::CUBE, name, "by " + creator)) return false;
    m_alive = std::make_shared<char>(0);
    m_pad = HORIZONTAL_PADDING * m_k;

    m_scroll = ScrollArea::create(bodySize());
    body()->addChild(m_scroll);
    m_hero = CCNode::create();
    m_scroll->content()->addChild(m_hero, 1);
    m_sections = CCNode::create();
    m_scroll->content()->addChild(m_sections, 1);

    buildHero();
    rebuildSections();
    updateActions();
    requestThumbnail();
    return true;
}

// --- the top band ---

void LevelPage::buildHero() {
    float k = m_k, W = bodySize().width;
    auto level = m_level.data();
    float top = HERO_PADDING * k;
    float thumbH = THUMB_HEIGHT * k, thumbW = thumbH * 16.f / 9.f;
    float x = m_pad + thumbW + HERO_GAP * k;
    float right = W - m_pad;

    // The picture, sharp, at the left: shimmering until it comes.
    m_thumb = RoundedBox::create({thumbW, thumbH}, THUMB_RADIUS * k, m_scheme.background3());
    m_thumb->setAnchorPoint({0, 1});
    m_thumb->setPosition({m_pad, -top});
    m_thumb->setGradient(m_scheme.background4(), 0.4f, 0);
    m_thumb->setShadow(12 * k, {0, 0, 0, 90});
    m_hero->addChild(m_thumb, 2);
    m_thumbFallback = makeIcon(icon::IMAGE, 34 * k);
    m_thumbFallback->setColor(theme::rgb(m_scheme.foreground1()));
    anchorOnGlyph(m_thumbFallback);
    m_thumbFallback->setPosition({m_pad + thumbW / 2, -(top + thumbH / 2)});
    m_thumbFallback->setVisible(false);
    m_hero->addChild(m_thumbFallback, 3);

    // The rating as a pill, and what the level is.
    float y = top + 10 * k;
    float cx = x;
    auto status = facts::levelStatus(level);
    if (status.text) {
        auto statusLabel = makeText(status.text, Weight::Bold, 11 * k);
        statusLabel->setColor(theme::rgb(m_scheme.background6()));
        float pw = statusLabel->getScaledContentSize().width + 14 * k, ph = 20 * k;
        auto pill = RoundedBox::create({pw, ph}, ph / 2, status.color);
        pill->setAnchorPoint({0, 0.5f});
        pill->setPosition({cx, -y});
        m_hero->addChild(pill, 3);
        statusLabel->setPosition({pw / 2, ph / 2});
        pill->addChild(statusLabel);
        cx += pw + 10 * k;
    }
    std::vector<facts::Stat> kind;
    if (level->isPlatformer()) kind.push_back({icon::RUNNING, "platformer"});
    else kind.push_back({icon::CLOCK, facts::lengthName(level)});
    if (level->m_twoPlayerMode) kind.push_back({icon::USERS, "two player"});
    if (level->m_gauntletLevel) kind.push_back({icon::DUNGEON, "gauntlet"});
    int original = level->m_originalLevel.value();
    if (original > 0 && original != level->m_levelID.value()) kind.push_back({icon::LINK, fmt::format("copy of {}", original)});
    auto kindRow = facts::statsRow(kind, 12 * k, theme::rgb(m_scheme.light2()), theme::rgb(m_scheme.content2()));
    kindRow->setPosition({cx, -y});
    m_hero->addChild(kindRow, 3);

    // The difficulty face with its stars, the coins, then the song.
    y = top + 52 * k;
    float faceSize = 44 * k;
    auto face = facts::difficultyFace(facts::difficultyFrame(level), facts::featureState(level), faceSize);
    face->setPosition({x + faceSize / 2, -y});
    m_hero->addChild(face, 3);
    cx = x + faceSize + 10 * k;
    if (level->m_stars.value() > 0) {
        auto starIcon = makeIcon(level->isPlatformer() ? icon::MOON : icon::STAR, 16 * k);
        starIcon->setAnchorPoint({0, 0.5f});
        starIcon->setPosition({cx, -y});
        m_hero->addChild(starIcon, 3);
        cx += starIcon->getScaledContentSize().width + 5 * k;
        auto stars = makeText(std::to_string(level->m_stars.value()), Weight::Bold, 22 * k);
        stars->setAnchorPoint({0, 0.5f});
        stars->setPosition({cx, -y});
        m_hero->addChild(stars, 3);
        cx += stars->getScaledContentSize().width + 14 * k;
    } else if (level->m_starsRequested > 0) {
        auto asked = makeText(fmt::format("asks for {}", level->m_starsRequested), Weight::Regular, 13 * k);
        asked->setColor(theme::rgb(m_scheme.foreground1()));
        asked->setAnchorPoint({0, 0.5f});
        asked->setPosition({cx, -y});
        m_hero->addChild(asked, 3);
        cx += asked->getScaledContentSize().width + 14 * k;
    }
    int coins = std::clamp(level->m_coins, 0, 3);
    if (coins > 0) {
        // Gold once you have them; silver while they only count for a rated
        // level; bronze otherwise (GD's own coin colours).
        auto entry = levels::fromLevel(level, false);
        levels::resolve(entry);
        for (int i = 0; i < coins; i++) {
            auto coin = makeIcon(icon::COINS, 15 * k);
            bool have = i < entry.coinsCollected;
            coin->setColor(have ? COIN_GOLD : entry.coinsVerified ? COIN_SILVER : COIN_BRONZE);
            coin->setAnchorPoint({0, 0.5f});
            coin->setPosition({cx, -y});
            m_hero->addChild(coin, 3);
            cx += coin->getScaledContentSize().width + 4 * k;
        }
    }

    // Downloads and likes (osu!'s plays and favourites), objects, the version.
    y = top + 92 * k;
    std::vector<facts::Stat> stats {
        {icon::CLOUD_DOWN, facts::metric(level->m_downloads)},
        {icon::THUMBS_UP, facts::metric(level->m_likes)},
    };
    int objects = level->m_objectCount.value();
    if (objects > 0) stats.push_back({icon::CUBE, facts::withCommas(objects)});
    if (level->m_levelVersion > 1) stats.push_back({icon::ROTATE, fmt::format("v{}", level->m_levelVersion)});
    auto statsNode = facts::statsRow(stats, 13 * k, theme::rgb(m_scheme.light2()), theme::rgb(m_scheme.content2()));
    statsNode->setPosition({x, -y});
    m_hero->addChild(statsNode, 3);
    // The numbers that a like changes.
    if (auto downloads = statsNode->getChildByType<CCLabelBMFont>(1)) m_downloadsLabel = downloads;
    if (auto likes = statsNode->getChildByType<CCLabelBMFont>(3)) m_likesLabel = likes;

    // What you can do: play (GD downloads the level first if it must), heart,
    // like, comments, a list, the ID, GD's own page, and the song when it
    // isn't downloaded. They flow onto more lines on narrow screens.
    float bh = BUTTON_HEIGHT * k;
    m_heroButtonsX = x;
    m_heroButtonsY = top + thumbH - bh / 2;
    auto& play = addButton(m_heroButtons, m_hero, Button::Kind::Filled, icon::PLAY, "play", bh, {0, 0}, {0, 0.5f},
                           [this] { this->play(); });
    m_playIcon = play.tinted.size() > 1 ? play.tinted[0] : nullptr;
    m_playLabel = play.tinted.back();
    m_playButton = m_heroButtons.size() - 1;
    auto& heart = addButton(m_heroButtons, m_hero, Button::Kind::Ghost, icon::HEART, "heart", bh, {0, 0}, {0, 0.5f}, [this] {
        if (m_leaving) return;
        // GD's heart (there once it has the level); failing that, the flag it flips.
        if (!pressGD("other-menu", "favorite-button")) m_level->m_levelFavorited = !m_level->m_levelFavorited;
        this->updateActions();
    });
    heart.activeColor = HEARTED;
    heart.active = [this] { return m_level->m_levelFavorited; };
    m_heartIcon = heart.tinted.size() > 1 ? heart.tinted[0] : nullptr;
    m_heartLabel = heart.tinted.back();
    m_heartButton = m_heroButtons.size() - 1;
    addButton(m_heroButtons, m_hero, Button::Kind::Ghost, icon::THUMBS_UP, "like", bh, {0, 0}, {0, 0.5f}, [this] {
        if (m_leaving || pressGD("left-side-menu", "like-button")) return;
        // GD's own like popup, told to tell its page.
        auto layer = LikeItemLayer::create(LikeItemType::Level, m_level->m_levelID.value(), 0);
        if (!layer) return;
        layer->m_likeDelegate = m_owner;
        layer->show();
    });
    addButton(m_heroButtons, m_hero, Button::Kind::Ghost, icon::COMMENTS, "comments", bh, {0, 0}, {0, 0.5f}, [this] {
        if (!m_leaving) CommentsOverlay::present(m_level);
    });
    addButton(m_heroButtons, m_hero, Button::Kind::Ghost, icon::SQUARE_PLUS, "add to list", bh, {0, 0}, {0, 0.5f}, [this] {
        if (!m_leaving) pressGD("other-menu", "list-button");
    });
    m_listButton = m_heroButtons.size() - 1;
    int id = level->m_levelID.value();
    auto& copy = addButton(m_heroButtons, m_hero, Button::Kind::Ghost, icon::COPY, fmt::format("ID {}", id), bh, {0, 0}, {0, 0.5f},
                           [this, id] {
        utils::clipboard::write(std::to_string(id));
        m_copiedMs = COPIED_MS;
        m_idLabel->setString("copied");
    });
    m_idLabel = copy.tinted.back();
    addButton(m_heroButtons, m_hero, Button::Kind::Ghost, icon::CIRCLE_INFO, "GD's page", bh, {0, 0}, {0, 0.5f},
              [this] { this->showVanilla(); });
    addButton(m_heroButtons, m_hero, Button::Kind::Ghost, icon::CLOUD_DOWN, "download song", bh, {0, 0}, {0, 0.5f}, [this] {
        if (m_leaving) return;
        // The song, the way GD's song widget fetches it: its info first if GD
        // doesn't have that yet.
        int songID = m_level->m_songID;
        auto songs = MusicDownloadManager::sharedState();
        if (songs->getSongInfoObject(songID)) songs->downloadSong(songID);
        else songs->getSongInfo(songID, true);
    });
    m_songButton = m_heroButtons.size() - 1;
    float by = layoutHeroButtons();

    // The band's height: the picture, or the buttons if they took more lines.
    float blockH = std::max(thumbH, by + bh / 2 - top);
    m_heroHeight = top + blockH + HERO_PADDING * k;

    // The picture again, dimmed, across the whole band, fading into the page.
    m_cover = RoundedBox::create({W, m_heroHeight}, 0, m_scheme.background4());
    m_cover->setAnchorPoint({0, 1});
    m_cover->setPosition({0, 0});
    m_hero->addChild(m_cover, 0);
    auto fade = CCLayerGradient::create(theme::lerp(m_scheme.background5(), CLEAR, 0.75f), m_scheme.background5());
    fade->setContentSize({W, m_heroHeight});
    fade->setPosition({0, -m_heroHeight});
    m_hero->addChild(fade, 1);
}

// Returns the last line's centre (from the band's top).
float LevelPage::layoutHeroButtons() {
    float k = m_k, right = bodySize().width - m_pad;
    float bh = BUTTON_HEIGHT * k, gap = BUTTON_SPACING * k;
    float bx = m_heroButtonsX, by = m_heroButtonsY;
    for (auto& b : m_heroButtons) {
        if (!b.node->isVisible()) continue;
        float w = b.node->getContentSize().width;
        if (bx > m_heroButtonsX && bx + w > right) {
            bx = m_heroButtonsX;
            by += bh + gap;
        }
        b.node->setPosition({bx, -by});
        bx += w + gap;
    }
    return by;
}

LevelPage::Button& LevelPage::addButton(std::vector<Button>& list, CCNode* parent, Button::Kind kind, char const* glyph,
                                        std::string const& text, float height, CCPoint pos, CCPoint anchor,
                                        std::function<void()> action) {
    float k = m_k;
    float textSize = kind == Button::Kind::Tab ? 12 * k : 13 * k;
    auto label = makeText(text, Weight::SemiBold, textSize);
    CCLabelBMFont* icon = glyph ? makeIcon(glyph, textSize * 0.95f) : nullptr;
    float padX = kind == Button::Kind::Tab ? 12 * k : 16 * k;
    float iconW = icon ? icon->getScaledContentSize().width + 7 * k : 0;
    float w = 2 * padX + iconW + label->getScaledContentSize().width;

    auto node = CCNode::create();
    node->setContentSize({w, height});
    node->setAnchorPoint(anchor);
    node->setPosition(pos);
    parent->addChild(node, 4);
    Button b;
    b.kind = kind;
    b.node = node;
    b.action = std::move(action);
    switch (kind) {
        case Button::Kind::Filled:
            b.color = m_scheme.colour3();
            b.hoverColor = theme::lerp(m_scheme.colour3(), m_scheme.highlight1(), 0.5f);
            break;
        case Button::Kind::Ghost:
            b.color = GHOST;
            b.hoverColor = GHOST_HOVER;
            b.textColor = theme::rgb(m_scheme.content2());
            break;
        case Button::Kind::Tab:
            b.color = CLEAR;
            b.hoverColor = m_scheme.background3();
            b.textColor = theme::rgb(m_scheme.light2());
            break;
    }
    b.activeColor = m_scheme.colour3();
    auto bg = RoundedBox::create({w, height}, height / 2, b.color);
    bg->setPosition({w / 2, height / 2});
    if (kind == Button::Kind::Ghost) bg->setBorder(1.f * k, {255, 255, 255, 50});
    node->addChild(bg);
    b.bg = bg;
    float x = padX;
    if (icon) {
        icon->setAnchorPoint({0, 0.5f});
        icon->setPosition({x, height / 2});
        node->addChild(icon, 1);
        b.tinted.push_back(icon);
        x += iconW;
    }
    label->setAnchorPoint({0, 0.5f});
    label->setPosition({x, height / 2});
    node->addChild(label, 1);
    b.tinted.push_back(label);
    list.push_back(std::move(b));
    m_pressed = nullptr; // the list may have moved
    return list.back();
}

// --- what the actions say ---

void LevelPage::updateActions() {
    if (m_playButton >= m_heroButtons.size()) return;
    bool downloaded = !std::string(m_level->m_levelString).empty();
    std::string caption = "play";
    if (!downloaded) {
        if (m_downloadFailed) caption = "download failed, try again";
        else if (m_playWhenReady) caption = "downloading, then playing...";
        else caption = "downloading...";
    }
    if (caption != m_playLabel->getString()) {
        m_playLabel->setString(caption.c_str());
        // The button grows with its caption.
        auto& play = m_heroButtons[m_playButton];
        float k = m_k, padX = 16 * k;
        float iconW = m_playIcon ? m_playIcon->getScaledContentSize().width + 7 * k : 0;
        float w = 2 * padX + iconW + m_playLabel->getScaledContentSize().width;
        play.node->setContentSize({w, play.node->getContentSize().height});
        play.bg->setContentSize({w, play.bg->getContentSize().height});
        play.bg->setPositionX(w / 2);
        layoutHeroButtons();
    }
    if (m_playIcon) m_playIcon->setString(downloaded ? icon::PLAY : m_downloadFailed ? icon::TRIANGLE_EXCLAMATION : icon::CLOUD_DOWN);
    if (m_heartLabel) m_heartLabel->setString(m_level->m_levelFavorited ? "hearted" : "heart");
    if (m_likesLabel) m_likesLabel->setString(facts::metric(m_level->m_likes).c_str());
    if (m_downloadsLabel) m_downloadsLabel->setString(facts::metric(m_level->m_downloads).c_str());

    // Adding to a list needs GD's own button, which its page shows once it has the level.
    if (m_listButton < m_heroButtons.size()) {
        auto& list = m_heroButtons[m_listButton];
        bool can = hasGD("other-menu", "list-button");
        if (list.node->isVisible() != can) {
            list.node->setVisible(can);
            layoutHeroButtons();
        }
    }
    // The song button only while there's a song to fetch.
    if (m_songButton < m_heroButtons.size()) {
        auto& song = m_heroButtons[m_songButton];
        int songID = m_level->m_songID;
        auto songs = MusicDownloadManager::sharedState();
        bool needed = songID > 0 && !songs->isSongDownloaded(songID);
        if (song.node->isVisible() != needed) {
            song.node->setVisible(needed);
            layoutHeroButtons();
        }
        if (needed) {
            int percent = songs->getDownloadProgress(songID);
            std::string text = percent > 0 && percent < 100 ? fmt::format("song {}%", percent) : "download song";
            auto label = song.tinted.back();
            if (text != label->getString()) label->setString(text.c_str());
        }
    }
}

void LevelPage::play() {
    if (m_leaving || !m_owner) return;
    if (m_owner->getChildByID("level-page-loader"_spr)) return;
    if (std::string(m_level->m_levelString).empty()) {
        // GD is fetching it (it started as its page was built): play once it lands.
        m_playWhenReady = true;
        if (m_downloadFailed) {
            m_downloadFailed = false;
            m_owner->downloadLevel();
        }
        updateActions();
        return;
    }
    Ref<LevelPage> self = this;
    if (auto loader = SongSelect::pageLoader(m_level, [self] {
        if (self->m_leaving || !self->isRunning()) return;
        if (!self->pressGD("play-menu", "play-button")) self->m_owner->onPlay(nullptr);
    })) {
        loader->setID("level-page-loader"_spr);
        // The overlay's body is transformed while opening; the loader needs
        // the scene's full-screen coordinates.
        m_owner->addChild(loader, 200);
    } else {
        if (!pressGD("play-menu", "play-button")) m_owner->onPlay(nullptr);
    }
}

bool LevelPage::hasGD(char const* menuID, char const* buttonID) const {
    auto menu = m_owner ? m_owner->getChildByID(menuID) : nullptr;
    return menu && menu->getChildByID(buttonID);
}

bool LevelPage::pressGD(char const* menuID, char const* buttonID) {
    auto menu = m_owner ? m_owner->getChildByID(menuID) : nullptr;
    auto item = menu ? typeinfo_cast<CCMenuItem*>(menu->getChildByID(buttonID)) : nullptr;
    if (!item || !item->isEnabled()) return false;
    // A press, as GD's menu would deliver it (the item itself as the sender).
    item->activate();
    return true;
}

void LevelPage::levelChanged() {
    if (m_leaving) return;
    // GD's page swaps its level for the downloaded one (the search result
    // had no level data): follow it.
    if (m_owner->m_level) m_level = m_owner->m_level;
    m_downloadFailed = false;
    updateActions();
    rebuildSections();
    if (m_playWhenReady && !std::string(m_level->m_levelString).empty()) {
        m_playWhenReady = false;
        play();
    }
}

void LevelPage::downloadFailed() {
    m_downloadFailed = true;
    m_playWhenReady = false;
    updateActions();
}

void LevelPage::goBack() {
    if (m_leaving) return;
    // GD's back does nothing while a scene fades (the pop is refused) or while
    // it starts the level: leaving then left this page dead, deaf to every
    // press after (#52). A closed page tries again next frame.
    if (CCDirector::get()->getIsTransitioning() || m_owner->m_isBusy) return;
    m_leaving = true;
    if (!m_vanilla) sfx::play(sfx::sound::WAVE_POP_OUT);
    m_owner->onBack(nullptr);
}

void LevelPage::showVanilla() {
    if (m_vanilla || m_leaving) return;
    m_vanilla = true;
    // GD's page, with everything it has, in front; this one waits underneath
    // in case GD's back button is ours (it isn't: GD's goes where it goes).
    this->setVisible(false);
    g_overlayOpen = false;
    // (Not its loading circle's touches: enabled while it isn't shown, the
    // circle would swallow every touch on the page.)
    for (auto child : CCArrayExt<CCNode*>(m_owner->getChildren())) {
        bool ours = child == this || child->getID() == "level-page-backdrop"_spr;
        child->setVisible(!ours);
    }
}

// --- the picture ---

void LevelPage::requestThumbnail() {
    std::weak_ptr<char> alive = m_alive;
    thumbnails::fetch(m_level->m_levelID.value(), [alive, this](CCTexture2D* texture) {
        if (alive.expired()) return;
        m_thumbShimmer = false;
        m_thumb->clearGradient();
        if (!texture) {
            m_thumb->setFillColor(m_scheme.background3());
            m_thumbFallback->setVisible(true);
            return;
        }
        m_thumb->setTexture(texture);
        m_thumb->setFillColor(WHITE);
        m_cover->setTexture(texture);
        m_cover->setFillColor(COVER_DIM);
    });
}

// --- per frame and input ---

bool LevelPage::covered() const {
    if (popupOnTop()) return true;
    auto scene = CCDirector::get()->getRunningScene();
    if (!scene) return false;
    for (auto child : CCArrayExt<CCNode*>(scene->getChildren())) {
        if (!child->isVisible() || child == m_owner) continue;
        // Our other pages (comments, a profile) open over the scene; so does
        // GD's list picker.
        if (typeinfo_cast<WaveOverlay*>(child) || typeinfo_cast<LevelBrowserLayer*>(child)) return true;
    }
    return false;
}

void LevelPage::updateButton(Button& b, bool hovered, float dt) {
    if (hovered && !b.hovered) sfx::hover(sfx::sound::DEFAULT_HOVER);
    b.hovered = hovered;
    bool active = b.active && b.active();
    float target = hovered ? 1.f : 0.f;
    if (b.hover.target() != target) b.hover.to(target, BUTTON_TRANSITION, Easing::OutQuint);
    b.hover.update(dt);
    float t = b.hover.get();
    auto base = active ? b.activeColor : b.color;
    auto lit = active ? theme::lerp(b.activeColor, WHITE, 0.15f) : b.hoverColor;
    b.bg->setFillColor(theme::lerp(base, lit, t));
    auto mix = [t](ccColor3B a, ccColor3B c) {
        auto m = [t](GLubyte x, GLubyte y) { return static_cast<GLubyte>(x + (y - x) * t); };
        return ccColor3B {m(a.r, c.r), m(a.g, c.g), m(a.b, c.b)};
    };
    auto text = active ? b.textActive : mix(b.textColor, b.textHover);
    for (auto label : b.tinted) label->setColor(text);
}

void LevelPage::onUpdate(float dt) {
    if (m_leaving || m_vanilla) return;
    // The close button: leave right away (the waves keep dropping during the fade).
    if (!isOpen()) return goBack();
    float ms = dt * 1000.f;

    if (m_copiedMs > 0) {
        m_copiedMs -= ms;
        if (m_copiedMs <= 0) m_idLabel->setString(fmt::format("ID {}", m_level->m_levelID.value()).c_str());
    }
    m_shimmerPhase += dt * SHIMMER_SPEED;
    if (m_thumbShimmer) m_thumb->setGradient(m_scheme.background4(), 0.4f, m_shimmerPhase);
    if (m_spinner) m_spinner->setRotation(m_spinner->getRotation() + dt * SPIN_SPEED);
    // The song's download progress, and the level's.
    updateActions();

    auto mouse = geode::cocos::getMousePos();
    bool interactive = isOpen() && !covered() && !m_drag.dragging() && m_scroll->containsWorldPoint(mouse);
    for (auto list : {&m_heroButtons, &m_sectionButtons}) {
        for (auto& b : *list) {
            bool hovered = interactive && b.action && b.enabled && nodeShown(b.node) && nodeContains(b.node, mouse);
            updateButton(b, hovered, dt);
        }
    }
}

LevelPage::Button* LevelPage::buttonAt(CCPoint world) {
    if (!m_scroll->containsWorldPoint(world)) return nullptr;
    Button* hit = nullptr;
    for (auto list : {&m_heroButtons, &m_sectionButtons}) {
        for (auto& b : *list) {
            if (b.action && b.enabled && nodeShown(b.node) && nodeContains(b.node, world)) hit = &b;
        }
    }
    return hit;
}

bool LevelPage::ccTouchBegan(CCTouch* touch, CCEvent* e) {
    if (m_leaving || m_vanilla || covered()) return false;
    if (!WaveOverlay::ccTouchBegan(touch, e)) return false;
    auto loc = touch->getLocation();
    m_pressed = buttonAt(loc);
    m_drag.began(m_scroll, loc);
    return true;
}

void LevelPage::ccTouchMoved(CCTouch* touch, CCEvent*) {
    if (m_drag.moved(touch->getLocation())) m_pressed = nullptr;
}

void LevelPage::ccTouchEnded(CCTouch* touch, CCEvent* e) {
    WaveOverlay::ccTouchEnded(touch, e);
    m_drag.ended();
    auto pressed = m_pressed;
    m_pressed = nullptr;
    if (!pressed || !pressed->action || !pressed->enabled || !nodeContains(pressed->node, touch->getLocation())) return;
    sfx::click(pressed->kind == Button::Kind::Filled ? sfx::sound::BUTTON_SELECT : sfx::sound::DEFAULT_SELECT);
    auto action = pressed->action; // may rebuild the list, and the button with it
    action();
}

} // namespace lazer
