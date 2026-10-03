#include "CommentsOverlayInternal.hpp"

#include <algorithm>

using namespace geode::prelude;

namespace lazer {

namespace {
    std::string withCommas(long long v) {
        auto s = fmt::format("{}", v);
        for (int i = int(s.size()) - 3; i > (s[0] == '-' ? 1 : 0); i -= 3) s.insert(size_t(i), ",");
        return s;
    }
}

// --- the comments and the footer (rebuilt as they load) ---

void CommentsOverlay::rebuild() {
    float k = m_k;
    m_list->removeAllChildren();
    m_pills.clear();
    m_spinners.clear();
    m_pressed = nullptr;
    m_dirty = false;

    // The counter, once the server has said how many.
    bool known = m_total >= 0;
    m_countBg->setVisible(known);
    m_countLabel->setVisible(known);
    if (known) {
        m_countLabel->setString(withCommas(m_total).c_str());
        auto size = m_countBg->getContentSize();
        m_countBg->setContentSize({m_countLabel->getScaledContentSize().width + 20 * k, size.height});
    }

    float y = 0;
    auto note = [&](std::string const& text) {
        float h = PLACEHOLDER_HEIGHT * k;
        auto label = makeText(text, Weight::Regular, 16 * k);
        label->setColor(theme::rgb(m_scheme.content2()));
        label->setAnchorPoint({0, 0.5f});
        label->setPosition({m_pad, -(y + h / 2)});
        m_list->addChild(label);
        y += h;
    };
    for (auto comment : CCArrayExt<GJComment*>(m_comments)) y = buildComment(comment, y);
    if (m_state == State::Loaded && m_comments->count() == 0) note("No comments yet.");
    if (m_state == State::Failed) note(m_comments->count() == 0 ? "Couldn't load the comments." : "Couldn't load more comments.");
    y = buildFooter(y);

    m_scroll->setContentHeight(m_listTop + y);
    m_scroll->claimWheel();
}

float CommentsOverlay::buildComment(GJComment* comment, float y) {
    float k = m_k, W = bodySize().width;
    auto glm = GameLevelManager::sharedState();
    auto content = m_list;
    // Like GD's CommentCell: the author's name is in their user score, and a
    // missing account ID comes from the user IDs GD has seen (kept on the comment).
    auto authorScore = comment->m_userScore ? comment->m_userScore : m_historyPlayer.data();
    std::string author = authorScore ? std::string(authorScore->m_userName) : "";
    if (author.empty()) author = glm->userNameForUserID(comment->m_userID);
    if (comment->m_accountID <= 0 && authorScore) comment->m_accountID = authorScore->m_accountID;
    if (comment->m_accountID <= 0) comment->m_accountID = glm->accountIDForUserID(comment->m_userID);
    int me = GJAccountManager::get()->m_accountID;
    bool own = me > 0 && comment->m_accountID == me;
    bool hidden = comment->m_isSpam || comment->m_commentDeleted;
    float top = y;
    y += COMMENT_PADDING * k;

    // Their icon, in a circle.
    float av = AVATAR * k;
    CCPoint avatarAt {m_pad + av / 2, -(y + av / 2)};
    auto tile = RoundedBox::create({av, av}, av / 2, m_scheme.background6());
    tile->setPosition(avatarAt);
    content->addChild(tile);
    if (auto s = authorScore) {
        auto icon = playerIcon(s->m_iconID, s->m_iconType, s->m_color1, s->m_color2, s->m_color3, s->m_glowEnabled, av * 0.62f);
        icon->setPosition(avatarAt);
        content->addChild(icon, 1);
    }

    // Author line (CommentAuthorLine): the name opens their profile, then
    // badges, and "spam" / "deleted" for hidden ones.
    float textX = m_pad + av + 10 * k;
    float textW = W - m_pad - textX;
    float lineCy = -(y + 8 * k);
    float x = textX;
    auto name = makeText(author, Weight::Bold, 14 * k);
    name->setAnchorPoint({0, 0.5f});
    name->setPosition({x, lineCy});
    float nameW = name->getScaledContentSize().width;
    if (nameW > textW * 0.6f) name->setScale(name->getScale() * textW * 0.6f / nameW);
    content->addChild(name, 1);
    if (comment->m_accountID > 0) {
        Pill link;
        link.node = name;
        link.tinted = {name};
        link.textHover = theme::rgb(m_scheme.light1());
        int accountID = comment->m_accountID;
        link.action = [accountID, me] { ProfilePage::create(accountID, accountID == me)->show(); };
        m_pills.push_back(std::move(link));
    }
    x += name->getScaledContentSize().width + 4 * k;
    if (comment->m_modBadge > 0 && comment->m_modBadge <= 3) {
        auto frame = fmt::format("modBadge_0{}_001.png", comment->m_modBadge);
        if (CCSpriteFrameCache::sharedSpriteFrameCache()->spriteFrameByName(frame.c_str())) {
            auto badge = CCSprite::createWithSpriteFrameName(frame.c_str());
            auto s = badge->getContentSize();
            badge->setScale(14 * k / std::max(1.f, std::max(s.width, s.height)));
            float bw = badge->getScaledContentSize().width;
            badge->setPosition({x + bw / 2, lineCy});
            content->addChild(badge, 1);
            x += bw + 4 * k;
        }
    }
    // OwnerTitleBadge: a small pill with tiny bold text.
    auto badge = [&](std::string const& text, ccColor4B fill, ccColor3B textColor) {
        auto label = makeText(text, Weight::Bold, 10 * k);
        label->setColor(textColor);
        float w = label->getScaledContentSize().width + 10 * k, h = 14 * k;
        auto box = RoundedBox::create({w, h}, h / 2, fill);
        box->setAnchorPoint({0, 0.5f});
        box->setPosition({x, lineCy});
        content->addChild(box, 1);
        label->setPosition({w / 2, h / 2});
        box->addChild(label);
        x += w + 4 * k;
    };
    // The level's creator (osu!'s "mapper"), and the percent GD attached.
    if (m_level && comment->m_accountID > 0 && comment->m_accountID == m_level->m_accountID.value()) {
        badge("creator", m_scheme.light1(), theme::rgb(m_scheme.background6()));
    }
    if (comment->m_percentage > 0) badge(fmt::format("{}%", comment->m_percentage), m_scheme.colour3(), {255, 255, 255});
    if (m_history && comment->m_levelID > 0) badge(fmt::format("level #{}", comment->m_levelID), m_scheme.background4(), theme::rgb(m_scheme.content2()));
    if (hidden) {
        auto label = makeText(comment->m_commentDeleted ? "deleted" : "spam", Weight::Bold, 14 * k);
        label->setColor(MUTED);
        label->setAnchorPoint({0, 0.5f});
        label->setPosition({x, lineCy});
        content->addChild(label, 1);
    }
    y += 20 * k;

    // The comment, in GD's colour for it (mods' are tinted).
    std::string text = comment->m_commentString;
    auto message = makeWrappedText(text.empty() ? " " : text, 14 * k, textW, hidden ? MUTED : comment->m_color);
    message->setPosition({textX, -y});
    content->addChild(message, 1);
    y += message->getContentSize().height + 4 * k;

    // When, in GD's words ("3 days").
    std::string when = comment->m_uploadDate;
    if (!when.empty()) {
        auto date = makeText(when + " ago", Weight::Regular, 12 * k);
        date->setColor(theme::rgb(m_scheme.foreground1()));
        date->setAnchorPoint({0, 0.5f});
        date->setPosition({textX, -(y + 7 * k)});
        content->addChild(date, 1);
    }
    y += 14 * k;
    y = std::max(y, top + COMMENT_PADDING * k + av);
    y += COMMENT_PADDING * k;

    // Votes, in the margin left of the icon (VotePill): "+N", green once you
    // liked it, and a thumbs down under it. Not on your own, and only once.
    {
        int id = comment->m_commentID;
        int levelID = m_history ? comment->m_levelID : m_levelID;
        int voted = 0; // 1 liked, -1 disliked
        if (auto it = m_votes.find(id); it != m_votes.end()) voted = it->second ? 1 : -1;
        else if (glm->hasLikedItem(LikeItemType::Comment, id, true, levelID)) voted = 1;
        else if (glm->hasLikedItem(LikeItemType::Comment, id, false, levelID)) voted = -1;
        bool canVote = !own && voted == 0 && !hidden;
        Ref<GJComment> ref = comment;

        int count = comment->m_likeCount;
        auto countText = makeText(count < 0 ? fmt::format("{}", count) : fmt::format("+{}", count), Weight::Regular, 14 * k);
        float h = VOTE_HEIGHT * k, w = countText->getScaledContentSize().width + 20 * k;
        float right = m_pad - 5 * k;
        auto fill = voted == 1 ? GREEN_LIGHT : m_scheme.background6();
        if (own) fill.a = 0; // your own: just the number
        std::function<void()> like;
        if (canVote) like = [this, ref] { this->vote(ref.data(), true); };
        auto& up = addPill(m_pills, content, {w, h}, h / 2, {std::max(2 * k, right - w), avatarAt.y}, {0, 0.5f}, fill,
                           theme::lerp(fill, ccColor4B {255, 255, 255, 255}, 0.15f), std::move(like));
        countText->setPosition({w / 2, h / 2});
        up.node->addChild(countText, 1);

        if (!own) {
            auto downFill = voted == -1 ? DANGER : m_scheme.background6();
            std::function<void()> dislike;
            if (canVote) dislike = [this, ref] { this->vote(ref.data(), false); };
            auto& down = addPill(m_pills, content, {h, h}, h / 2, {right - h, avatarAt.y - h - 4 * k}, {0, 0.5f}, downFill,
                                 theme::lerp(downFill, ccColor4B {255, 255, 255, 255}, 0.15f), std::move(dislike));
            auto thumb = makeIcon(icon::THUMBS_DOWN, 10 * k);
            thumb->setPosition({h / 2, h / 2});
            down.node->addChild(thumb, 1);
        }
    }

    // A thin line under each comment.
    auto line = CCLayerColor::create(SEPARATOR);
    line->setContentSize({W, 1.5f * k});
    line->setPosition({0, -y});
    content->addChild(line);
    return y;
}

float CommentsOverlay::buildFooter(float y) {
    float k = m_k, W = bodySize().width;
    bool loading = m_state == State::Loading;
    bool failed = m_state == State::Failed;
    int loaded = static_cast<int>(m_comments->count());
    bool more = m_total < 0 || loaded < m_total;
    // Nothing more to load: osu! hides the button.
    if (!loading && !failed && !more) return y + 20 * k;

    // ShowMoreButton: a chevron each side of "SHOW MORE (N)"; only a spinner while loading.
    y += 10 * k;
    std::string text = failed ? "TRY AGAIN" : m_total >= 0 ? fmt::format("SHOW MORE ({})", m_total - loaded) : "SHOW MORE";
    auto label = makeText(text, Weight::Bold, 12 * k);
    auto left = makeIcon(icon::CHEVRON_DOWN, 7.5f * k);
    auto right = makeIcon(icon::CHEVRON_DOWN, 7.5f * k);
    float chevronW = left->getScaledContentSize().width;
    float w = std::max(120 * k, label->getScaledContentSize().width + (chevronW + 10 * k) * 2 + 40 * k);
    float h = 24 * k;
    std::function<void()> action;
    if (!loading) action = [this] { this->load(m_page); }; // after a failure m_page is still the page that failed
    auto& pill = addPill(m_pills, m_list, {w, h}, h / 2, {W / 2, -(y + h / 2)}, {0.5f, 0.5f}, m_scheme.background2(),
                         m_scheme.background1(), std::move(action));
    if (loading) {
        auto spinner = makeSpinner(12 * k, {255, 255, 255});
        spinner->setPosition({w / 2, h / 2});
        pill.node->addChild(spinner, 1);
        m_spinners.push_back(spinner);
    } else {
        label->setPosition({w / 2, h / 2});
        pill.node->addChild(label, 1);
        left->setPosition({20 * k + chevronW / 2, h / 2});
        right->setPosition({w - 20 * k - chevronW / 2, h / 2});
        pill.node->addChild(left, 1);
        pill.node->addChild(right, 1);
        pill.tinted = {left, right};
        pill.textColor = theme::rgb(m_scheme.foreground1());
        pill.textHover = theme::rgb(m_scheme.light1());
    }
    y += h + 10 * k;
    return y + 20 * k;
}

} // namespace lazer
