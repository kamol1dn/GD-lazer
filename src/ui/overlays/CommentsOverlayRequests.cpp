#include "CommentsOverlayInternal.hpp"

#include "../../settings/Account.hpp"
#include "Dialog.hpp"

#include <algorithm>

using namespace geode::prelude;

namespace lazer {

// --- loading, posting, voting (GameLevelManager) ---

void CommentsOverlay::load(int page) {
    auto glm = GameLevelManager::sharedState();
    m_state = State::Loading;
    m_loadingMs = 0;
    m_pendingTotal = -1;
    m_page = page;
    m_key = std::string(glm->getCommentKey(m_levelID, page, static_cast<int>(m_sort), commentType()));
    glm->m_levelCommentDelegate = this;
    // `total` is the count GD already knows, so the server can skip counting again.
    glm->getLevelComments(m_levelID, page, std::max(0, m_total), static_cast<int>(m_sort), commentType());
    rebuild();
}

void CommentsOverlay::reload(bool resetCache) {
    // GD keeps comment pages for a while; a refresh (and a new comment) wants fresh ones.
    if (resetCache) GameLevelManager::sharedState()->resetCommentTimersForLevelID(m_levelID, commentType());
    m_comments->removeAllObjects();
    m_total = -1;
    load(0);
}

void CommentsOverlay::setupPageInfo(gd::string info, char const* key) {
    if (!key) return;
    // "total:start:count" (GameLevelManager::createPageInfo). GD sends it with
    // the page; whether before or after it, the count lands.
    auto parts = utils::string::split(std::string(info), ":");
    int total = parts.empty() ? -1 : utils::numFromString<int>(parts[0]).unwrapOr(-1);
    if (total < 0) return;
    if (m_key == key) {
        m_pendingTotal = total;
    } else if (m_lastKey == key) {
        m_total = total;
        m_dirty = true;
    }
}

void CommentsOverlay::loadCommentsFinished(CCArray* comments, char const* key) {
    // Only the page asked for: a stale request (a sort switched mid-load) or
    // a profile's posts (the same delegate slot) are someone else's.
    if (!key || m_key != key) return;
    auto glm = GameLevelManager::sharedState();
    if (glm->m_levelCommentDelegate == this) glm->m_levelCommentDelegate = nullptr;
    m_lastKey = m_key;
    m_key.clear();

    int added = 0;
    if (comments) {
        for (auto comment : CCArrayExt<GJComment*>(comments)) {
            bool seen = false;
            for (auto shown : CCArrayExt<GJComment*>(m_comments)) {
                if (shown->m_commentID == comment->m_commentID) { seen = true; break; }
            }
            if (seen) continue;
            m_comments->addObject(comment);
            added++;
        }
    }
    if (m_pendingTotal >= 0) m_total = m_pendingTotal;
    else if (added == 0) m_total = static_cast<int>(m_comments->count()); // no page info and nothing new: that's all of them
    m_page++;
    m_state = State::Loaded;
    rebuild();
}

void CommentsOverlay::loadCommentsFailed(char const* key) {
    if (!key || m_key != key) return;
    auto glm = GameLevelManager::sharedState();
    if (glm->m_levelCommentDelegate == this) glm->m_levelCommentDelegate = nullptr;
    m_key.clear();
    m_state = State::Failed;
    rebuild();
}

void CommentsOverlay::post() {
    if (m_history) return;
    if (m_posting) return;
    if (!loggedIn()) return askSignIn("comment on levels");
    std::string text = m_input ? trim(std::string(m_input->getString())) : "";
    if (text.empty()) return;
    if (text.size() > static_cast<size_t>(COMMENT_LIMIT)) text = text.substr(0, COMMENT_LIMIT);
    m_posting = true;
    if (m_input) m_input->defocus();
    auto glm = GameLevelManager::sharedState();
    glm->m_commentUploadDelegate = this;
    int percent = m_includePercent ? m_level->m_normalPercent.value() : 0;
    glm->uploadLevelComment(m_levelID, text, percent);
    updateEditor();
}

void CommentsOverlay::commentUploadFinished(int) {
    auto glm = GameLevelManager::sharedState();
    if (glm->m_commentUploadDelegate == this) glm->m_commentUploadDelegate = nullptr;
    m_posting = false;
    if (m_input) m_input->setString("");
    // GD doesn't hand the new comment back: show the newest, where it is.
    m_sort = Sort::Recent;
    reload(true);
    updateEditor();
}

void CommentsOverlay::commentUploadFailed(int, CommentError error) {
    auto glm = GameLevelManager::sharedState();
    if (glm->m_commentUploadDelegate == this) glm->m_commentUploadDelegate = nullptr;
    m_posting = false;
    updateEditor();
    Dialog::show(icon::TRIANGLE_EXCLAMATION, "Couldn't post your comment",
        error == CommentError::Banned
            ? "Your account isn't allowed to comment."
            : "GD didn't take it. It may be too soon after your last comment: try again in a bit.",
        {{"OK", Dialog::Kind::Cancel, nullptr}});
}

void CommentsOverlay::vote(GJComment* comment, bool like) {
    if (!comment) return;
    if (!loggedIn()) return askSignIn("vote on comments");
    int id = comment->m_commentID;
    int levelID = m_history ? comment->m_levelID : m_levelID;
    auto glm = GameLevelManager::sharedState();
    // One vote per comment (GD remembers yours on this device).
    if (m_votes.contains(id) || glm->hasLikedItem(LikeItemType::Comment, id, true, levelID)
        || glm->hasLikedItem(LikeItemType::Comment, id, false, levelID)) return;
    // GD's LikeItemLayer, without the popup.
    glm->likeItem(LikeItemType::Comment, id, like, levelID);
    m_votes[id] = like;
    // Count it right away, like GD's comment cells do.
    comment->m_likeCount += like ? 1 : -1;
    float scroll = m_scroll->scroll();
    rebuild();
    m_scroll->scrollTo(scroll, false);
}

void CommentsOverlay::askSignIn(char const* what) {
    Dialog::show(icon::USER, "Sign in first", fmt::format("You need a GD account to {}.", what), {
        {"Sign in", Dialog::Kind::Ok, [] { account::logIn(); }},
        {"Not now", Dialog::Kind::Cancel, nullptr},
    });
}

} // namespace lazer
