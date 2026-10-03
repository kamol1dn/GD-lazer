#include "PauseMenu.hpp"

#include "../../audio/Sfx.hpp"
#include "../core/RoundedBox.hpp"
#include "../core/Text.hpp"
#include "../core/Theme.hpp"
#include "GameplayButtons.hpp"
#include "SettingsRows.hpp"

#include <Geode/Geode.hpp>

#include <algorithm>
#include <cmath>

using namespace geode::prelude;

namespace lazer {

using namespace lazer::gameplay;

namespace {
    constexpr float TRANSITION_MS = 200;     // GameplayMenuOverlay.TRANSITION_DURATION
    constexpr float QUIT_GAP = 28;
    constexpr float SLIDER_WIDTH = 190, SLIDER_GAP = 40; // GD's music / effects sliders

    CCNodeRGBA* group() {
        auto node = CCNodeRGBA::create();
        node->setCascadeOpacityEnabled(true);
        return node;
    }

    // One line of osu!'s play info: plain text, then the value in bold.
    CCNode* infoLine(std::string const& text, std::string const& value, float size) {
        auto node = group();
        auto a = makeText(text, Weight::Regular, size);
        a->setColor({200, 200, 200});
        auto b = makeText(value, Weight::Bold, size);
        float wa = a->getScaledContentSize().width, wb = b->getScaledContentSize().width;
        a->setAnchorPoint({0, 0.5f});
        a->setPosition({-(wa + wb) / 2, 0});
        b->setAnchorPoint({0, 0.5f});
        b->setPosition({-(wa + wb) / 2 + wa, 0});
        node->addChild(a);
        node->addChild(b);
        node->setContentSize({wa + wb, size});
        return node;
    }

    // GD's normal / practice progress bars, as a slim osu! bar with its value.
    CCNode* progressBar(std::string const& name, int percent, ccColor4B colour, float k) {
        auto node = group();
        float size = 14 * k, track = 120 * k, h = 6 * k, gap = 8 * k;
        auto label = makeText(name, Weight::Regular, size);
        label->setColor({200, 200, 200});
        auto value = makeText(fmt::format("{}%", percent), Weight::Bold, size);
        float wl = label->getScaledContentSize().width, wv = value->getScaledContentSize().width;
        float total = wl + gap + track + gap + wv;
        float x = -total / 2;
        label->setAnchorPoint({0, 0.5f});
        label->setPosition({x, 0});
        node->addChild(label);
        x += wl + gap;
        auto back = RoundedBox::create({track, h}, h / 2, {255, 255, 255, 40});
        back->setAnchorPoint({0, 0.5f});
        back->setPosition({x, 0});
        node->addChild(back);
        float fill = track * std::clamp(percent, 0, 100) / 100.f;
        if (fill > 0) {
            auto bar = RoundedBox::create({std::max(h, fill), h}, h / 2, colour);
            bar->setAnchorPoint({0, 0.5f});
            bar->setPosition({x, 0});
            node->addChild(bar);
        }
        x += track + gap;
        value->setAnchorPoint({0, 0.5f});
        value->setPosition({x, 0});
        node->addChild(value);
        node->setContentSize({total, size});
        return node;
    }

    std::string formatTime(double seconds) {
        if (seconds < 0) seconds = 0;
        int ms = static_cast<int>(std::round(seconds * 1000));
        return fmt::format("{}:{:02}.{:03}", ms / 60000, (ms / 1000) % 60, ms % 1000);
    }

