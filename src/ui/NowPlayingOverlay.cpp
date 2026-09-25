#include "NowPlayingOverlay.hpp"

#include "../audio/Sfx.hpp"
#include "LevelThumbnails.hpp"
#include "Text.hpp"
#include "Theme.hpp"

#include <Geode/Geode.hpp>

using namespace geode::prelude;

namespace lazer {

namespace {
    // Values from NowPlayingOverlay.cs (osu! pixels, 768 tall).
    constexpr float PLAYER_WIDTH = 400;
    constexpr float PLAYER_HEIGHT = 130;
    constexpr float TRANSITION = 500;
    constexpr float PROGRESS_HEIGHT = 10;
    constexpr float BOTTOM_AREA = 55;
    constexpr float MARGIN = 10;
    constexpr float CORNER = 5;
    constexpr float BUTTON_SIZE = 30; // IconButton.DEFAULT_BUTTON_SIZE
    constexpr float ICON_SIZE = 18;
    constexpr float SEEK_DEBOUNCE_MS = 40; // TRACK_DRAG_SEEK_DEBOUNCE

    constexpr ccColor4B YELLOW {0xff, 0xcc, 0x22, 255};
    constexpr ccColor4B YELLOW_DARK {0xee, 0xaa, 0x00, 255};
    constexpr ccColor4B YELLOW_DARKER {0xcc, 0x66, 0x00, 255};
    constexpr ccColor4B BACKGROUND_TINT {150, 150, 150, 255}; // OsuColour.Gray(150)

    GLubyte toByte(float a) { return static_cast<GLubyte>(std::clamp(a, 0.f, 1.f) * 255.f); }

    bool nodeContains(CCNode* node, CCPoint world) {
        auto local = node->convertToNodeSpace(world);
        auto size = node->getContentSize();
        return local.x >= 0 && local.y >= 0 && local.x <= size.width && local.y <= size.height;
    }

