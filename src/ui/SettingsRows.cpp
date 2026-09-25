#include "SettingsRows.hpp"

#include "Text.hpp"
#include "Theme.hpp"

#include <algorithm>
#include <cctype>

using namespace cocos2d;

namespace lazer {

namespace {
    GLubyte toByte(float a) { return static_cast<GLubyte>(std::clamp(a, 0.f, 1.f) * 255.f); }

    std::string lower(std::string s) {
        std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return std::tolower(c); });
        return s;
    }

    std::string upper(std::string s) {
        std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return std::toupper(c); });
        return s;
    }

    template <class T, class... Args>
    T* make(Args&&... args) {
        auto ret = new T();
        if (ret->init(std::forward<Args>(args)...)) {
            ret->autorelease();
            return ret;
        }
        delete ret;
        return nullptr;
    }

    constexpr ccColor4B HOVER_BG {255, 255, 255, 12};
}

// ---------------------------------------------------------------------------

SectionHeaderRow* SectionHeaderRow::create(std::string const& title, float width, float k) {
    auto row = new SectionHeaderRow();
    row->init();
    row->autorelease();
    row->setContentSize({width, 64 * k});
    auto text = makeText(title, Weight::Regular, 30 * k);
    text->setAnchorPoint({0, 0.5f});
    text->setPosition({0, 30 * k});
    row->addChild(text);
    row->m_searchText = lower(title);
    return row;
}

SubsectionHeaderRow* SubsectionHeaderRow::create(std::string const& title, float width, float k) {
    auto row = new SubsectionHeaderRow();
    row->init();
    row->autorelease();
    row->setContentSize({width, 36 * k});
    auto text = makeText(upper(title), Weight::Bold, 15 * k);
    text->setColor(theme::LIGHT1);
    text->setAnchorPoint({0, 0.5f});
    text->setPosition({0, 16 * k});
    row->addChild(text);
    row->m_searchText = lower(title);
    return row;
}

// ---------------------------------------------------------------------------

ToggleRow* ToggleRow::create(std::string const& label, float width, float k,
                             std::function<bool()> get, std::function<bool()> toggle) {
    return make<ToggleRow>(label, width, k, std::move(get), std::move(toggle));
}

bool ToggleRow::init(std::string const& label, float width, float k,
                     std::function<bool()> get, std::function<bool()> toggle) {
    if (!CCNode::init()) return false;
    m_get = std::move(get);
    m_toggle = std::move(toggle);
    m_searchText = lower(label);
    float h = 36 * k;
    this->setContentSize({width, h});

    m_hoverBg = RoundedBox::create({width + 12 * k, h}, 5 * k, HOVER_BG);
    m_hoverBg->setPosition({width / 2, h / 2});
    m_hoverBg->setOpacity(0);
    this->addChild(m_hoverBg);

    // Nub.cs: 50 x 15 with a 3px border (osu! px).
    m_nubWidth = 50 * k;
    m_nubHeight = 15 * k;
    m_border = 3 * k;

    auto text = makeText(label, Weight::Regular, 17 * k);
    text->setAnchorPoint({0, 0.5f});
    text->setPosition({0, h / 2});
    // Keep long GD labels clear of the switch.
    float maxTextWidth = width - m_nubWidth - 10 * k;
    if (text->getScaledContentSize().width > maxTextWidth) {
        text->setScale(text->getScale() * maxTextWidth / text->getScaledContentSize().width);
    }
    this->addChild(text, 1);

    m_nub = RoundedBox::create({m_nubWidth, m_nubHeight}, m_nubHeight / 2, {0, 0, 0, 0});
    m_nub->setAnchorPoint({1, 0.5f});
    m_nub->setPosition({width, h / 2});
    this->addChild(m_nub, 1);

    setValue(m_get ? m_get() : false, false);
    this->scheduleUpdate();
    this->update(0);
    return true;
}

void ToggleRow::setValue(bool on, bool animate) {
    // Nub.onCurrentValueChanged
    float d = animate ? 200.f : 0.f;
    m_fill.to(on ? 1.f : 0.f, d, Easing::OutQuint);
    if (on) {
        m_widthFactor.to(1.f, d, Easing::OutElasticHalf);
        m_borderScale.to(8.5f / 3.f, d, Easing::OutElasticHalf);
    } else {
        m_widthFactor.to(0.75f, d, Easing::OutQuint);
        m_borderScale.to(1.f, d, Easing::OutQuint);
    }
}

void ToggleRow::refresh() {
    setValue(m_get ? m_get() : false, false);
}

void ToggleRow::setHovered(bool hovered) {
    m_hover.to(hovered ? 1.f : 0.f, hovered ? 100.f : 300.f, Easing::OutQuint);
}

void ToggleRow::onClick(CCPoint) {
    if (m_toggle) setValue(m_toggle(), true);
}

void ToggleRow::update(float dt) {
    for (auto t : {&m_widthFactor, &m_fill, &m_borderScale, &m_hover}) t->update(dt);

    auto accent = theme::HIGHLIGHT1;
    float w = m_nubWidth * m_widthFactor.get();
    m_nub->setContentSize({w, m_nubHeight});
    m_nub->setRadius(m_nubHeight / 2);
    m_nub->setBorder(std::min(m_border * m_borderScale.get(), m_nubHeight / 2), accent);
    m_nub->setFillColor({accent.r, accent.g, accent.b, toByte(m_fill.get())});
    m_hoverBg->setOpacity(toByte(m_hover.get()));
}