    void findSliders(CCNode* node, std::vector<Slider*>& out) {
        if (auto slider = typeinfo_cast<Slider*>(node)) {
            out.push_back(slider);
            return;
        }
        for (auto child : CCArrayExt<CCNode*>(node->getChildren())) findSliders(child, out);
    }

}

PauseMenu* PauseMenu::create(PauseLayer* layer) {
    auto ret = new PauseMenu();
    if (ret->init(layer)) {
        ret->autorelease();
        return ret;
    }
    delete ret;
    return nullptr;
}

bool PauseMenu::init(PauseLayer* layer) {
    if (!CCLayerRGBA::init()) return false;
    m_layer = layer;
    m_k = unitScale();
    this->setID("pause-menu"_spr);
    this->setCascadeOpacityEnabled(true);

    // GD's pause menu, hidden (never removed: other mods find their nodes).
    std::vector<Slider*> sliders;
    for (auto child : CCArrayExt<CCNode*>(layer->getChildren())) {
        findSliders(child, sliders);
        m_vanillaNodes.emplace_back(child);
        child->setVisible(false);
    }
    // Hidden sliders would still take drags: ours drive them instead.
    for (auto slider : sliders) {
        slider->setTouchEnabled(false);
        if (slider->m_touchLogic) slider->m_touchLogic->setTouchEnabled(false);
    }
    // GD makes the music slider first, then the effects one.
    if (sliders.size() == 2) {
        m_musicSlider = sliders[0];
        m_sfxSlider = sliders[1];
    }

    build();
    layout();
    m_alpha.to(1, TRANSITION_MS, Easing::In);
    this->setOpacity(0);
    this->setTouchEnabled(true);
    this->scheduleUpdate();
    return true;
}

void PauseMenu::build() {
    float k = m_k;
    auto win = CCDirector::get()->getWinSize();
    auto play = PlayLayer::get();
    auto level = play ? play->m_level : nullptr;
    bool practice = play && play->m_isPracticeMode;
    bool platformer = level && level->isPlatformer();

    auto dim = CCLayerColor::create({0, 0, 0, static_cast<GLubyte>(BACKGROUND_ALPHA * 255)});
    dim->setContentSize(win);
    this->addChild(dim, -1);

    // "paused", and the level underneath it.
    m_titleBlock = group();
    auto title = makeText("paused", Weight::SemiBold, TITLE_SIZE * k);
    spaceLetters(title, TITLE_SPACING * k);
    title->setColor(theme::rgb(YELLOW));
    m_titleBlock->addChild(title);
    float titleH = TITLE_SIZE * k;
    float blockH = titleH;
    if (level) {
        std::string text = level->m_levelName;
        std::string creator = level->m_creatorName;
        if (!creator.empty() && level->m_levelType != GJLevelType::Editor) text += " by " + creator;
        if (practice) text += " (practice)";
        auto desc = makeText(text, Weight::Regular, INFO_SIZE * k);
        desc->setColor({200, 200, 200});
        float maxW = win.width - 80 * k;
        if (desc->getScaledContentSize().width > maxW) desc->setScale(desc->getScale() * maxW / desc->getScaledContentSize().width);
        // Clear of the title's descender (the p hangs below its baseline).
        blockH += 12 * k + INFO_SIZE * k;
        desc->setPosition({0, -blockH / 2 + INFO_SIZE * k / 2});
        m_titleBlock->addChild(desc);
    }
    title->setPosition({0, blockH / 2 - titleH / 2});
    m_titleBlock->setContentSize({0, blockH});
    this->addChild(m_titleBlock);

    // GD's progress bars under the title (classic levels).
    m_barsBlock = group();
    if (level && !platformer) {
        auto normal = progressBar("normal", level->m_normalPercent.value(), GREEN, k);
        auto practiceBar = progressBar("practice", level->m_practicePercent, BLUE_LIGHT, k);
        float gap = 36 * k;
        float w1 = normal->getContentSize().width, w2 = practiceBar->getContentSize().width;
        if (w1 + gap + w2 <= win.width - 40 * k) {
            normal->setPosition({-(w1 + gap + w2) / 2 + w1 / 2, 0});
            practiceBar->setPosition({(w1 + gap + w2) / 2 - w2 / 2, 0});
            m_barsBlock->setContentSize({w1 + gap + w2, 14 * k});
        } else {
            normal->setPosition({0, 10 * k});
            practiceBar->setPosition({0, -10 * k});
            m_barsBlock->setContentSize({std::max(w1, w2), 34 * k});
        }
        m_barsBlock->addChild(normal);
        m_barsBlock->addChild(practiceBar);
    }
    this->addChild(m_barsBlock);

    // Retry count and progress (GameplayMenuOverlay.updateInfoText).
    m_infoBlock = group();
    std::vector<CCNode*> lines;
    if (play) {
        lines.push_back(infoLine("Retry count: ", std::to_string(std::max(0, play->m_attempts - 1)), INFO_SIZE * k));
        if (platformer) {
            lines.push_back(infoLine("Time: ", formatTime(play->m_attemptTime), INFO_SIZE * k));
            int best = level->m_bestTime;
            lines.push_back(infoLine("Best time: ", best > 0 ? formatTime(best / 1000.0) : "none", INFO_SIZE * k));
        } else {
            lines.push_back(infoLine("Song progress: ", fmt::format("{}%", play->getCurrentPercentInt()), INFO_SIZE * k));
        }
    }
    float lineGap = 5 * k;
    float infoH = 0;
    for (auto line : lines) infoH += line->getContentSize().height;
    if (!lines.empty()) infoH += lineGap * (lines.size() - 1);
    float y = infoH / 2;
    for (auto line : lines) {
        float h = line->getContentSize().height;
        line->setPosition({0, y - h / 2});
        y -= h + lineGap;
        m_infoBlock->addChild(line);
    }
    m_infoBlock->setContentSize({0, infoH});
    this->addChild(m_infoBlock);

    // GD's music and effects sliders, through GD's own (hidden) sliders and
    // handlers when they're there.
    // A plain pointer: the menu is the layer's child, so a strong reference
    // here would be a cycle that kept the pause layer (and other mods'
    // keybind listeners on it) alive after the level, and a pause key pressed
    // anywhere later would resume a level that's gone.
    PauseLayer* layerRef = m_layer;
    Ref<Slider> music = m_musicSlider;
    Ref<Slider> effects = m_sfxSlider;
    std::function<float()> getMusic = [] { return GameManager::get()->m_bgVolume; };
    std::function<void(float)> setMusic = [](float v) {
        GameManager::get()->m_bgVolume = v;
        FMODAudioEngine::sharedEngine()->setBackgroundMusicVolume(v);
    };
    std::function<float()> getSfx = [] { return GameManager::get()->m_sfxVolume; };
    std::function<void(float)> setSfx = [](float v) {
        GameManager::get()->m_sfxVolume = v;
        FMODAudioEngine::sharedEngine()->setEffectsVolume(v);
    };
    if (music && effects) {
        getMusic = [music] { return music->getValue(); };
        setMusic = [layerRef, music](float v) {
            music->setValue(v);
            layerRef->musicSliderChanged(music->getThumb());
        };
        getSfx = [effects] { return effects->getValue(); };
        setSfx = [layerRef, effects](float v) {
            effects->setValue(v);
            layerRef->sfxSliderChanged(effects->getThumb());
        };
    }
    auto percent = [](float v) { return fmt::format("{}%", static_cast<int>(std::round(v * 100))); };
    m_slidersBlock = group();
    float sliderW = SLIDER_WIDTH * k;
    m_sliders = {
        SliderRow::create("Music", sliderW, k, getMusic, setMusic, percent),
        SliderRow::create("Effects", sliderW, k, getSfx, setSfx, percent),
    };
    float sliderH = m_sliders.front()->getContentSize().height;
    bool sideBySide = 2 * sliderW + SLIDER_GAP * k <= win.width - 40 * k;
    if (sideBySide) {
        m_sliders[0]->setPosition({-sliderW - SLIDER_GAP * k / 2, -sliderH / 2});
        m_sliders[1]->setPosition({SLIDER_GAP * k / 2, -sliderH / 2});
        m_slidersBlock->setContentSize({2 * sliderW + SLIDER_GAP * k, sliderH});
    } else {
        m_sliders[0]->setPosition({-sliderW / 2, 0});
        m_sliders[1]->setPosition({-sliderW / 2, -sliderH});
        m_slidersBlock->setContentSize({sliderW, 2 * sliderH});
    }
    for (auto slider : m_sliders) m_slidersBlock->addChild(slider);
    this->addChild(m_slidersBlock);

    // Everything clickable is in one menu, like GD's own buttons here.
    m_menu = CCMenu::create();
    m_menu->setID("pause-buttons"_spr);
    m_menu->setPosition({0, 0});
    m_menu->setCascadeOpacityEnabled(true);
    this->addChild(m_menu, 1);

    // The middle row, in GD's order: practice, play (continue), retry.
    PauseLayer* layer = m_layer;
    auto middle = [&](char const* glyph, float size, ccColor4B colour, std::string const& caption, std::function<void()> action) {
        auto item = roundButton(glyph, size * k, colour, std::move(action));
        m_menu->addChild(item);
        m_middle.push_back(item);
        auto label = makeText(caption, Weight::SemiBold, CAPTION_SIZE * k);
        label->setColor({220, 220, 220});
        this->addChild(label);
        m_captions.push_back(label);
    };
    if (practice) middle(icon::PLAY, SIDE_SIZE, BLUE, "normal mode", [layer] { layer->onNormalMode(nullptr); });
    else middle(icon::GEM, SIDE_SIZE, BLUE, "practice", [layer] { layer->onPracticeMode(nullptr); });
    m_continueIndex = static_cast<int>(m_middle.size());
    middle(icon::PLAY, PLAY_SIZE, GREEN, "continue", [layer] { layer->onResume(nullptr); });
    middle(icon::ROTATE, SIDE_SIZE, YELLOW_DARK, "retry", [layer] { layer->onRestart(nullptr); });

    // The footer: quit on its own, then GD's extras.
    float h = FOOTER_HEIGHT * k;
    auto pill = [&](char const* glyph, std::string const& label, ccColor4B colour, std::function<void()> action) {
        auto item = pillButton(glyph, label, h, k, colour, std::move(action));
        m_menu->addChild(item);
        m_footer.push_back(item);
        return item;
    };
    // Through tryQuit: GD's "confirm exit" option asks first.
    m_quit = pill(icon::XMARK, "quit", QUIT_RED, [layer] { layer->tryQuit(nullptr); });
    auto extra = [&](char const* glyph, std::string const& label, std::function<void()> action) {
        m_extras.push_back(pill(glyph, label, GRAY4, std::move(action)));
    };
    if (play && (practice || play->m_isTestMode || play->m_startPosObject)) {
        extra(icon::STEP_BACKWARD, "from the start", [layer] { layer->onRestartFull(nullptr); });
    }
    if (level && level->m_levelType == GJLevelType::Editor) {
        extra(icon::PEN, "edit", [layer] { layer->onEdit(nullptr); });
    }
    extra(icon::GEAR, "options", [layer] { layer->onSettings(nullptr); });
}

void PauseMenu::layout() {
    float k = m_k;
    auto win = CCDirector::get()->getWinSize();
    float W = win.width, H = win.height;
    float gap = FOOTER_GAP * k;
    float maxW = W - 32 * k;

    // Footer: quit, a gap, the extras; mods on a row of their own above when
    // they don't fit next to the extras.
    std::vector<AnimatedButtonItem*> row {m_quit};
    std::vector<float> gaps {QUIT_GAP * k};
    for (auto item : m_extras) {
        row.push_back(item);
        gaps.push_back(gap);
    }
    float rowW = 0;
    for (size_t i = 0; i < row.size(); i++) rowW += row[i]->getContentSize().width + (i + 1 < row.size() ? gaps[i] : 0);
    float modsW = 0;
    for (auto item : m_mods) modsW += item->getContentSize().width + gap;
    bool modsInline = !m_mods.empty() && rowW + modsW <= maxW;
    if (modsInline) {
        for (auto item : m_mods) {
            row.push_back(item);
            gaps.push_back(gap);
        }
    }
    float y = FOOTER_MARGIN * k + FOOTER_HEIGHT * k / 2;
    layoutRow(row, gaps, y, W / 2, maxW);
    float footerTop = FOOTER_MARGIN * k + FOOTER_HEIGHT * k;
    if (!m_mods.empty() && !modsInline) {
        y += FOOTER_HEIGHT * k + gap;
        layoutRow(m_mods, std::vector<float>(m_mods.size(), gap), y, W / 2, maxW);
        footerTop += gap + FOOTER_HEIGHT * k;
    }

    // Title and bars from the top.
    float titleH = m_titleBlock->getContentSize().height;
    m_titleBlock->setPosition({W / 2, H - TOP_MARGIN * k - titleH / 2});
    float barsH = m_barsBlock->getContentSize().height;
    float top = H - TOP_MARGIN * k - titleH;
    if (barsH > 0) {
        m_barsBlock->setPosition({W / 2, top - 16 * k - barsH / 2});
        top -= 16 * k + barsH;
    }

    // The middle row and its captions, the info and the sliders, centred in
    // what's left. Short screens shrink the row.
    float infoH = m_infoBlock->getContentSize().height;
    float slidersH = m_slidersBlock->getContentSize().height;
    float room = top - footerTop;
    float rowH = PLAY_SIZE * k + CAPTION_GAP * k + CAPTION_SIZE * k;
    float infoGap = 22 * k, slidersGap = 32 * k;
    float rest = infoGap + infoH + slidersGap + slidersH;
    float needed = rowH + rest + 40 * k;
    float shrink = needed > room && needed > 0 ? std::max(0.5f, (room - rest - 40 * k) / rowH) : 1.f;
    rowH *= shrink;
    float blockH = rowH + rest;
    float blockTop = footerTop + (room + blockH) / 2;
    float centreY = blockTop - PLAY_SIZE * k * shrink / 2;

    float total = 0;
    for (auto item : m_middle) total += item->getContentSize().width * shrink;
    total += MIDDLE_GAP * k * shrink * (m_middle.size() - 1);
    float x = W / 2 - total / 2;
    for (size_t i = 0; i < m_middle.size(); i++) {
        auto item = m_middle[i];
        float w = item->getContentSize().width * shrink;
        item->setScale(shrink);
        item->setPosition({x + w / 2, centreY});
        // Captions line up under the row, whatever the button's size.
        m_captions[i]->setPosition({x + w / 2, blockTop - rowH + CAPTION_SIZE * k / 2});
        x += w + MIDDLE_GAP * k * shrink;
    }
    m_infoBlock->setPosition({W / 2, blockTop - rowH - infoGap - infoH / 2});
    m_slidersBlock->setPosition({W / 2, blockTop - rowH - infoGap - infoH - slidersGap});
}

void PauseMenu::takeModButtons() {
    auto found = collectModButtons(m_layer);
    if (found.empty()) return;
    float k = m_k;
    float size = FOOTER_HEIGHT * k;
    for (auto item : found) {
        log::debug("Pause menu mod button: {}", item->getID().view());
        Ref<CCMenuItem> target = item;
        auto button = AnimatedButtonItem::create({size, size}, 10 * k, GRAY4, modButtonImage(item, size * 0.72f),
                                                 [target] { target->activate(); });
        m_menu->addChild(button);
        m_mods.push_back(button);
        m_footer.push_back(button);
        // Its own menu (if a mod made one) goes too; GD's are hidden already.
        if (auto parent = item->getParent()) parent->setVisible(false);
    }
    layout();
}

void PauseMenu::select(int index) {
    if (index == m_selected) return;
    if (m_selected >= 0 && m_selected < static_cast<int>(m_middle.size())) m_middle[m_selected]->setHovered(false);
    m_selected = index;
    if (m_selected >= 0 && m_selected < static_cast<int>(m_middle.size())) m_middle[m_selected]->setHovered(true);
}

bool PauseMenu::handleKey(enumKeyCodes key) {
    int n = static_cast<int>(m_middle.size());
    if (n == 0 || gameplayPopupOnTop()) return false;
    switch (key) {
        // From nothing, the first pick is continue (the middle button).
        case KEY_Left:
        case KEY_Up:
            select(m_selected < 0 ? m_continueIndex : (m_selected + n - 1) % n);
            return true;
        case KEY_Right:
        case KEY_Down:
            select(m_selected < 0 ? m_continueIndex : (m_selected + 1) % n);
            return true;
        case KEY_Enter:
            if (m_selected < 0) return false;
            // May close the pause menu: nothing after it.
            m_middle[m_selected]->activate();
            return true;
        default:
            return false;
    }
}

SliderRow* PauseMenu::sliderAt(CCPoint world) const {
    for (auto slider : m_sliders) {
        auto local = slider->convertToNodeSpace(world);
        auto size = slider->getContentSize();
        // A little above and below the row too: the bar is thin.
        if (local.x >= -8 * m_k && local.x <= size.width + 8 * m_k && local.y >= -8 * m_k && local.y <= size.height) return slider;
    }
    return nullptr;
}

// Above the menu (and GD's layer): a touch on a slider is ours to drag, any
// other passes on.
void PauseMenu::registerWithTouchDispatcher() {
    CCDirector::get()->getTouchDispatcher()->addTargetedDelegate(this, -550, true);
}

bool PauseMenu::ccTouchBegan(CCTouch* touch, CCEvent*) {
    if (gameplayPopupOnTop()) return false;
    auto slider = sliderAt(touch->getLocation());
    if (!slider) return false;
    m_draggingSlider = slider;
    slider->onDrag(slider->convertToNodeSpace(touch->getLocation()));
    return true;
}

void PauseMenu::ccTouchMoved(CCTouch* touch, CCEvent*) {
    if (m_draggingSlider) m_draggingSlider->onDrag(m_draggingSlider->convertToNodeSpace(touch->getLocation()));
}

void PauseMenu::ccTouchEnded(CCTouch*, CCEvent*) {
    m_draggingSlider = nullptr;
}

void PauseMenu::ccTouchCancelled(CCTouch*, CCEvent*) {
    m_draggingSlider = nullptr;
}

void PauseMenu::update(float dt) {
    // Other pause mods can reveal GD's nodes after setup. They remain alive
    // for their handlers, but only the custom controls should be drawn.
    for (auto const& node : m_vanillaNodes) {
        if (node->getParent() == m_layer) node->setVisible(false);
    }
    // Once every mod has had its customSetup: gather their buttons.
    if (!m_scanned) {
        m_scanned = true;
        takeModButtons();
    }
    // GD's own dim (the layer's colour) gives way to osu!'s.
    if (!m_layer->isCascadeOpacityEnabled() && m_layer->getOpacity() != 0) m_layer->setOpacity(0);

    if (m_alpha.update(dt) || this->getOpacity() != 255) {
        this->setOpacity(static_cast<GLubyte>(std::clamp(m_alpha.get(), 0.f, 1.f) * 255.f));
    }

#ifdef GEODE_IS_DESKTOP
    if (gameplayPopupOnTop()) return;
    auto mouse = geode::cocos::getMousePos();
    bool moved = mouse.x != m_lastMouse.x || mouse.y != m_lastMouse.y;
    m_lastMouse = mouse;
    for (auto item : m_footer) item->setHovered(item->isVisible() && item->containsWorldPoint(mouse));
    auto overSlider = m_draggingSlider ? m_draggingSlider : sliderAt(mouse);
    for (auto slider : m_sliders) slider->setHovered(slider == overSlider);
    // Hovering a middle button selects it; leaving it deselects (keyboard
    // picks stay until the mouse moves).
    if (moved) {
        int hovered = -1;
        for (int i = 0; i < static_cast<int>(m_middle.size()); i++) {
            if (m_middle[i]->containsWorldPoint(mouse)) hovered = i;
        }
        select(hovered);
    }
#endif
}

} // namespace lazer
