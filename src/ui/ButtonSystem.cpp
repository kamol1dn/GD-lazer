#include "ButtonSystem.hpp"

using namespace cocos2d;

namespace lazer {

namespace {
    // osu! lays the menu out for a 768px-tall screen.
    constexpr float OSU_HEIGHT = 768.f;
    constexpr float BUTTON_WIDTH = 140.f;
    constexpr float BUTTON_AREA_HEIGHT = 100.f;
    constexpr float WEDGE_WIDTH = 20.f;

    constexpr float LOGO_TOPLEVEL_SCALE = 0.5f;
    // LogoTrackingContainer.LogoFacade scale: how much room the logo takes in the flow.
    constexpr float LOGO_FACADE_SCALE = 0.74f;

    constexpr ccColor4B BAR_COLOR {50, 50, 50, 255}; // OsuColour.Gray(50)
}

ButtonSystem* ButtonSystem::create(std::vector<ButtonDef> left, std::vector<ButtonDef> right) {
    auto ret = new ButtonSystem();
    if (ret->init(std::move(left), std::move(right))) {
        ret->autorelease();
        return ret;
    }
    delete ret;
    return nullptr;
}

bool ButtonSystem::init(std::vector<ButtonDef> left, std::vector<ButtonDef> right) {
    if (!CCNode::init()) return false;

    auto win = CCDirector::sharedDirector()->getWinSize();
    this->setContentSize(win);
    m_center = win / 2;

    float k = win.height / OSU_HEIGHT;
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

    for (auto const& def : left) m_left.push_back(makeButton(def));
    for (auto const& def : right) m_right.push_back(makeButton(def));

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
            if (leaves) this->setState(State::EnteringMode);
            if (action) action();
        }
    );
    button->setExplodes(leaves);
    button->setSelectSound(def.sound);
    this->addChild(button, 1);
    return button;
}

void ButtonSystem::onLogoClicked() {
    switch (m_state) {
        case State::Initial:
            sfx::play(sfx::sound::LOGO_SELECT);
            setState(State::TopLevel);
            break;
        case State::TopLevel:
            // Clicking the logo at top level presses the first button (Play), like osu!.
            if (!m_right.empty()) m_right.front()->trigger();
            break;
        case State::EnteringMode:
            break;
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
    schedule(delay, [this, state] {
        switch (state) {
            case State::Initial:
                m_barAlpha.to(0, 300, Easing::None);
                m_barScaleX.to(2, 300, Easing::InSine);
                m_barScaleY.to(0, 300, Easing::InSine);
                for (auto b : m_left) b->setState(MenuButton::State::Contracted);
                for (auto b : m_right) b->setState(MenuButton::State::Contracted);
                break;
            case State::TopLevel:
                m_barAlpha.to(1, 300, Easing::None);
                m_barScaleX.to(1, 400, Easing::OutQuint);
                m_barScaleY.to(1, 400, Easing::OutQuint);
                for (auto b : m_left) b->setState(MenuButton::State::Expanded);
                for (auto b : m_right) b->setState(MenuButton::State::Expanded);
                break;
            case State::EnteringMode:
                m_barAlpha.to(0, 300, Easing::None);
                m_barScaleX.to(2, 300, Easing::InSine);
                m_barScaleY.to(0, 300, Easing::InSine);
                for (auto list : {&m_left, &m_right}) {
                    for (auto b : *list) {
                        if (b->getState() != MenuButton::State::Exploded) {
                            b->setState(MenuButton::State::Contracted, 1);
                        }
                    }
                }
                break;
        }
    });
}

void ButtonSystem::resumeTopLevel() {
    // Coming back from another screen: logo already parked, just unfold the buttons.
    m_logoPos.set(m_logoTarget);
    m_logoScale.set(LOGO_TOPLEVEL_SCALE);
    setState(State::TopLevel);
}

bool ButtonSystem::back() {
    if (m_state != State::TopLevel) return false;
    sfx::play(sfx::sound::BACK_TO_LOGO);
    setState(State::Initial);
    return true;
}

void ButtonSystem::layoutButtons() {
    // Buttons flow outwards from the logo's resting spot, each taking its
    // *current* width, with neighbours overlapping by one wedge.
    float facadeHalf = m_logoRadius * LOGO_TOPLEVEL_SCALE * LOGO_FACADE_SCALE;
    float y = m_center.y;

    float x = m_logoTarget.x + facadeHalf;
    for (auto b : m_right) {
        float w = b->flowWidth();
        b->setPosition({x + w / 2, y});
        x += w - m_wedge;
    }

    x = m_logoTarget.x - facadeHalf;
    for (auto b : m_left) {
        float w = b->flowWidth();
        b->setPosition({x - w / 2, y});
        x -= w - m_wedge;
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
    m_barAlpha.update(dt);
    m_barScaleX.update(dt);
    m_barScaleY.update(dt);

    m_logo->setPosition(m_logoPos.get());
    m_logo->setScale(m_logoScale);

    m_barHolder->setScaleX(m_barScaleX);
    m_barHolder->setScaleY(std::max(0.f, m_barScaleY.get()));
    m_bar->setOpacity(static_cast<GLubyte>(std::clamp(m_barAlpha.get(), 0.f, 1.f) * 255));
    m_barHolder->setVisible(m_barAlpha.get() > 0.001f);

    layoutButtons();
}

} // namespace lazer