    double nowMs() {
        using namespace std::chrono;
        return duration<double, std::milli>(steady_clock::now().time_since_epoch()).count();
    }
}

NowPlayingOverlay* NowPlayingOverlay::create(float toolbarHeight) {
    auto ret = new NowPlayingOverlay();
    if (ret->init(toolbarHeight)) {
        ret->autorelease();
        return ret;
    }
    delete ret;
    return nullptr;
}

bool NowPlayingOverlay::init(float toolbarHeight) {
    if (!CCNode::init()) return false;
    auto win = CCDirector::sharedDirector()->getWinSize();
    this->setContentSize(win);
    m_k = win.height / 768.f;
    m_toolbarHeight = toolbarHeight;
    float k = m_k;
    float w = PLAYER_WIDTH * k, h = PLAYER_HEIGHT * k;

    m_panel = CCNodeRGBA::create();
    m_panel->setCascadeOpacityEnabled(true);
    m_panel->setContentSize({w, h});
    m_panel->setAnchorPoint({0.5f, 0.5f});
    m_panel->setPosition({win.width - MARGIN * k - w / 2, win.height - toolbarHeight - MARGIN * k - h / 2});
    this->addChild(m_panel);

    // Plain card underneath (and the shadow), shown when there's no thumbnail.
    m_base = RoundedBox::create({w, h}, CORNER * k, theme::BACKGROUND4);
    m_base->setShadow(CORNER * k * 2, {0, 0, 0, 90});
    m_base->setPosition({w / 2, h / 2});
    m_panel->addChild(m_base, 0);

    // Dark band behind the buttons.
    float bandH = BOTTOM_AREA * k;
    auto band = RoundedBox::create({w, bandH}, CORNER * k, {0, 0, 0, 128});
    band->setCornerRadii(0, 0, CORNER * k, CORNER * k);
    band->setPosition({w / 2, bandH / 2});
    m_panel->addChild(band, 2);

    m_title = makeText("Nothing to play", Weight::Regular, 25 * k);
    m_title->setAnchorPoint({0.5f, 0.f});
    m_title->setPosition({w / 2, h - 40 * k});
    m_panel->addChild(m_title, 3);

    m_artist = makeText("Nothing to play", Weight::Bold, 15 * k);
    m_artist->setAnchorPoint({0.5f, 1.f});
    m_artist->setPosition({w / 2, h - 45 * k});
    m_panel->addChild(m_artist, 3);
    m_titleScale = m_title->getScale();
    m_artistScale = m_artist->getScale();

    // Buttons: previous / play / next in the middle of the band, shuffle on the left.
    auto& player = MusicPlayer::get();
    float playSize = BUTTON_SIZE * 1.4f;
    float gap = 5 * k;
    float cx = w / 2;
    addButton(icon::STEP_BACKWARD, cx - playSize * k / 2 - gap - BUTTON_SIZE * k / 2, BUTTON_SIZE, 1.f,
              [] { MusicPlayer::get().previous(); });
    m_playButton = m_buttons.size();
    addButton(icon::CIRCLE_PLAY, cx, playSize, 1.4f, [] { MusicPlayer::get().togglePause(); });
    addButton(icon::STEP_FORWARD, cx + playSize * k / 2 + gap + BUTTON_SIZE * k / 2, BUTTON_SIZE, 1.f,
              [] { MusicPlayer::get().next(); });
    m_shuffleButton = m_buttons.size();
    addButton(icon::SHUFFLE, bandH / 2, BUTTON_SIZE, 1.f, [] { MusicPlayer::get().toggleShuffle(); });
    m_buttons[m_shuffleButton].active = player.shuffle();

    // Seek bar along the bottom edge.
    m_progressBg = RoundedBox::create({w, PROGRESS_HEIGHT / 2 * k}, CORNER * k, {YELLOW_DARKER.r, YELLOW_DARKER.g, YELLOW_DARKER.b, 128});
    m_progressBg->setCornerRadii(0, 0, CORNER * k, CORNER * k);
    m_progressBg->setAnchorPoint({0, 0});
    m_panel->addChild(m_progressBg, 4);
    m_progressFill = RoundedBox::create({0, PROGRESS_HEIGHT / 2 * k}, CORNER * k, YELLOW);
    m_progressFill->setAnchorPoint({0, 0});
    m_panel->addChild(m_progressFill, 5);
    m_progressHeight.set(PROGRESS_HEIGHT / 2 * k);

    this->setVisible(false);
    onTrackChanged(player.current(), MusicPlayer::Direction::None);
    this->scheduleUpdate();
    return true;
}

NowPlayingOverlay::Button& NowPlayingOverlay::addButton(char const* glyph, float x, float size, float iconScale,
                                                        std::function<void()> action) {
    float k = m_k;
    float s = size * k;
    float bandCenter = (PROGRESS_HEIGHT + (BOTTOM_AREA - PROGRESS_HEIGHT) / 2) * k;

    auto node = CCNodeRGBA::create();
    node->setCascadeOpacityEnabled(true);
    node->setContentSize({s, s});
    node->setAnchorPoint({0.5f, 0.5f});
    node->setPosition({x, bandCenter});
    m_panel->addChild(node, 3);

    // OsuAnimatedButton: rounded hover box in YellowDark at 60%.
    auto hover = RoundedBox::create({s, s}, 10 * k * size / BUTTON_SIZE, {YELLOW_DARK.r, YELLOW_DARK.g, YELLOW_DARK.b, 153});
    hover->setPosition({s / 2, s / 2});
    hover->setOpacity(0);
    node->addChild(hover);

    auto label = makeIcon(glyph, ICON_SIZE * k * iconScale);
    label->setPosition({s / 2, s / 2});
    node->addChild(label, 1);

    m_buttons.push_back(Button {node, hover, label, s, std::move(action)});
    return m_buttons.back();
}

void NowPlayingOverlay::setIcon(Button& b, char const* glyph) {
    if (std::string_view(b.icon->getString()) != glyph) b.icon->setString(glyph);
}

void NowPlayingOverlay::setText(CCLabelBMFont* label, std::string const& text) {
    label->setString(text.c_str());
    // MarqueeContainer would scroll long names; shrinking them to fit is close enough.
    float base = label == m_title ? m_titleScale : m_artistScale;
    float maxWidth = (PLAYER_WIDTH - 30) * m_k;
    float width = label->getContentSize().width * base;
    label->setScale(width > maxWidth ? base * maxWidth / width : base);
}

void NowPlayingOverlay::onEnter() {
    CCNode::onEnter();
    // Above the menu, below the toolbar's own buttons (-131).
    CCDirector::sharedDirector()->getTouchDispatcher()->addTargetedDelegate(this, -130, true);
    m_listener = MusicPlayer::get().addListener([this](auto track, auto dir) { onTrackChanged(track, dir); });
}

void NowPlayingOverlay::onExit() {
    CCDirector::sharedDirector()->getTouchDispatcher()->removeDelegate(this);
    MusicPlayer::get().removeListener(m_listener);
    CCNode::onExit();
}

void NowPlayingOverlay::open() {
    if (m_open) return;
    m_open = true;
    this->setVisible(true);
    m_alpha.to(1.f, TRANSITION, Easing::OutQuint);
    m_scale.to(1.f, TRANSITION, Easing::OutElasticHalf);
    sfx::play(sfx::sound::OVERLAY_POP_IN);
}

void NowPlayingOverlay::close() {
    if (!m_open) return;
    m_open = false;
    m_alpha.to(0.f, TRANSITION, Easing::OutQuint);
    m_scale.to(0.9f, TRANSITION, Easing::OutQuint);
    sfx::play(sfx::sound::OVERLAY_POP_OUT);
    for (auto& b : m_buttons) b.hovered = false;
    m_pressed = nullptr;
    m_seeking = false;
}

bool NowPlayingOverlay::back() {
    if (!m_open) return false;
    close();
    return true;
}

void NowPlayingOverlay::onTrackChanged(MusicPlayer::Track const* track, MusicPlayer::Direction direction) {
    int generation = ++m_generation;
    if (!track) {
        setText(m_title, "Nothing to play");
        setText(m_artist, "Nothing to play");
        showBackground(nullptr, direction);
        return;
    }
    setText(m_title, track->title);
    setText(m_artist, track->artist.empty() ? "Unknown artist" : track->artist);

    Ref<NowPlayingOverlay> self = this;
    thumbnails::fetchFirst(track->levelIDs(), [self, generation, direction](CCTexture2D* texture, int) {
        if (self->m_generation != generation) return;
        self->showBackground(texture, direction);
    });
}

void NowPlayingOverlay::showBackground(CCTexture2D* texture, MusicPlayer::Direction direction) {
    float w = PLAYER_WIDTH * m_k, h = PLAYER_HEIGHT * m_k;
    if (m_oldBackground) m_oldBackground->removeFromParent();
    m_oldBackground = m_background;
    m_background = nullptr;

    if (texture) {
        m_background = RoundedBox::create({w, h}, CORNER * m_k, BACKGROUND_TINT);
        m_background->setTexture(texture);
        m_background->setPosition({w / 2, h / 2});
        m_panel->addChild(m_background, 1);
    }

    // Slide the new background in from the side we're moving towards.
    float from = direction == MusicPlayer::Direction::Next ? 1.f : direction == MusicPlayer::Direction::Prev ? -1.f : 0.f;
    m_bgShift.set(from);
    m_bgShift.to(0.f, 500, Easing::OutCubic);
    m_oldBgShift.set(0.f);
    if (from != 0.f) {
        m_oldBgShift.to(-from, 500, Easing::OutCubic);
    } else if (m_oldBackground) {
        m_oldBackground->removeFromParent();
        m_oldBackground = nullptr;
    }
}

NowPlayingOverlay::Button* NowPlayingOverlay::buttonAt(CCPoint world) {
    for (auto& b : m_buttons) {
        if (nodeContains(b.node, world)) return &b;
    }
    return nullptr;
}

bool NowPlayingOverlay::inProgressBar(CCPoint world) {
    auto local = m_panel->convertToNodeSpace(world);
    return local.x >= 0 && local.x <= m_panel->getContentSize().width
        && local.y >= 0 && local.y <= PROGRESS_HEIGHT * m_k;
}

float NowPlayingOverlay::progressFraction(CCPoint world) {
    auto local = m_panel->convertToNodeSpace(world);
    return std::clamp(local.x / m_panel->getContentSize().width, 0.f, 1.f);
}

void NowPlayingOverlay::update(float dt) {
    m_alpha.update(dt);
    m_scale.update(dt);
    if (!m_open && m_alpha.get() <= 0.001f) {
        this->setVisible(false);
        return;
    }
    this->setVisible(true);
    m_panel->setOpacity(toByte(m_alpha.get()));
    m_panel->setScale(m_scale.get());

    auto& player = MusicPlayer::get();
    auto mouse = geode::cocos::getMousePos();
    bool interactive = m_open && m_alpha.get() > 0.5f;

    // Backgrounds sliding on track change.
    m_bgShift.update(dt);
    m_oldBgShift.update(dt);
    if (m_background) m_background->setTextureShift(m_bgShift.get());
    if (m_oldBackground) {
        m_oldBackground->setTextureShift(m_oldBgShift.get());
        if (std::abs(m_oldBgShift.get()) >= 0.999f) {
            m_oldBackground->removeFromParent();
            m_oldBackground = nullptr;
        }
    }

    // Buttons.
    setIcon(m_buttons[m_playButton], player.isActive() && !player.isPaused() ? icon::CIRCLE_PAUSE : icon::CIRCLE_PLAY);
    m_buttons[m_shuffleButton].active = player.shuffle();
    auto hoveredButton = interactive && !m_seeking ? buttonAt(mouse) : nullptr;
    for (auto& b : m_buttons) {
        bool hovered = &b == hoveredButton;
        if (hovered != b.hovered) {
            b.hovered = hovered;
            if (hovered) sfx::hover(sfx::sound::DEFAULT_HOVER);
            b.hover.to(hovered ? 1.f : 0.f, 500, Easing::OutQuint);
        }
        b.hover.update(dt);
        b.flash.update(dt);
        b.scale.update(dt);
        b.hoverBg->setOpacity(toByte(std::max(b.hover.get(), b.flash.get())));
        b.hoverBg->setFillColor(theme::lerp(
            {YELLOW_DARK.r, YELLOW_DARK.g, YELLOW_DARK.b, 153}, YELLOW, std::clamp(b.flash.get(), 0.f, 1.f)
        ));
        b.node->setScale(b.scale.get());
        b.icon->setColor(b.active ? theme::rgb(YELLOW) : ccColor3B {255, 255, 255});
    }

    // Seek bar: thickens on hover (HoverableProgressBar), follows the drag while seeking.
    bool overBar = interactive && (m_seeking || inProgressBar(mouse));
    if (overBar != m_progressHovered) {
        m_progressHovered = overBar;
        m_progressHeight.to((overBar ? PROGRESS_HEIGHT : PROGRESS_HEIGHT / 2) * m_k, 500, Easing::OutQuint);
    }
    m_progressHeight.update(dt);
    float w = m_panel->getContentSize().width;
    float barH = m_progressHeight.get();
    unsigned len = player.lengthMs();
    float fraction = m_seeking ? m_seekFraction : (len > 0 ? std::min(1.f, player.positionMs() / float(len)) : 0.f);
    float fillW = w * fraction;
    float r = CORNER * m_k;
    m_progressBg->setContentSize({w, barH});
    m_progressFill->setContentSize({fillW, barH});
    // Round the fill's right end only once it reaches the panel's rounded corner.
    m_progressFill->setCornerRadii(0, 0, r, fillW >= w - r ? r : 0);
}

bool NowPlayingOverlay::ccTouchBegan(CCTouch* touch, CCEvent*) {
    if (!m_open || !this->isVisible()) return false;
    auto loc = touch->getLocation();
    auto win = CCDirector::sharedDirector()->getWinSize();

    if (!nodeContains(m_panel, loc)) {
        // Clicking elsewhere closes it (the toolbar handles its own toggle button).
        if (loc.y < win.height - m_toolbarHeight) close();
        return false;
    }

    if (inProgressBar(loc) && MusicPlayer::get().lengthMs() > 0) {
        m_seeking = true;
        m_seekFraction = progressFraction(loc);
        return true;
    }
    m_pressed = buttonAt(loc);
    if (m_pressed) m_pressed->scale.to(0.75f, 2000, Easing::OutQuint); // OsuAnimatedButton.ScaleOnMouseDown
    return true; // the card swallows everything that lands on it
}

void NowPlayingOverlay::ccTouchMoved(CCTouch* touch, CCEvent*) {
    if (!m_seeking) return;
    m_seekFraction = progressFraction(touch->getLocation());
    float now = static_cast<float>(nowMs());
    if (now - m_lastSeekMs >= SEEK_DEBOUNCE_MS) {
        m_lastSeekMs = now;
        MusicPlayer::get().seek(m_seekFraction);
    }
}

void NowPlayingOverlay::ccTouchEnded(CCTouch* touch, CCEvent*) {
    auto loc = touch->getLocation();
    if (m_seeking) {
        m_seeking = false;
        MusicPlayer::get().seek(progressFraction(loc));
        return;
    }
    if (!m_pressed) return;
    auto pressed = m_pressed;
    m_pressed = nullptr;
    pressed->scale.to(1.f, 1000, Easing::OutElastic);
    if (buttonAt(loc) != pressed) return;
    pressed->flash.set(1.f);
    pressed->flash.to(0.f, 800, Easing::OutQuint);
    sfx::click(sfx::sound::DEFAULT_SELECT);
    if (pressed->action) pressed->action();
}

} // namespace lazer
