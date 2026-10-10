#include "TimelyLevels.hpp"

#include <Geode/modify/DailyLevelPage.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <functional>
#include <string>

using namespace geode::prelude;

namespace lazer::timely {

namespace {
    // No answer from the server (or the download) by then: say so.
    constexpr double LOADING_TIMEOUT_S = 20.0;

    struct Slot {
        Ref<DailyLevelPage> page;
        double loadingSince = -1;
    };
    std::array<Slot, 3> g_slots;

    Slot& slot(GJTimedLevelType type) {
        return g_slots[std::clamp(static_cast<int>(type), 0, 2)];
    }

    double now() {
        using namespace std::chrono;
        return duration<double>(steady_clock::now().time_since_epoch()).count();
    }

    // The claim button of GD's node: the menu item wired to its onClaimReward.
    CCMenuItem* claimButton(DailyLevelNode* node) {
        if (!node) return nullptr;
        auto target = static_cast<SEL_MenuHandler>(&DailyLevelNode::onClaimReward);
        std::function<CCMenuItem*(CCNode*)> find = [&](CCNode* parent) -> CCMenuItem* {
            for (auto child : CCArrayExt<CCNode*>(parent->getChildren())) {
                if (auto item = typeinfo_cast<CCMenuItem*>(child); item && item->m_pfnSelector == target) return item;
                if (auto found = find(child)) return found;
            }
            return nullptr;
        };
        return find(node);
    }

    bool shown(CCNode* node, CCNode* upTo) {
        for (auto n = node; n && n != upTo; n = n->getParent()) {
            if (!n->isVisible()) return false;
        }
        return node != nullptr;
    }

    // GD keeps the timer as "now plus what's left", but which "now" differs
    // between its builds (the full Unix time, or the low bits of it the way
    // its page counts): whichever leaves a sane amount is the one it used.
    int secondsLeft(GJTimedLevelType type) {
        auto glm = GameLevelManager::sharedState();
        double timer = glm->getDailyTimer(type);
        if (timer <= 0) return 0;
        using namespace std::chrono;
        auto ms = duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
        double full = ms / 1000.0;
        double masked = ((ms / 1000 & 0xfffff) * 1000 + ms % 1000) / 1000.0;
        constexpr double SANE = 400.0 * 86400;
        for (double nowValue : {full, masked}) {
            double left = timer - nowValue;
            if (left >= 0 && left < SANE) return static_cast<int>(left);
        }
        return 0;
    }

