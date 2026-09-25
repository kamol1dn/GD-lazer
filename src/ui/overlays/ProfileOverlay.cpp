#include "ProfileOverlay.hpp"

#include "../../audio/Sfx.hpp"
#include "../../integrations/ModIntegrations.hpp"
#include "../core/Text.hpp"

#include <Geode/modify/ProfilePage.hpp>
#include <Geode/utils/web.hpp>
#include <algorithm>
#include <array>
#include <cmath>

using namespace geode::prelude;

// GD's ProfilePage, rerouted: show() hands it to our overlay, which keeps it
// in the scene but invisible and deaf to input, and reads its data.
class $modify(LazerProfilePage, ProfilePage) {
    struct Fields {
        Ref<CCArray> comments;
        int commentsVersion = 0;
    };

    bool hidden() { return this->getUserObject("hidden"_spr) != nullptr; }

    void show() {
        auto mod = Mod::get();
        if (!mod->getSettingValue<bool>("enabled") || !mod->getSettingValue<bool>("restyle-profiles")) {
            return ProfilePage::show();
        }
        lazer::ProfileOverlay::present(this);
    }

    void registerWithTouchDispatcher() {
        if (hidden()) return;
        ProfilePage::registerWithTouchDispatcher();
    }

#ifdef GEODE_IS_ANDROID
    // Android's keyBackClicked is just onClose(nullptr): too small to hook (the
    // hook's patch spills into the next function), so guard onClose instead.
    void onClose(CCObject* sender) {
        if (hidden()) return;
        ProfilePage::onClose(sender);
    }
#else
    void keyBackClicked() {
        if (hidden()) return;
        ProfilePage::keyBackClicked();
    }
#endif

    void loadCommentsFinished(CCArray* comments, char const* key) {
        ProfilePage::loadCommentsFinished(comments, key);
        m_fields->comments = comments;
        m_fields->commentsVersion++;
    }

    void loadCommentsFailed(char const* key) {
        ProfilePage::loadCommentsFailed(key);
        m_fields->comments = nullptr;
        m_fields->commentsVersion++;
    }
};

namespace lazer {

namespace {
    constexpr float HORIZONTAL_PADDING = 50.f;
    constexpr float COVER_HEIGHT = 104.f;
    constexpr float AVATAR = 96.f;
    constexpr ccColor4B DANGER {204, 51, 85, 255};

    LazerProfilePage* fields(ProfilePage* page) { return static_cast<LazerProfilePage*>(page); }

    bool nodeContains(CCNode* node, CCPoint world) {
        auto local = node->convertToNodeSpace(world);
        auto size = node->getContentSize();
        return local.x >= 0 && local.y >= 0 && local.x <= size.width && local.y <= size.height;
    }

    std::string withCommas(long long v) {
        auto s = fmt::format("{}", v);
        for (int i = int(s.size()) - 3; i > (s[0] == '-' ? 1 : 0); i -= 3) s.insert(size_t(i), ",");
        return s;
    }

    ccColor4B darken(ccColor3B c, float f) {
        return {GLubyte(c.r * f), GLubyte(c.g * f), GLubyte(c.b * f), 255};
    }

    // Hue of a colour, for the overlay's colour scheme.
    float hueOf(ccColor3B c) {
        float r = c.r / 255.f, g = c.g / 255.f, b = c.b / 255.f;
        float mx = std::max({r, g, b}), mn = std::min({r, g, b});
        if (mx - mn < 0.08f) return 255.f; // greyish: osu!'s default purple
        float h;
        if (mx == r) h = std::fmod((g - b) / (mx - mn), 6.f);
        else if (mx == g) h = (b - r) / (mx - mn) + 2.f;
        else h = (r - g) / (mx - mn) + 4.f;
        h *= 60.f;
        return h < 0 ? h + 360.f : h;
    }

    struct Colors { int c1, c2, glowColor; bool glow; };

    // One of a player's icons in their colours.
    CCNode* playerIcon(IconType type, int id, Colors c, float size) {
        auto gm = GameManager::get();
        auto player = SimplePlayer::create(1);
        player->updatePlayerFrame(std::max(1, id), type);
        player->setColors(gm->colorForIdx(c.c1), gm->colorForIdx(c.c2));
        if (c.glow) player->setGlowOutline(gm->colorForIdx(c.glowColor));
        else player->disableGlowOutline();
        player->setScale(size / 30.f);
        return player;
    }