// ---------------------------------------------------------------------------

SliderRow* SliderRow::create(std::string const& label, float width, float k,
                             std::function<float()> get, std::function<void(float)> set,
                             std::function<std::string(float)> format) {
    return make<SliderRow>(label, width, k, std::move(get), std::move(set), std::move(format));
}

bool SliderRow::init(std::string const& label, float width, float k,
                     std::function<float()> get, std::function<void(float)> set,
                     std::function<std::string(float)> format) {
    if (!CCNode::init()) return false;
    m_get = std::move(get);
    m_set = std::move(set);
    m_format = std::move(format);
    m_searchText = lower(label);
    float h = 52 * k;
    this->setContentSize({width, h});

    auto text = makeText(label, Weight::Regular, 17 * k);
    text->setAnchorPoint({0, 0.5f});
    text->setPosition({0, h - 14 * k});
    this->addChild(text);

    m_valueLabel = makeText("", Weight::SemiBold, 15 * k);
    m_valueLabel->setAnchorPoint({1, 0.5f});
    m_valueLabel->setPosition({width, h - 14 * k});
    m_valueLabel->setColor(theme::LIGHT1);
    this->addChild(m_valueLabel);

    m_barX = 0;
    m_barW = width;
    m_barY = 14 * k;
    float barH = 5 * k;
    m_track = RoundedBox::create({m_barW, barH}, barH / 2, theme::DARK3);
    m_track->setAnchorPoint({0, 0.5f});
    m_track->setPosition({m_barX, m_barY});
    this->addChild(m_track);

    m_filled = RoundedBox::create({0, barH}, barH / 2, theme::HIGHLIGHT1);
    m_filled->setAnchorPoint({0, 0.5f});
    m_filled->setPosition({m_barX, m_barY});
    this->addChild(m_filled);

    m_nub = RoundedBox::create({30 * k, 15 * k}, 7.5f * k, theme::HIGHLIGHT1);
    m_nub->setBorder(3 * k, {255, 255, 255, 255});
    this->addChild(m_nub, 1);

    refresh();
    this->scheduleUpdate();
    return true;
}

void SliderRow::refresh() {
    m_value = std::clamp(m_get ? m_get() : 0.f, 0.f, 1.f);
    layoutBar();
}

void SliderRow::layoutBar() {
    float x = m_barX + m_barW * m_value;
    m_filled->setContentSize({m_barW * m_value, m_filled->getContentSize().height});
    float nubW = m_nub->getContentSize().width;
    m_nub->setPosition({std::clamp(x, m_barX + nubW / 2, m_barX + m_barW - nubW / 2), m_barY});
    if (m_format) m_valueLabel->setString(m_format(m_value).c_str());
}

void SliderRow::onDrag(CCPoint local) {
    m_value = std::clamp((local.x - m_barX) / m_barW, 0.f, 1.f);
    if (m_set) m_set(m_value);
    layoutBar();
}

void SliderRow::setHovered(bool hovered) {
    m_hover.to(hovered ? 1.f : 0.f, 200, Easing::OutQuint);
}

void SliderRow::update(float dt) {
    m_hover.update(dt);
    // Nub grows slightly while hovered.
    m_nub->setScale(1.f + 0.15f * m_hover.get());
}

// ---------------------------------------------------------------------------

ButtonRow* ButtonRow::create(std::string const& label, float width, float k,
                             std::function<void()> action, bool dangerous) {
    return make<ButtonRow>(label, width, k, std::move(action), dangerous);
}

bool ButtonRow::init(std::string const& label, float width, float k, std::function<void()> action, bool dangerous) {
    if (!CCNode::init()) return false;
    m_action = std::move(action);
    m_searchText = lower(label);
    float h = 42 * k;
    float bh = 32 * k;
    this->setContentSize({width, h});

    // DangerousSettingsButton uses a red tone.
    m_color = dangerous ? ccColor4B{204, 51, 85, 255} : theme::COLOUR3;
    m_bg = RoundedBox::create({width, bh}, 5 * k, m_color);
    m_bg->setPosition({width / 2, h / 2});
    this->addChild(m_bg);

    auto text = makeText(label, Weight::SemiBold, 16 * k);
    text->setPosition({width / 2, h / 2});
    this->addChild(text, 1);

    this->scheduleUpdate();
    return true;
}

void ButtonRow::setHovered(bool hovered) {
    m_hover.to(hovered ? 1.f : 0.f, 200, Easing::OutQuint);
}

void ButtonRow::onClick(CCPoint) {
    m_flash.set(1.f);
    m_flash.to(0.f, 400, Easing::OutQuint);
    if (m_action) m_action();
}

void ButtonRow::update(float dt) {
    m_hover.update(dt);
    m_flash.update(dt);
    float t = std::clamp(m_hover.get() * 0.15f + m_flash.get() * 0.4f, 0.f, 1.f);
    m_bg->setFillColor(theme::lerp(m_color, {255, 255, 255, 255}, t));
}

} // namespace lazer
