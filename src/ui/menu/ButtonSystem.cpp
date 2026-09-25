#include "ButtonSystem.hpp"

#include "../core/Text.hpp"
#include "../core/Theme.hpp"

#include <Geode/loader/Log.hpp>

using namespace cocos2d;

namespace lazer {

namespace {
    // osu! pixels (see unitScale).
    constexpr float BUTTON_WIDTH = 140.f;
    constexpr float BUTTON_AREA_HEIGHT = 100.f;
    constexpr float WEDGE_WIDTH = 20.f;

    constexpr float LOGO_TOPLEVEL_SCALE = 0.5f;
    // LogoTrackingContainer.LogoFacade scale: how much room the logo takes in the flow.
    constexpr float LOGO_FACADE_SCALE = 0.74f;

    constexpr ccColor4B BAR_COLOR {50, 50, 50, 255}; // OsuColour.Gray(50)
    constexpr ccColor3B BACK_COLOR {51, 58, 94};
}

ButtonSystem* ButtonSystem::create(std::vector<ButtonDef> buttons) {
    auto ret = new ButtonSystem();
    if (ret->init(std::move(buttons))) {
        ret->autorelease();
        return ret;
    }
    delete ret;
    return nullptr;
}

bool ButtonSystem::init(std::vector<ButtonDef> buttons) {
    if (!CCNode::init()) return false;

    auto win = CCDirector::sharedDirector()->getWinSize();
    this->setContentSize(win);
    m_center = win / 2;

    float k = unitScale();
    m_buttonWidth = BUTTON_WIDTH * k;
    m_barHeight = BUTTON_AREA_HEIGHT * k;
    m_wedge = WEDGE_WIDTH * k;
    m_logoRadius = win.height * 0.28f;

    // Same offset osu! gives ButtonArea.Flow, so the whole group reads as centred.
    float logoFlowSize = m_logoRadius * 2;
    m_logoTarget = CCPoint(m_center.x + m_wedge * 2 - (m_buttonWidth + logoFlowSize / 4), m_center.y);

    m_barHolder = CCNode::create();
    m_barHolder->setPosition(m_center);
    this->addChild(m_barHolder, 0);

    m_bar = RoundedBox::create({win.width, m_barHeight}, 0, BAR_COLOR);
    m_barHolder->addChild(m_bar);

    // "back", nearest the logo on the left, in every submenu.
    ButtonDef back {"back", icon::CIRCLE_CHEVRON_LEFT, BACK_COLOR, [this] { this->setState(State::TopLevel); }, false};
    m_left.push_back({makeButton(back), State::Play, State::Browse});

    // osu! flows the submenus' buttons before the top level's, so the top
    // level explodes outwards while a submenu unfolds from the logo.
    for (auto state : {State::Play, State::Create, State::Browse, State::TopLevel}) {
        for (auto const& def : buttons) {
            if (def.visibleIn != state) continue;
            auto& side = def.left ? m_left : m_right;
            side.push_back({makeButton(def), state, state});
        }
    }

    m_logo = LazerLogo::create(m_logoRadius);
    m_logo->setCallback([this] { this->onLogoClicked(); });
    this->addChild(m_logo, 2);

    m_logoPos.set(m_center);
    this->scheduleUpdate();
    this->update(0);
    return true;
}

MenuButton* ButtonSystem::makeButton(ButtonDef const& def) {
    auto action = def.action;
    bool leaves = def.leavesMenu;
    auto button = MenuButton::create(
        def.label, def.icon, def.color,
        {m_buttonWidth, m_barHeight, m_wedge},
        [this, action, leaves] {
            if (leaves) {
                State from = m_state;
                m_leftFrom = from;
                this->setState(State::EnteringMode);
                // GD sometimes stays put (a locked mode shows a popup instead):
                // if we're still the running scene a moment later, unfold again.
                schedule(900, [this, from] {
                    if (m_state != State::EnteringMode) return;
                    auto director = cocos2d::CCDirector::sharedDirector();
                    CCNode* scene = this;
                    while (scene->getParent()) scene = scene->getParent();
                    bool stayed = director->getRunningScene() == scene && !director->getNextScene();
                    geode::log::debug("Button system: left menu? {}", !stayed);
                    if (stayed) this->resume(from);
                });
            }
            if (action) action();
        }
    );
    button->setExplodes(leaves);
    button->setSelectSound(def.sound);
    this->addChild(button, 1);
    return button;
}

void ButtonSystem::onLogoClicked() {
    if (m_exiting) return;
    if (m_state == State::Initial) {
        sfx::play(sfx::sound::LOGO_SELECT);
        setState(State::TopLevel);
        return;
    }
    // Otherwise the logo presses the first button of the current menu (Play, Solo...), like osu!.
    if (!isMenu(m_state)) return;
    for (auto& e : m_right) {
        if (e.min == m_state) {
            e.button->trigger();
            return;
        }
    }
}

void ButtonSystem::schedule(float delayMs, std::function<void()> fn, bool isLogoAction) {
    if (isLogoAction) {
        std::erase_if(m_pending, [](Pending const& p) { return p.isLogoAction; });
    }
    m_pending.push_back({delayMs, std::move(fn), isLogoAction});
}

void ButtonSystem::setState(State state) {
    if (state == m_state) return;
    State last = m_state;
    m_state = state;
    geode::log::debug("Button system: {} -> {}", static_cast<int>(last), static_cast<int>(state));
    if (m_stateCallback) m_stateCallback(state);

    // --- logo (ButtonSystem.updateLogoState) ---
    switch (state) {
        case State::Initial:
            schedule(m_barAlpha.get() * 150.f, [this] {
                m_logoPos.to(m_center, 800, Easing::OutExpo);
                m_logoScale.to(1.f, 800, Easing::OutExpo);
            }, true);
            if (last == State::TopLevel) sfx::play(sfx::sound::LOGO_SWOOSH);
            break;
        case State::TopLevel:
        case State::Play:
        case State::Create:
        case State::Browse:
            if (last == State::Initial) {
                bool impact = m_logoScale.get() > 0.6f;
                m_logoScale.to(LOGO_TOPLEVEL_SCALE, 200, Easing::In);
                m_logoPos.to(m_logoTarget, 200, Easing::In);
                schedule(200, [this, impact] {
                    if (impact) m_logo->playImpact();
                }, true);
            } else {
                m_logoPos.to(m_logoTarget, 0);
                m_logoScale.to(LOGO_TOPLEVEL_SCALE, 200, Easing::OutQuint);
            }
            break;
        case State::EnteringMode:
            break;
    }

    // --- bar + buttons (ButtonArea / MainMenuButton.UpdateState) ---
    float delay = last == State::Initial ? 150.f : 0.f;
    schedule(delay, [this, state] { this->updateButtons(state); });
}

void ButtonSystem::updateButtons(State state) {
    bool menu = isMenu(state);
    m_barAlpha.to(menu ? 1.f : 0.f, 300, Easing::None);
    m_barScaleX.to(menu ? 1.f : 2.f, menu ? 400 : 300, menu ? Easing::OutQuint : Easing::InSine);
    m_barScaleY.to(menu ? 1.f : 0.f, menu ? 400 : 300, menu ? Easing::OutQuint : Easing::InSine);

    for (auto list : {&m_left, &m_right}) {
        for (auto& e : *list) {
            auto b = e.button;
            if (state == State::Initial) {
                b->setState(MenuButton::State::Contracted);
            } else if (state == State::EnteringMode) {
                if (b->getState() != MenuButton::State::Exploded) b->setState(MenuButton::State::Contracted, 1);
            } else if (state >= e.min && state <= e.max) {
                b->setState(MenuButton::State::Expanded);
            } else if (state < e.min || b->getState() == MenuButton::State::Contracted) {
                // Ahead of us (or already folded): stay folded behind the logo.
                b->setState(MenuButton::State::Contracted);
            } else {
                b->setState(MenuButton::State::Exploded);
            }
        }
    }
}

void ButtonSystem::resume(State state) {
    // Coming back from another screen: logo already parked, just unfold the buttons.
    m_logoPos.set(m_logoTarget);
    m_logoScale.set(LOGO_TOPLEVEL_SCALE);
    setState(isMenu(state) ? state : State::TopLevel);
}

void ButtonSystem::onEnter() {
    CCNode::onEnter();
    if (m_state == State::EnteringMode) {
        geode::log::debug("Button system: back on the menu, unfolding {}", static_cast<int>(m_leftFrom));
        resume(m_leftFrom);
    }
}

void ButtonSystem::playExit(float durationMs) {
    m_exiting = true;
    setState(State::Initial);
    m_logoRotation.to(20, durationMs * 1.5f, Easing::None);
}

bool ButtonSystem::back() {
    switch (m_state) {
        case State::TopLevel:
            sfx::play(sfx::sound::BACK_TO_LOGO);
            setState(State::Initial);
            return true;
        case State::Play:
        case State::Create:
        case State::Browse:
            // osu! clicks its back button.
            m_left.front().button->trigger();
            return true;
        default:
            return false;
    }
}

void ButtonSystem::layoutButtons() {
    // Buttons flow outwards from the logo's resting spot, each taking its
    // *current* width, with neighbours overlapping by one wedge.
    float facadeHalf = m_logoRadius * LOGO_TOPLEVEL_SCALE * LOGO_FACADE_SCALE;
    float y = m_center.y;

    float x = m_logoTarget.x + facadeHalf;
    for (auto& e : m_right) {
        auto b = e.button;
        float w = b->flowWidth();
        b->setPosition({x + w / 2, y});
        x += w - std::min(w, m_wedge); // folded (zero-width) buttons take no room
    }

    x = m_logoTarget.x - facadeHalf;
    for (auto& e : m_left) {
        auto b = e.button;
        float w = b->flowWidth();
        b->setPosition({x - w / 2, y});
        x -= w - std::min(w, m_wedge);
    }
}

void ButtonSystem::update(float dt) {
    float ms = dt * 1000.f;
    // Run due delayed actions. Take the list first: actions may schedule new ones.
    auto pending = std::move(m_pending);
    m_pending.clear();
    for (auto& p : pending) {
        p.remainingMs -= ms;
        if (p.remainingMs <= 0) p.fn();
        else m_pending.push_back(std::move(p));
    }

    m_logoPos.update(dt);
    m_logoScale.update(dt);
    m_logoRotation.update(dt);
    m_barAlpha.update(dt);
    m_barScaleX.update(dt);
    m_barScaleY.update(dt);

    m_logo->setPosition(m_logoPos.get());
    m_logo->setScale(m_logoScale);
    m_logo->setRotation(m_logoRotation);

    m_barHolder->setScaleX(m_barScaleX);
    m_barHolder->setScaleY(std::max(0.f, m_barScaleY.get()));
    m_bar->setOpacity(static_cast<GLubyte>(std::clamp(m_barAlpha.get(), 0.f, 1.f) * 255));
    m_barHolder->setVisible(m_barAlpha.get() > 0.001f);

    layoutButtons();
}

} // namespace lazer