    void makePage(Slot& s, GJTimedLevelType type) {
        auto page = DailyLevelPage::create(type);
        if (!page) return;
        // Never shown or touched: GD's popup registers touches at a forced
        // priority (see the hook below) and would take Escape.
        page->setUserObject("hidden"_spr, CCBool::create(true));
        page->setTouchEnabled(false);
        page->setKeypadEnabled(false);
        page->setVisible(false);
        s.page = page;
        s.loadingSince = now();
        log::debug("Timely: made GD's {} page", static_cast<int>(type));
    }
}

void attach(GJTimedLevelType type, CCNode* host) {
    auto& s = slot(type);
    if (!host) return;
    if (!s.page) makePage(s, type);
    if (!s.page) return;
    auto page = s.page.data();
    if (page->getParent() != host) {
        page->removeFromParentAndCleanup(false);
        host->addChild(page, -1000);
    }
    // GD wires a page up as the server's delegate when it's made: another
    // page since (GD's own, or another type's) would have taken over.
    auto glm = GameLevelManager::sharedState();
    glm->m_GJDailyLevelDelegate = page;
    if (page->m_dailyNode) {
        // Back from a play: the node is rebuilt from its level, so what it
        // shows (the claim button) is current.
        page->refreshDailyPage();
    } else if (!page->m_gettingDailyStatus && !page->m_downloadStarted) {
        // A request another page cut off: asked again.
        s.loadingSince = now();
        page->tryGetDailyStatus();
    }
}

void detach(GJTimedLevelType type) {
    auto& s = slot(type);
    if (s.page) s.page->removeFromParentAndCleanup(false);
}

Status status(GJTimedLevelType type) {
    Status st;
    auto& s = slot(type);
    auto page = s.page.data();
    auto glm = GameLevelManager::sharedState();
    st.dailyID = glm->getDailyID(type);
    st.activeID = glm->getActiveDailyID(type);
    if (!page) {
        st.state = State::Failed;
        return st;
    }
    st.secondsLeft = secondsLeft(type);
    auto node = page->m_dailyNode;
    if (node && node->m_level) {
        st.state = State::Ready;
        st.level = node->m_level;
        st.dailyID = node->m_level->m_dailyID.value() > 0 ? node->m_level->m_dailyID.value() : st.dailyID;
        // GD files a download of the level as a new object: that one has the
        // data and the progress, while the node keeps the old one.
        if (auto saved = glm->getSavedDailyLevel(st.dailyID); saved && saved != st.level && !std::string(saved->m_levelString).empty()) {
            st.level = saved;
        }
        st.downloading = std::string(st.level->m_levelString).empty();
        st.completed = GameStatsManager::sharedState()->hasCompletedDailyLevel(st.dailyID);
        auto claim = claimButton(node);
        st.claimable = claim && shown(claim, page);
        st.skippable = node->m_skipButton && shown(node->m_skipButton, page);
        s.loadingSince = -1;
    } else if (page->m_gettingDailyStatus || page->m_downloadStarted) {
        st.state = State::Loading;
        st.downloading = page->m_downloadStarted;
        if (s.loadingSince < 0) s.loadingSince = now();
        if (now() - s.loadingSince > LOADING_TIMEOUT_S) st.state = State::Failed;
    } else if (!glm->hasDailyStateBeenLoaded(type)) {
        st.state = State::Failed;
    } else if (st.dailyID > 0 && glm->getSavedDailyLevel(st.dailyID) == nullptr && s.loadingSince >= 0 && now() - s.loadingSince < 2.0) {
        // Between the status and the download (GD starts it on the next frame).
        st.state = State::Loading;
    } else if (st.dailyID > 0 && glm->getSavedDailyLevel(st.dailyID) == nullptr) {
        st.state = State::Failed;
    } else {
        // GD has the level but shows no node for it: its reward was claimed
        // (the box goes once it is), and the next one isn't set yet.
        st.state = State::Waiting;
    }
    st.version = static_cast<unsigned>(st.state) * 7919u
        ^ static_cast<unsigned>(reinterpret_cast<uintptr_t>(st.level)) * 31u
        ^ static_cast<unsigned>(st.dailyID) * 131u
        ^ static_cast<unsigned>(st.activeID) * 17u
        ^ (st.completed ? 0x100u : 0u) ^ (st.claimable ? 0x200u : 0u) ^ (st.skippable ? 0x400u : 0u)
        ^ (st.downloading ? 0x800u : 0u);
    return st;
}

void refresh(GJTimedLevelType type) {
    auto& s = slot(type);
    if (!s.page) return;
    if (s.page->m_dailyNode) s.page->refreshDailyPage();
    else if (!s.page->m_gettingDailyStatus && !s.page->m_downloadStarted) {
        s.loadingSince = now();
        s.page->tryGetDailyStatus();
    }
}

void retry(GJTimedLevelType type) {
    auto& s = slot(type);
    if (!s.page) return;
    auto page = s.page.data();
    // GD's page gives up after a failure; a fresh one asks again from the start.
    auto host = page->getParent();
    page->removeFromParentAndCleanup(true);
    s.page = nullptr;
    GameLevelManager::sharedState()->resetDailyLevelState(type);
    if (host) attach(type, host);
}

void claim(GJTimedLevelType type) {
    auto& s = slot(type);
    if (!s.page || !s.page->m_dailyNode) return;
    auto node = s.page->m_dailyNode;
    if (auto button = claimButton(node)) {
        log::info("Timely: claiming the {} reward", static_cast<int>(type));
        button->activate();
    }
}

void skip(GJTimedLevelType type) {
    auto& s = slot(type);
    if (!s.page || !s.page->m_dailyNode) return;
    auto node = s.page->m_dailyNode;
    if (!node->m_level) return;
    log::info("Timely: skipping the {} level", static_cast<int>(type));
    s.loadingSince = now();
    s.page->skipDailyLevel(node, node->m_level);
}

std::string timeLeft(int seconds) {
    seconds = std::max(0, seconds);
    int days = seconds / 86400;
    int hours = seconds / 3600 % 24;
    int minutes = seconds / 60 % 60;
    int secs = seconds % 60;
    if (days > 0) return fmt::format("{}d {}h {:02}m", days, hours, minutes);
    if (hours > 0) return fmt::format("{}h {:02}m {:02}s", hours, minutes, secs);
    return fmt::format("{}m {:02}s", minutes, secs);
}

} // namespace lazer::timely

// Our hidden pages must never grab touches (FLAlertLayer registers at a very
// high priority and swallows everything).
class $modify(LazerHiddenDailyPage, DailyLevelPage) {
    void registerWithTouchDispatcher() {
        if (this->getUserObject("hidden"_spr)) return;
        DailyLevelPage::registerWithTouchDispatcher();
    }
};
