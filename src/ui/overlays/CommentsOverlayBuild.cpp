#include "CommentsOverlayInternal.hpp"

#include "../../audio/Sfx.hpp"

#include <algorithm>

using namespace geode::prelude;

namespace lazer {

// --- building ---

CommentsOverlay::Pill& CommentsOverlay::addPill(std::vector<Pill>& list, CCNode* parent, CCSize size, float radius,
                                                CCPoint pos, CCPoint anchor, ccColor4B color, ccColor4B hoverColor,
                                                std::function<void()> action) {
    auto node = CCNode::create();
    node->setContentSize(size);
    node->setAnchorPoint(anchor);
    node->setPosition(pos);
    parent->addChild(node, 1);
    auto bg = RoundedBox::create(size, radius, color);
    bg->setPosition({size.width / 2, size.height / 2});
    node->addChild(bg);

    Pill pill;
    pill.node = node;
    pill.bg = bg;
    pill.color = color;
    pill.hoverColor = hoverColor;
    pill.action = std::move(action);
    list.push_back(std::move(pill));
    return list.back();
}

float CommentsOverlay::buildInfo(float y) {
    float k = m_k, W = bodySize().width;
    auto content = m_scroll->content();
    y += INFO_PADDING * k;

    // The description, as GD's InfoLayer shows it (with its words for none).
    std::string desc = trim(std::string(m_level->getUnpackedLevelDescription()));
    bool none = desc.empty();
    if (none) desc = "(No description provided)";
    auto text = makeWrappedText(desc, 14 * k, W - 2 * m_pad, theme::rgb(none ? m_scheme.foreground1() : m_scheme.content2()));
    text->setPosition({m_pad, -y});
    content->addChild(text, 1);
    y += text->getContentSize().height + 10 * k;

    // Chips under it, flowing onto another line on narrow screens: the ID
    // (tap to copy, like GD's copy button), when it was uploaded and updated,
    // what it's a copy of, and GD's own page.
    float h = TAB_HEIGHT * k, gap = 5 * k, x = m_pad, top = y;
    auto place = [&](float w) {
        if (x > m_pad && x + w > W - m_pad) {
            x = m_pad;
            top += h + gap;
        }
        CCPoint at {x, -(top + h / 2)};
        x += w + gap;
        return at;
    };
    auto muted = theme::rgb(m_scheme.foreground1());
    // A bare icon and label (nothing to tap).
    auto chip = [&](char const* glyph, std::string const& label) {
        auto icon = makeIcon(glyph, 10 * k);
        icon->setColor(muted);
        auto name = makeText(label, Weight::SemiBold, 12 * k);
        name->setColor(muted);
        float iconW = icon->getScaledContentSize().width;
        float w = iconW + 5 * k + name->getScaledContentSize().width + 10 * k;
        auto at = place(w);
        icon->setPosition({at.x + 5 * k + iconW / 2, at.y});
        content->addChild(icon, 1);
        name->setAnchorPoint({0, 0.5f});
        name->setPosition({at.x + 5 * k + iconW + 5 * k, at.y});
        content->addChild(name, 1);
    };
    // A header-style button (the sort header's refresh): background on hover.
    auto button = [&](char const* glyph, std::string const& label, float minLabelW, std::function<void()> action) {
        auto icon = makeIcon(glyph, 10 * k);
        auto name = makeText(label, Weight::SemiBold, 12 * k);
        float iconW = icon->getScaledContentSize().width;
        float labelW = std::max(minLabelW, name->getScaledContentSize().width);
        float w = iconW + 5 * k + labelW + 20 * k;
        auto& pill = addPill(m_fixedPills, content, {w, h}, 3 * k, place(w), {0, 0.5f}, CLEAR,
                             m_scheme.background3(), std::move(action));
        icon->setPosition({10 * k + iconW / 2, h / 2});
        pill.node->addChild(icon, 1);
        name->setAnchorPoint({0, 0.5f});
        name->setPosition({10 * k + iconW + 5 * k, h / 2});
        pill.node->addChild(name, 1);
        return name;
    };

    int id = m_levelID;
    m_idText = fmt::format("ID {}", id);
    float copiedW = makeText("copied", Weight::SemiBold, 12 * k)->getScaledContentSize().width;
    m_idLabel = button(icon::COPY, m_idText, copiedW, [this, id] {
        utils::clipboard::write(std::to_string(id));
        m_copiedMs = COPIED_MS;
        m_idLabel->setString("copied");
    });
    std::string uploaded = m_level->m_uploadDate;
    std::string updated = m_level->m_updateDate;
    if (!uploaded.empty()) chip(icon::CLOUD_UP, fmt::format("uploaded {} ago", uploaded));
    if (!updated.empty() && updated != uploaded) chip(icon::ROTATE, fmt::format("updated {} ago", updated));
    int original = m_level->m_originalLevel.value();
    if (original > 0 && original != id) chip(icon::LINK, fmt::format("copy of {}", original));
    button(icon::CIRCLE_INFO, "more info", 0, [this] { this->openGDPage(); });

    y = top + h + INFO_PADDING * k;
    // A thin line between the level's part and the comments.
    auto line = CCLayerColor::create(SEPARATOR);
    line->setContentSize({W, 1.5f * k});
    line->setPosition({0, -y});
    content->addChild(line);
    return y;
}

void CommentsOverlay::openGDPage() {
    // GD's own page for the level: its level info, "original" and other
    // mods' buttons. Marked so the hook (CommentsOverlayHooks.cpp) lets GD show it.
    auto layer = InfoLayer::create(m_level, nullptr, nullptr);
    if (!layer) return;
    layer->setUserObject("vanilla"_spr, CCBool::create(true));
    layer->show();
}

float CommentsOverlay::buildCounter(float y) {
    float k = m_k, h = COUNTER_HEIGHT * k;
    auto content = m_scroll->content();
    float cy = -(y + h / 2);

    // "comments" and, once known, how many in a small dark pill.
    auto title = makeText("comments", Weight::Regular, 20 * k);
    title->setColor(theme::rgb(m_scheme.light1()));
    title->setAnchorPoint({0, 0.5f});
    title->setPosition({m_pad, cy});
    content->addChild(title);

    float x = m_pad + title->getScaledContentSize().width + 5 * k;
    float pillH = 24 * k;
    m_countBg = RoundedBox::create({40 * k, pillH}, pillH / 2, m_scheme.background6());
    m_countBg->setAnchorPoint({0, 0.5f});
    m_countBg->setPosition({x, cy});
    m_countBg->setVisible(false);
    content->addChild(m_countBg);
    m_countLabel = makeText("0", Weight::Bold, 14 * k);
    m_countLabel->setColor(theme::rgb(m_scheme.foreground1()));
    m_countLabel->setAnchorPoint({0, 0.5f});
    m_countLabel->setPosition({x + 10 * k, cy});
    m_countLabel->setVisible(false);
    content->addChild(m_countLabel, 1);
    return y + h;
}

float CommentsOverlay::buildEditor(float y) {
    float k = m_k, W = bodySize().width;
    auto content = m_scroll->content();
    float top = y + EDITOR_PADDING * k;
    float editorH = (TEXTBOX_HEIGHT + EDITOR_FOOTER) * k;

    // Your icon beside the box, where osu! shows your avatar.
    auto gm = GameManager::get();
    float av = EDITOR_AVATAR * k;
    CCPoint avatarAt {m_pad + av / 2, -(top + av / 2)};
    auto tile = RoundedBox::create({av, av}, av / 2, m_scheme.background6());
    tile->setPosition(avatarAt);
    content->addChild(tile);
    auto me = playerIcon(gm->getPlayerFrame(), IconType::Cube, gm->getPlayerColor(), gm->getPlayerColor2(),
                         gm->getPlayerGlowColor(), gm->getPlayerGlow(), av * 0.62f);
    me->setPosition(avatarAt);
    content->addChild(me, 1);

    // The editor: a bordered box with the text box on top and a footer of buttons.
    float ex = m_pad + 60 * k, ew = W - m_pad - ex;
    float border = EDITOR_BORDER * k;
    auto frame = RoundedBox::create({ew, editorH}, 6 * k, m_scheme.background3());
    frame->setAnchorPoint({0, 1});
    frame->setPosition({ex, -top});
    content->addChild(frame);
    float fieldH = TEXTBOX_HEIGHT * k - border;
    auto field = RoundedBox::create({ew - border * 2, fieldH}, 3 * k, m_scheme.background5());
    field->setAnchorPoint({0, 1});
    field->setPosition({ex + border, -(top + border)});
    content->addChild(field);

    // GD's text input (Geode's box is 30 tall: scaled so its text suits the page).
    float scale = k;
    float fieldCy = -(top + border + fieldH / 2);
    m_input = TextInput::create((ew - EDITOR_SIDE * 2 * k) / scale, "type your comment here", "outfit-regular.fnt"_spr);
    // GD's own character filter drops punctuation: allow everything typeable.
    m_input->setCommonFilter(CommonFilter::Any);
    m_input->hideBG();
    m_input->setTextAlign(TextInputAlign::Left);
    m_input->setScale(scale);
    m_input->setAnchorPoint({0, 0.5f});
    m_input->setPosition({ex + EDITOR_SIDE * k, fieldCy});
    m_input->setMaxCharCount(COMMENT_LIMIT);
    m_input->setDelegate(this);
    content->addChild(m_input, 2);

    // Footer: the percent toggle on the left, post (or sign in) on the right.
    float footerCy = -(top + TEXTBOX_HEIGHT * k + EDITOR_FOOTER * k / 2);
    float right = ex + ew - EDITOR_SIDE * k;
    float bw = BUTTON_WIDTH * k, bh = BUTTON_HEIGHT * k;
    auto accent = m_scheme.colour3();
    auto accentHover = theme::lerp(accent, ccColor4B {255, 255, 255, 255}, 0.2f); // RoundedButton lightens on hover
    auto button = [&](std::string const& text, float width, std::function<void()> action) -> size_t {
        auto& pill = addPill(m_fixedPills, content, {width, bh}, 5 * k, {right, footerCy}, {1, 0.5f}, accent, accentHover,
                             std::move(action));
        auto label = makeText(text, Weight::Bold, 12 * k);
        label->setPosition({width / 2, bh / 2});
        pill.node->addChild(label, 1);
        return m_fixedPills.size() - 1;
    };
    m_postPill = button("post", bw, [this] { this->post(); });
    m_signInPill = button("sign in", 100 * k, [this] { this->askSignIn("comment on levels"); });
    m_postSpinner = makeSpinner(18 * k, {255, 255, 255});
    m_postSpinner->setPosition({right - 100 * k - 5 * k - 9 * k, footerCy});
    m_postSpinner->setVisible(false);
    content->addChild(m_postSpinner, 2);

    // GD's percent toggle (ShareCommentLayer's), as a header-style check button.
    if (!m_level->isPlatformer() && m_level->m_normalPercent.value() > 0) {
        auto label = makeText(fmt::format("include my best ({}%)", m_level->m_normalPercent.value()), Weight::SemiBold, 12 * k);
        float box = 10 * k;
        float w = box + 5 * k + label->getScaledContentSize().width + 20 * k;
        auto& pill = addPill(m_fixedPills, content, {w, TAB_HEIGHT * k}, 3 * k, {ex + EDITOR_SIDE * k, footerCy}, {0, 0.5f},
                             CLEAR, m_scheme.background4(), [this] {
            m_includePercent = !m_includePercent;
            sfx::play(m_includePercent ? sfx::sound::CHECK_ON : sfx::sound::CHECK_OFF);
            updateEditor();
        });
        pill.silent = true;
        m_percentPill = m_fixedPills.size() - 1;
        m_percentBox = RoundedBox::create({box, box}, 2 * k, m_scheme.background5());
        m_percentBox->setBorder(1.5f * k, m_scheme.light1());
        m_percentBox->setPosition({10 * k + box / 2, TAB_HEIGHT * k / 2});
        pill.node->addChild(m_percentBox, 1);
        m_percentCheck = makeIcon(icon::CHECK, 7 * k);
        m_percentCheck->setPosition(m_percentBox->getPosition());
        pill.node->addChild(m_percentCheck, 2);
        label->setAnchorPoint({0, 0.5f});
        label->setPosition({10 * k + box + 5 * k, TAB_HEIGHT * k / 2});
        pill.node->addChild(label, 1);
    }
    return y + EDITOR_PADDING * 2 * k + editorH;
}

float CommentsOverlay::buildSortHeader(float y) {
    float k = m_k, W = bodySize().width, h = HEADER_HEIGHT * k;
    auto content = m_scroll->content();
    auto bg = CCLayerColor::create(m_scheme.background4());
    bg->setContentSize({W, h});
    bg->setPosition({0, -(y + h)});
    content->addChild(bg);

    float cy = -(y + h / 2);
    auto sortLabel = makeText("Sort", Weight::SemiBold, 12 * k);
    sortLabel->setAnchorPoint({0, 0.5f});
    sortLabel->setPosition({m_pad, cy});
    content->addChild(sortLabel, 1);

    // TabButton: the background shows while active or hovered, the active
    // one reads bold and in Light1.
    float x = m_pad + sortLabel->getScaledContentSize().width + 10 * k;
    for (auto [name, sort] : {std::pair {"recent", Sort::Recent}, std::pair {"top", Sort::Top}}) {
        auto normal = makeText(name, Weight::SemiBold, 12 * k);
        auto bold = makeText(name, Weight::Bold, 12 * k);
        float w = std::max(normal->getScaledContentSize().width, bold->getScaledContentSize().width) + 20 * k;
        auto& tab = addPill(m_fixedPills, content, {w, TAB_HEIGHT * k}, 3 * k, {x, cy}, {0, 0.5f}, CLEAR,
                            m_scheme.background3(), [this, sort = sort] {
            if (m_sort == sort || m_state == State::Loading) return;
            m_sort = sort;
            reload(false);
        });
        tab.tag = TAG_TAB + static_cast<int>(sort);
        tab.tinted = {normal, bold};
        for (auto label : tab.tinted) {
            label->setPosition({w / 2, TAB_HEIGHT * k / 2});
            tab.node->addChild(label, 1);
        }
        x += w + 5 * k;
    }

    // Refresh, at the right (GD's comments have one; osu! keeps "show deleted" there).
    auto refreshIcon = makeIcon(icon::ROTATE, 10 * k);
    auto refreshLabel = makeText("refresh", Weight::SemiBold, 12 * k);
    float iconW = refreshIcon->getScaledContentSize().width;
    float w = iconW + 5 * k + refreshLabel->getScaledContentSize().width + 20 * k;
    auto& refresh = addPill(m_fixedPills, content, {w, TAB_HEIGHT * k}, 3 * k, {W - m_pad, cy}, {1, 0.5f}, CLEAR,
                            m_scheme.background3(), [this] {
        if (m_state != State::Loading) reload(true);
    });
    refreshIcon->setPosition({10 * k + iconW / 2, TAB_HEIGHT * k / 2});
    refresh.node->addChild(refreshIcon, 1);
    refreshLabel->setAnchorPoint({0, 0.5f});
    refreshLabel->setPosition({10 * k + iconW + 5 * k, TAB_HEIGHT * k / 2});
    refresh.node->addChild(refreshLabel, 1);
    return y + h;
}

void CommentsOverlay::updateEditor() {
    if (m_history) return;
    if (m_fixedPills.size() <= std::max(m_postPill, m_signInPill)) return;
    bool in = loggedIn();
    std::string text = m_input ? trim(std::string(m_input->getString())) : "";
    auto& post = m_fixedPills[m_postPill];
    auto& signIn = m_fixedPills[m_signInPill];
    post.node->setVisible(in);
    signIn.node->setVisible(!in);
    post.enabled = in && !m_posting && !text.empty();
    if (m_postSpinner) m_postSpinner->setVisible(m_posting);
    if (m_input) {
        bool usable = in && !m_posting;
        if (!usable) m_input->defocus();
        m_input->setEnabled(usable);
        static std::string const PLACEHOLDERS[] {"type your comment here", "sign in to comment", "posting..."};
        auto const& placeholder = PLACEHOLDERS[!in ? 1 : m_posting ? 2 : 0];
        if (m_placeholder != placeholder) {
            m_placeholder = placeholder;
            m_input->setPlaceholder(placeholder);
        }
    }
    if (m_percentBox) {
        m_percentBox->setFillColor(m_includePercent ? m_scheme.colour3() : m_scheme.background5());
        m_percentCheck->setVisible(m_includePercent);
    }
}

} // namespace lazer