    Colors colorsOf(GJUserScore* s) { return {s->m_color1, s->m_color2, s->m_color3, s->m_glowEnabled}; }

    std::vector<int> parseInts(std::string const& s) {
        std::vector<int> out;
        for (auto part : utils::string::split(s, ",")) out.push_back(utils::numFromString<int>(part).unwrapOr(0));
        return out;
    }
}

void ProfileOverlay::present(ProfilePage* page) {
    auto scene = CCDirector::sharedDirector()->getRunningScene();
    if (!scene || !page) return;

    // The overlay takes the player's colour once it's known; start from their
    // own colours on their own profile.
    float hue = 255.f;
    if (page->m_ownProfile) hue = hueOf(GameManager::get()->colorForIdx(GameManager::get()->getPlayerColor()));

    auto overlay = new ProfileOverlay();
    if (!overlay->init(page, theme::Scheme {hue})) {
        delete overlay;
        return;
    }
    overlay->autorelease();
    // Under GD's own popups (FLAlertLayer uses z 105), over everything else.
    scene->addChild(overlay, 100);
    overlay->open();
}

bool ProfileOverlay::init(ProfilePage* page, theme::Scheme scheme) {
    if (!WaveOverlay::init(0, scheme, icon::USER, "player info", "stats, icons and posts", 72.f)) return false;
    m_page = page;
    m_pad = HORIZONTAL_PADDING * m_k;

    // GD's page: in the scene (so it loads and its popups work), never drawn or touched.
    page->setUserObject("hidden"_spr, CCBool::create(true));
    page->setTouchEnabled(false);
    page->setKeypadEnabled(false);
    page->setOpacity(0);
    if (page->m_mainLayer) page->m_mainLayer->setVisible(false);
    this->addChild(page, -10);

    m_scroll = ScrollArea::create(bodySize());
    body()->addChild(m_scroll);

    m_status = makeText("loading...", Weight::Regular, 20 * m_k);
    m_status->setColor(theme::rgb(m_scheme.content2()));
    m_status->setPosition(bodySize() / 2);
    body()->addChild(m_status, 2);
    return true;
}

void ProfileOverlay::onEnter() {
    WaveOverlay::onEnter();
    CCDirector::sharedDirector()->getKeypadDispatcher()->addDelegate(this);
}

void ProfileOverlay::onExit() {
    CCDirector::sharedDirector()->getKeypadDispatcher()->removeDelegate(this);
    WaveOverlay::onExit();
}

void ProfileOverlay::keyBackClicked() {
    close();
}

void ProfileOverlay::onClosed() {
    this->removeFromParent();
}

// --- building ---

void ProfileOverlay::rebuild() {
    m_scroll->content()->removeAllChildren();
    m_pills.clear();
    m_pressed = nullptr;
    m_shownScore = m_page->m_score;
    m_shownComments = fields(m_page)->m_fields->commentsVersion;
    m_signature = stateSignature();
    m_status->setVisible(false);

    float y = buildHeader(0);
    y = buildActions(y);
    y = buildStats(y);
    y = buildIcons(y);
    y = buildBreakdown(y);
    y = buildPosts(y);
    m_scroll->setContentHeight(y + 28 * m_k);
    m_scroll->claimWheel();
}

float ProfileOverlay::addSectionTitle(std::string const& title, float y) {
    y += 12 * m_k;
    auto label = makeText(title, Weight::SemiBold, 18 * m_k);
    label->setAnchorPoint({0, 1});
    label->setPosition({m_pad, -y});
    m_scroll->content()->addChild(label);
    return y + label->getScaledContentSize().height + 6 * m_k;
}

float ProfileOverlay::addPill(char const* glyph, std::string const& label, float x, float y,
                              std::function<void()> action, ccColor4B color) {
    float k = m_k, h = 30 * k;
    auto text = makeText(label, Weight::SemiBold, 14 * k);
    auto iconLabel = glyph ? makeIcon(glyph, 13 * k) : nullptr;
    float w = text->getScaledContentSize().width + 26 * k + (iconLabel ? 20 * k : 0);

    auto node = CCNode::create();
    node->setContentSize({w, h});
    node->setAnchorPoint({0, 1});
    node->setPosition({x, -y});
    m_scroll->content()->addChild(node, 1);

    auto bg = RoundedBox::create({w, h}, h / 2, color);
    bg->setPosition({w / 2, h / 2});
    node->addChild(bg);
    float tx = 13 * k;
    if (iconLabel) {
        iconLabel->setPosition({tx + 7 * k, h / 2});
        node->addChild(iconLabel, 1);
        tx += 20 * k;
    }
    text->setAnchorPoint({0, 0.5f});
    text->setPosition({tx, h / 2});
    node->addChild(text, 1);

    m_pills.push_back({node, bg, color, std::move(action)});
    return w;
}

float ProfileOverlay::buildHeader(float y) {
    auto score = m_page->m_score;
    auto gm = GameManager::get();
    auto content = m_scroll->content();
    float k = m_k, W = bodySize().width;
    auto c1 = gm->colorForIdx(score->m_color1), c2 = gm->colorForIdx(score->m_color2);

    // Cover: the player's two colours, dimmed.
    float coverH = COVER_HEIGHT * k;
    auto cover = CCLayerGradient::create(darken(c1, 0.55f), darken(c2, 0.35f), {1, -0.4f});
    cover->setContentSize({W, coverH});
    cover->setPosition({0, -(y + coverH)});
    content->addChild(cover);
    auto shade = CCLayerGradient::create({0, 0, 0, 0}, {0, 0, 0, 150}, {0, -1});
    shade->setContentSize({W, coverH});
    shade->setPosition({0, -(y + coverH)});
    content->addChild(shade);

    // Avatar: their cube, overlapping the cover's bottom edge.
    float av = AVATAR * k;
    CCPoint avatarCenter {m_pad + av / 2, -(y + coverH - av * 0.35f)};
    auto tile = RoundedBox::create({av, av}, 18 * k, m_scheme.background4());
    tile->setShadow(14 * k, {0, 0, 0, 110});
    tile->setPosition(avatarCenter);
    content->addChild(tile, 1);
    auto cube = playerIcon(IconType::Cube, score->m_playerCube, colorsOf(score), 60 * k);
    cube->setPosition(avatarCenter);
    content->addChild(cube, 2);
    // 2P Skins: player 2 on your own profile.
    if (m_page->m_ownProfile) {
        if (auto p2 = integrations::playerIcon(true, 26 * k)) {
            float mini = 40 * k;
            CCPoint at {avatarCenter.x + av / 2 - mini / 4, avatarCenter.y - av / 2 + mini / 4};
            auto p2Tile = RoundedBox::create({mini, mini}, 12 * k, m_scheme.background6());
            p2Tile->setBorder(3 * k, m_scheme.background5());
            p2Tile->setPosition(at);
            content->addChild(p2Tile, 3);
            p2->setPosition(at);
            content->addChild(p2, 4);
        }
    }

    // Name (+ mod badge) on the cover, rank under it.
    float textX = m_pad + av + 20 * k;
    auto name = makeText(score->m_userName, Weight::Bold, 30 * k);
    name->setAnchorPoint({0, 0});
    name->setPosition({textX, -(y + coverH - 6 * k)});
    content->addChild(name, 2);
    if (score->m_modBadge > 0 && score->m_modBadge <= 3) {
        auto frame = fmt::format("modBadge_0{}_001.png", score->m_modBadge);
        if (CCSpriteFrameCache::sharedSpriteFrameCache()->spriteFrameByName(frame.c_str())) {
            auto badge = CCSprite::createWithSpriteFrameName(frame.c_str());
            auto s = badge->getContentSize();
            badge->setScale(26 * k / std::max(s.width, s.height));
            badge->setPosition({textX + name->getScaledContentSize().width + 22 * k,
                                -(y + coverH - 6 * k) + name->getScaledContentSize().height / 2});
            content->addChild(badge, 2);
        }
    }

    float infoY = y + coverH + 8 * k;
    std::string rank = score->m_globalRank > 0 ? fmt::format("#{}", withCommas(score->m_globalRank)) : "unranked";
    auto rankLabel = makeText(rank, Weight::SemiBold, 19 * k);
    rankLabel->setColor(theme::rgb(m_scheme.highlight1()));
    rankLabel->setAnchorPoint({0, 1});
    rankLabel->setPosition({textX, -infoY});
    content->addChild(rankLabel, 2);
    auto rankCaption = makeText("global rank", Weight::Regular, 13 * k);
    rankCaption->setColor(theme::rgb(m_scheme.content2()));
    rankCaption->setAnchorPoint({0, 1});
    rankCaption->setPosition({textX + rankLabel->getScaledContentSize().width + 8 * k, -(infoY + 4 * k)});
    content->addChild(rankCaption, 2);

    // Better Progression: level badge and EXP, at the right.
    if (auto progress = integrations::betterProgression(score)) {
        float barW = 240 * k;
        float right = W - m_pad;
        float rowY = -(infoY + 6 * k);
        float x = right - barW;
        if (auto badge = integrations::progressionBadge(progress->level, 34 * k)) {
            badge->setPosition({x - 26 * k, rowY - 8 * k});
            content->addChild(badge, 2);
        }
        auto level = makeText(fmt::format("level {}", progress->level), Weight::SemiBold, 17 * k);
        level->setAnchorPoint({0, 0.5f});
        level->setPosition({x, rowY});
        content->addChild(level, 2);
        long long into = progress->exp - progress->levelStart;
        long long span = std::max(1LL, progress->levelEnd - progress->levelStart);
        auto exp = makeText(fmt::format("{} / {} exp", withCommas(into), withCommas(span)), Weight::Regular, 13 * k);
        exp->setColor(theme::rgb(m_scheme.content2()));
        exp->setAnchorPoint({1, 0.5f});
        exp->setPosition({right, rowY});
        content->addChild(exp, 2);
        auto track = RoundedBox::create({barW, 6 * k}, 3 * k, m_scheme.background6());
        track->setAnchorPoint({0, 0.5f});
        track->setPosition({x, rowY - 15 * k});
        content->addChild(track, 2);
        float fraction = std::clamp(float(into) / float(span), 0.f, 1.f);
        auto fill = RoundedBox::create({std::max(6 * k, barW * fraction), 6 * k}, 3 * k, m_scheme.highlight1());
        fill->setAnchorPoint({0, 0.5f});
        fill->setPosition({x, rowY - 15 * k});
        content->addChild(fill, 3);
    }

    return std::max(y + coverH + av * 0.65f, infoY + 26 * k);
}

float ProfileOverlay::buildActions(float y) {
    auto page = m_page.data();
    auto score = page->m_score;
    float k = m_k, x = m_pad, gap = 6 * k;
    y += 10 * k;
    auto normal = m_scheme.background4();
    auto accent = m_scheme.colour3();
    auto run = [page](void (ProfilePage::*handler)(CCObject*)) {
        return [page, handler] { (page->*handler)(nullptr); };
    };
    float right = bodySize().width - m_pad, lineH = 30 * k + gap;
    auto pill = [&](char const* glyph, std::string const& label, std::function<void()> action, ccColor4B color) {
        float w = addPill(glyph, label, x, y, std::move(action), color);
        if (x + w > right && x > m_pad) {
            // Didn't fit: move it to the start of the next line.
            y += lineH;
            x = m_pad;
            m_pills.back().node->setPosition({x, -y});
        }
        x += w + gap;
    };

    if (page->m_ownProfile) {
        pill(icon::ENVELOPE, score->m_newMsgCount > 0 ? fmt::format("messages ({})", score->m_newMsgCount) : "messages",
             run(&ProfilePage::onMessages), score->m_newMsgCount > 0 ? accent : normal);
        pill(icon::USERS, "friends", run(&ProfilePage::onFriends), normal);
        pill(icon::USER_CLOCK, score->m_friendReqCount > 0 ? fmt::format("requests ({})", score->m_friendReqCount) : "requests",
             run(&ProfilePage::onRequests), score->m_friendReqCount > 0 ? accent : normal);
        pill(icon::GEAR, "settings", run(&ProfilePage::onSettings), normal);
    } else {
        // m_friendStatus: 0 none, 1 friends, 3 / 4 request sent / received.
        auto friendLabel = score->m_friendStatus == 1 ? "friends" : score->m_friendStatus >= 3 ? "request pending" : "add friend";
        pill(score->m_friendStatus == 1 ? icon::USER_CHECK : icon::USER_PLUS, friendLabel,
             run(&ProfilePage::onFriend), score->m_friendStatus == 0 ? accent : normal);
        pill(icon::ENVELOPE, "message", run(&ProfilePage::onSendMessage), normal);
        bool following = GameLevelManager::sharedState()->isFollowingUser(page->m_accountID);
        pill(following ? icon::BELL_SLASH : icon::BELL, following ? "unfollow" : "follow",
             run(&ProfilePage::onFollow), normal);
    }
    pill(icon::LAYERS, "levels", run(&ProfilePage::onMyLevels), normal);
    pill(icon::LIST, "lists", run(&ProfilePage::onMyLists), normal);
    pill(icon::CLOCK, "comment history", run(&ProfilePage::onCommentHistory), normal);
    pill(icon::COPY, "copy name", run(&ProfilePage::onCopyName), normal);
    pill(icon::ROTATE, "refresh", run(&ProfilePage::onUpdate), normal);
    if (!page->m_ownProfile) pill(icon::BAN, "block", run(&ProfilePage::onBlockUser), DANGER);

    // Their links, opened in the browser.
    struct Social { char const* name; std::string handle; char const* url; };
    std::vector<Social> socials {
        {"youtube", score->m_youtubeURL, "https://www.youtube.com/channel/{}"},
        {"twitter", score->m_twitterURL, "https://twitter.com/{}"},
        {"twitch", score->m_twitchURL, "https://twitch.tv/{}"},
        {"instagram", score->m_instagramURL, "https://instagram.com/{}"},
        {"tiktok", score->m_tiktokURL, "https://tiktok.com/@{}"},
    };
    bool any = false;
    for (auto const& s : socials) any = any || !s.handle.empty();
    if (any || !std::string(score->m_discordUsername).empty()) {
        for (auto const& s : socials) {
            if (s.handle.empty()) continue;
            auto url = fmt::format(fmt::runtime(s.url), s.handle);
            pill(icon::LINK, s.name, [url] { web::openLinkInBrowser(url); }, m_scheme.background5());
        }
        std::string discord = score->m_discordUsername;
        if (!discord.empty()) {
            pill(icon::COPY, fmt::format("discord: {}", discord), [discord] {
                utils::clipboard::write(discord);
                Notification::create("Copied Discord username", NotificationIcon::Success)->show();
            }, m_scheme.background5());
        }
    }
    return y + 30 * k;
}

float ProfileOverlay::buildStats(float y) {
    auto s = m_page->m_score;
    y = addSectionTitle("stats", y);
    struct Stat { char const* sprite; char const* label; int value; };
    std::vector<Stat> stats {
        {"GJ_starsIcon_001.png", "stars", s->m_stars},
        {"GJ_moonsIcon_001.png", "moons", s->m_moons},
        {"GJ_diamondsIcon_001.png", "diamonds", s->m_diamonds},
        {"GJ_coinsIcon_001.png", "secret coins", s->m_secretCoins},
        {"GJ_coinsIcon2_001.png", "user coins", s->m_userCoins},
        {"GJ_demonIcon_001.png", "demons", s->m_demons},
        {"GJ_hammerIcon_001.png", "creator points", s->m_creatorPoints},
    };
    float k = m_k, gap = 8 * k;
    int n = int(stats.size());
    float w = (bodySize().width - m_pad * 2 - gap * (n - 1)) / n, h = 52 * k;
    for (int i = 0; i < n; i++) {
        auto box = RoundedBox::create({w, h}, 8 * k, m_scheme.background4());
        box->setAnchorPoint({0, 1});
        box->setPosition({m_pad + i * (w + gap), -y});
        m_scroll->content()->addChild(box);
        float textX = 12 * k;
        if (auto icon = CCSprite::createWithSpriteFrameName(stats[i].sprite)) {
            auto size = icon->getContentSize();
            icon->setScale(22 * k / std::max(size.width, size.height));
            icon->setPosition({12 * k + 11 * k, h / 2});
            box->addChild(icon);
            textX = 12 * k + 22 * k + 10 * k;
        }
        auto value = makeText(withCommas(stats[i].value), Weight::Bold, 18 * k);
        value->setAnchorPoint({0, 0});
        value->setPosition({textX, h / 2 - 2 * k});
        float vw = value->getScaledContentSize().width, room = w - textX - 8 * k;
        if (vw > room) value->setScale(value->getScale() * room / vw);
        box->addChild(value);
        auto label = makeText(stats[i].label, Weight::Regular, 12 * k);
        label->setColor(theme::rgb(m_scheme.content2()));
        label->setAnchorPoint({0, 1});
        label->setPosition({textX, h / 2 - 2 * k});
        float lw = label->getScaledContentSize().width;
        if (lw > room) label->setScale(label->getScale() * room / lw);
        box->addChild(label);
    }
    return y + h;
}

float ProfileOverlay::buildIcons(float y) {
    auto s = m_page->m_score;
    y = addSectionTitle("icons", y);

    struct Row { char const* caption; std::array<int, 9> ids; Colors colors; };
    std::vector<Row> rows {{"player 1", {s->m_playerCube, s->m_playerShip, s->m_playerBall, s->m_playerUfo,
        s->m_playerWave, s->m_playerRobot, s->m_playerSpider, s->m_playerSwing, s->m_playerJetpack}, colorsOf(s)}};
    // 2P Skins only knows your own player 2.
    if (m_page->m_ownProfile) {
        if (auto p2 = integrations::player2Look()) {
            rows.push_back({"player 2", {p2->cube, p2->ship, p2->ball, p2->ufo, p2->wave, p2->robot, p2->spider,
                p2->swing, p2->jetpack}, {p2->color1, p2->color2, p2->glowColor, p2->glow}});
        }
    }

    constexpr std::array<IconType, 9> TYPES {IconType::Cube, IconType::Ship, IconType::Ball, IconType::Ufo,
        IconType::Wave, IconType::Robot, IconType::Spider, IconType::Swing, IconType::Jetpack};
    constexpr std::array<char const*, 9> NAMES {"cube", "ship", "ball", "ufo", "wave", "robot", "spider", "swing", "jetpack"};
    float k = m_k, gap = 8 * k;
    float w = (bodySize().width - m_pad * 2 - gap * 8) / 9, h = 66 * k;
    for (auto const& row : rows) {
        if (rows.size() > 1) {
            auto caption = makeText(row.caption, Weight::SemiBold, 13 * k);
            caption->setColor(theme::rgb(m_scheme.content2()));
            caption->setAnchorPoint({0, 1});
            caption->setPosition({m_pad, -y});
            m_scroll->content()->addChild(caption);
            y += caption->getScaledContentSize().height + 4 * k;
        }
        for (int i = 0; i < 9; i++) {
            auto box = RoundedBox::create({w, h}, 10 * k, m_scheme.background4());
            box->setAnchorPoint({0, 1});
            box->setPosition({m_pad + i * (w + gap), -y});
            m_scroll->content()->addChild(box);
            auto icon = playerIcon(TYPES[i], row.ids[i], row.colors, h * 0.5f);
            icon->setPosition({w / 2, h / 2 + 7 * k});
            box->addChild(icon);
            auto label = makeText(NAMES[i], Weight::Regular, 11 * k);
            label->setColor(theme::rgb(m_scheme.content2()));
            label->setPosition({w / 2, 9 * k});
            box->addChild(label);
        }
        y += h + 8 * k;
    }
    return y - 8 * k;
}

float ProfileOverlay::buildBreakdown(float y) {
    auto s = m_page->m_score;
    // GD 2.2 sends comma lists: demons = classic easy..extreme, platformer
    // easy..extreme, weekly, gauntlet; stars / moons = auto..insane, daily, gauntlet.
    auto demons = parseInts(s->m_demonInfo);
    auto stars = parseInts(s->m_starsInfo);
    auto moons = parseInts(s->m_platformerInfo);
    if (demons.size() < 12 && stars.size() < 8 && moons.size() < 8) return y;
    y = addSectionTitle("completed", y);

    float k = m_k;
    auto row = [&](std::string const& title, std::vector<std::pair<char const*, int>> const& values) {
        auto heading = makeText(title, Weight::SemiBold, 15 * k);
        heading->setColor(theme::rgb(m_scheme.content2()));
        heading->setAnchorPoint({0, 0.5f});
        heading->setPosition({m_pad, -(y + 16 * k)});
        m_scroll->content()->addChild(heading);
        float x = m_pad + 170 * k;
        for (auto const& [label, value] : values) {
            auto text = makeText(fmt::format("{}  {}", label, withCommas(value)), Weight::Regular, 14 * k);
            float w = text->getScaledContentSize().width + 22 * k;
            auto chip = RoundedBox::create({w, 28 * k}, 14 * k, m_scheme.background4());
            chip->setAnchorPoint({0, 0.5f});
            chip->setPosition({x, -(y + 16 * k)});
            m_scroll->content()->addChild(chip);
            text->setPosition({w / 2, 14 * k});
            chip->addChild(text);
            x += w + 8 * k;
        }
        y += 30 * k;
    };
    if (demons.size() >= 12) {
        row("demons", {{"easy", demons[0]}, {"medium", demons[1]}, {"hard", demons[2]}, {"insane", demons[3]},
                       {"extreme", demons[4]}, {"weekly", demons[10]}, {"gauntlet", demons[11]}});
        row("platformer demons", {{"easy", demons[5]}, {"medium", demons[6]}, {"hard", demons[7]},
                                  {"insane", demons[8]}, {"extreme", demons[9]}});
    }
    if (stars.size() >= 8) {
        row("rated levels", {{"auto", stars[0]}, {"easy", stars[1]}, {"normal", stars[2]}, {"hard", stars[3]},
                             {"harder", stars[4]}, {"insane", stars[5]}, {"daily", stars[6]}, {"gauntlet", stars[7]}});
    }
    if (moons.size() >= 8) {
        row("platformer levels", {{"auto", moons[0]}, {"easy", moons[1]}, {"normal", moons[2]}, {"hard", moons[3]},
                                  {"harder", moons[4]}, {"insane", moons[5]}, {"daily", moons[6]}, {"gauntlet", moons[7]}});
    }
    return y;
}

float ProfileOverlay::buildPosts(float y) {
    auto page = m_page.data();
    float k = m_k, W = bodySize().width;
    y = addSectionTitle("posts", y);
    if (page->m_ownProfile) {
        addPill(icon::PLUS, "new post", W - m_pad - 120 * k, y - 46 * k,
                [page] { page->onComment(nullptr); }, m_scheme.colour3());
    }

    auto comments = fields(page)->m_fields->comments.data();
    if (!comments || comments->count() == 0) {
        auto none = makeText(fields(page)->m_fields->commentsVersion == 0 ? "loading..." : "no posts yet",
                             Weight::Regular, 16 * k);
        none->setColor(theme::rgb(m_scheme.content2()));
        none->setAnchorPoint({0, 1});
        none->setPosition({m_pad, -y});
        m_scroll->content()->addChild(none);
        return y + 30 * k;
    }

    float cardW = W - m_pad * 2;
    for (auto comment : CCArrayExt<GJComment*>(comments)) {
        auto text = makeWrappedText(comment->m_commentString, 16 * k, cardW - 40 * k, theme::CONTENT1);
        float h = text->getContentSize().height + 44 * k;
        auto card = RoundedBox::create({cardW, h}, 10 * k, m_scheme.background4());
        card->setAnchorPoint({0, 1});
        card->setPosition({m_pad, -y});
        m_scroll->content()->addChild(card);
        text->setPosition({18 * k, h - 13 * k});
        card->addChild(text);

        auto likes = makeIcon(icon::THUMBS_UP, 12 * k);
        likes->setColor(comment->m_likeCount < 0 ? ccColor3B {255, 110, 110} : theme::rgb(m_scheme.content2()));
        likes->setPosition({24 * k, 16 * k});
        card->addChild(likes);
        auto meta = makeText(fmt::format("{}   ·   {} ago", withCommas(comment->m_likeCount), std::string(comment->m_uploadDate)),
                             Weight::Regular, 13 * k);
        meta->setColor(theme::rgb(m_scheme.content2()));
        meta->setAnchorPoint({0, 0.5f});
        meta->setPosition({36 * k, 16 * k});
        card->addChild(meta);
        y += h + 8 * k;
    }

    // GD pages posts ten at a time.
    int pages = (page->m_itemCount + 9) / 10;
    if (pages > 1) {
        y += 6 * k;
        float x = m_pad;
        if (page->m_page > 0) x += addPill(icon::CHEVRON_LEFT, "newer", x, y, [page] { page->onPrevPage(nullptr); }, m_scheme.background4()) + 8 * k;
        if (page->m_page + 1 < pages) addPill(icon::CHEVRON_RIGHT, "older", x, y, [page] { page->onNextPage(nullptr); }, m_scheme.background4());
        auto label = makeText(fmt::format("page {} of {}", page->m_page + 1, pages), Weight::Regular, 14 * k);
        label->setColor(theme::rgb(m_scheme.content2()));
        label->setAnchorPoint({1, 0.5f});
        label->setPosition({W - m_pad, -(y + 17 * k)});
        m_scroll->content()->addChild(label);
        y += 34 * k;
    }
    return y;
}

// --- per frame ---

std::string ProfileOverlay::stateSignature() const {
    auto s = m_page->m_score;
    if (!s) return "";
    return fmt::format("{}:{}:{}:{}:{}", s->m_friendStatus, s->m_newMsgCount, s->m_friendReqCount,
                       GameLevelManager::sharedState()->isFollowingUser(m_page->m_accountID), s->m_messageState);
}

void ProfileOverlay::onUpdate(float dt) {
    auto page = m_page.data();
    if (!page) return;

    if (!page->m_score) {
        bool failed = page->m_somethingWentWrong && page->m_somethingWentWrong->isVisible();
        m_status->setString(failed ? "couldn't load this player" : "loading...");
        m_status->setVisible(true);
    } else if (page->m_score != m_shownScore || fields(page)->m_fields->commentsVersion != m_shownComments
               || stateSignature() != m_signature) {
        // New data (first load, refresh, posts page, friend / follow changes).
        float scroll = m_scroll->scroll();
        bool first = m_shownScore == nullptr;
        rebuild();
        if (!first) m_scroll->scrollTo(scroll, false);
    }

    auto mouse = geode::cocos::getMousePos();
    bool interactive = isOpen() && !m_drag.dragging() && m_scroll->containsWorldPoint(mouse);
    for (auto& pill : m_pills) {
        bool hovered = interactive && nodeContains(pill.node, mouse);
        if (hovered && !pill.hovered) sfx::hover(sfx::sound::DEFAULT_HOVER);
        pill.hovered = hovered;
        pill.bg->setFillColor(hovered ? theme::lerp(pill.color, {255, 255, 255, 255}, 0.12f) : pill.color);
    }
}

bool ProfileOverlay::ccTouchBegan(CCTouch* touch, CCEvent* e) {
    if (!WaveOverlay::ccTouchBegan(touch, e)) return false;
    auto loc = touch->getLocation();
    m_pressed = nullptr;
    if (m_scroll->containsWorldPoint(loc)) {
        for (auto& pill : m_pills) {
            if (nodeContains(pill.node, loc)) m_pressed = &pill;
        }
    }
    m_drag.began(m_scroll, loc);
    return true;
}

void ProfileOverlay::ccTouchMoved(CCTouch* touch, CCEvent*) {
    if (m_drag.moved(touch->getLocation())) m_pressed = nullptr;
}

void ProfileOverlay::ccTouchEnded(CCTouch* touch, CCEvent* e) {
    WaveOverlay::ccTouchEnded(touch, e);
    m_drag.ended();
    auto pressed = m_pressed;
    m_pressed = nullptr;
    if (!pressed || !nodeContains(pressed->node, touch->getLocation())) return;
    sfx::click(sfx::sound::DEFAULT_SELECT);
    auto action = pressed->action; // may rebuild the page
    if (action) action();
}

} // namespace lazer
