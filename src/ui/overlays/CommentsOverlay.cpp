#include "CommentsOverlayInternal.hpp"

#include "../../audio/Sfx.hpp"
#include "../../settings/Account.hpp"

using namespace geode::prelude;

namespace lazer {

namespace {
    bool nodeContains(CCNode* node, CCPoint world) {
        auto local = node->convertToNodeSpace(world);
        auto size = node->getContentSize();
        return local.x >= 0 && local.y >= 0 && local.x <= size.width && local.y <= size.height;
    }

    bool nodeShown(CCNode* node) {
        for (auto n = node; n; n = n->getParent()) {
            if (!n->isVisible()) return false;
        }
        return true;
    }

    // Nothing in a hidden GD layer may take touches: its list, menus and
    // loading circle would otherwise still catch them (invisible) over ours.
    void deafen(CCNode* node) {
        if (auto layer = typeinfo_cast<CCLayer*>(node)) layer->setTouchEnabled(false);
        for (auto child : CCArrayExt<CCNode*>(node->getChildren())) deafen(child);
    }
}

bool CommentsOverlay::wants(InfoLayer* layer) {
    if (!layer || !Mod::get()->getSettingValue<bool>("enabled")) return false;
    // A player's comment history (m_score) and a list's comments keep GD's
    // page; so does a level that isn't online (no comments to show).
    return layer->m_level && !layer->m_score && !layer->m_levelList && layer->m_level->m_levelID.value() > 0;
}

bool CommentsOverlay::present(GJGameLevel* level, InfoLayer* gdLayer) {
    auto scene = CCDirector::get()->getRunningScene();
    if (!scene || !level) return false;
    auto overlay = new CommentsOverlay();
    if (!overlay->init(level)) {
        delete overlay;
        return false;
    }
    overlay->autorelease();
    if (gdLayer) {
        // GD's page, kept (other mods may hold on to it or hook it) but never
        // drawn or touched. It isn't shown, so it never registered for input.
        gdLayer->setUserObject("hidden"_spr, CCBool::create(true));
        deafen(gdLayer);
        if (auto circle = gdLayer->m_loadingCircle) {
            // It may sit in the scene rather than the layer; it never finishes here.
            circle->setTouchEnabled(false);
            circle->setVisible(false);
        }
        gdLayer->setKeypadEnabled(false);
        gdLayer->setKeyboardEnabled(false);
        gdLayer->setVisible(false);
        overlay->addChild(gdLayer, -10);
    }
    // Under GD's own popups (z 105) and our dialogs, over everything else,
    // like the profile page (which opens over this one from a name).
    scene->addChild(overlay, 100);
    overlay->open();
    return true;
}

bool CommentsOverlay::presentHistory(GJUserScore* player) {
    auto scene = CCDirector::get()->getRunningScene();
    if (!scene || !player || player->m_userID <= 0) return false;
    auto overlay = new CommentsOverlay();
    if (!overlay->init(nullptr, player)) { delete overlay; return false; }
    overlay->autorelease();
    scene->addChild(overlay, 101);
    overlay->open();
    return true;
}

bool CommentsOverlay::init(GJGameLevel* level, GJUserScore* player) {
    m_history = player != nullptr;
    m_historyPlayer = player;
    std::string name = m_history ? std::string(player->m_userName) : std::string(level->m_levelName);
    if (name.size() > 30) name = name.substr(0, 28) + "...";
    std::string creator = m_history ? "" : std::string(level->m_creatorName);
    if (creator.empty()) creator = "unknown";
    if (!WaveOverlay::init(0, SCHEME, icon::COMMENTS, m_history ? "comment history" : name,
        m_history ? "level comments by " + name : "level by " + creator, 72.f)) return false;
    m_level = level;
    m_levelID = m_history ? player->m_userID : level->m_levelID.value();
    m_pad = HORIZONTAL_PADDING * m_k;
    m_comments = CCArray::create();
    // GD attaches your best percent to a comment unless you turn that off;
    // platformers have no percent.
    m_includePercent = !m_history && !level->isPlatformer() && level->m_normalPercent.value() > 0;
    m_wasLoggedIn = loggedIn();

    m_scroll = ScrollArea::create(bodySize());
    body()->addChild(m_scroll);

    // The top part is built once; the comments under it are rebuilt as they load.
    float y = m_history ? 0 : buildInfo(0);
    y = buildCounter(y);
    if (!m_history) y = buildEditor(y);
    y = buildSortHeader(y);
    m_listTop = y;
    m_list = CCNode::create();
    m_list->setPosition({0, -m_listTop});
    m_scroll->content()->addChild(m_list);

    updateEditor();
    load(0);
    return true;
}

CommentsOverlay::~CommentsOverlay() {
    auto glm = GameLevelManager::sharedState();
    if (glm->m_levelCommentDelegate == this) glm->m_levelCommentDelegate = nullptr;
    if (glm->m_commentUploadDelegate == this) glm->m_commentUploadDelegate = nullptr;
}

void CommentsOverlay::onEnter() {
    WaveOverlay::onEnter();
    CCDirector::get()->getKeypadDispatcher()->addDelegate(this);
}

void CommentsOverlay::onExit() {
    CCDirector::get()->getKeypadDispatcher()->removeDelegate(this);
    // GD keeps raw pointers to its delegates: a request finishing after the
    // scene changed mustn't call into a dead page.
    auto glm = GameLevelManager::sharedState();
    if (glm->m_levelCommentDelegate == this) glm->m_levelCommentDelegate = nullptr;
    if (glm->m_commentUploadDelegate == this) glm->m_commentUploadDelegate = nullptr;
    WaveOverlay::onExit();
}

void CommentsOverlay::keyBackClicked() {
    close();
}

void CommentsOverlay::onClosed() {
    this->removeFromParent();
}

bool CommentsOverlay::loggedIn() const {
    return account::loggedIn();
}

bool CommentsOverlay::covered() {
    auto parent = this->getParent();
    if (!parent || !parent->getChildren()) return false;
    bool after = false;
    for (auto child : CCArrayExt<CCNode*>(parent->getChildren())) {
        if (child == this) {
            after = true;
            continue;
        }
        if (after && child->isVisible() && typeinfo_cast<WaveOverlay*>(child)) return true;
    }
    return false;
}

// --- per frame and input ---

void CommentsOverlay::onUpdate(float dt) {
    float ms = dt * 1000.f;
    if (m_copiedMs > 0) {
        m_copiedMs -= ms;
        if (m_copiedMs <= 0 && m_idLabel) m_idLabel->setString(m_idText.c_str());
    }
    if (m_state == State::Loading) {
        m_loadingMs += ms;
        if (m_loadingMs > LOAD_TIMEOUT_MS) {
            m_key.clear();
            m_state = State::Failed;
            m_dirty = true;
        }
    }
    // Signing in (from the dialog) turns the box on.
    bool in = loggedIn();
    if (in != m_wasLoggedIn) {
        m_wasLoggedIn = in;
        updateEditor();
    }
    if (m_dirty) rebuild();

    for (auto spinner : m_spinners) spinner->setRotation(spinner->getRotation() + dt * SPIN_SPEED);
    if (m_postSpinner && m_postSpinner->isVisible()) m_postSpinner->setRotation(m_postSpinner->getRotation() + dt * SPIN_SPEED);

    auto mouse = geode::cocos::getMousePos();
    bool interactive = isOpen() && !covered() && !popupOnTop() && !m_drag.dragging() && m_scroll->containsWorldPoint(mouse);
    for (auto list : {&m_fixedPills, &m_pills}) {
        for (auto& p : *list) {
            bool hovered = interactive && p.action && p.enabled && nodeShown(p.node) && nodeContains(p.node, mouse);
            if (hovered && !p.hovered) sfx::hover(sfx::sound::DEFAULT_HOVER);
            p.hovered = hovered;
            if (p.tag >= TAG_TAB && p.tag < TAG_TAB + 2) {
                // TabButton.UpdateState
                bool active = p.tag - TAG_TAB == static_cast<int>(m_sort);
                p.bg->setFillColor(active || hovered ? p.hoverColor : p.color);
                auto colour = active && !hovered ? theme::rgb(m_scheme.light1()) : ccColor3B {255, 255, 255};
                p.tinted[0]->setVisible(!active);
                p.tinted[1]->setVisible(active);
                for (auto label : p.tinted) label->setColor(colour);
                continue;
            }
            if (p.bg) {
                auto colour = hovered ? p.hoverColor : p.color;
                if (!p.enabled) colour = theme::lerp(p.color, ccColor4B {40, 40, 40, 255}, 0.6f); // greyed, like ButtonRow
                p.bg->setFillColor(colour);
            }
            for (auto label : p.tinted) label->setColor(hovered ? p.textHover : p.textColor);
        }
    }
}

CommentsOverlay::Pill* CommentsOverlay::pillAt(CCPoint world) {
    if (!m_scroll->containsWorldPoint(world)) return nullptr;
    Pill* hit = nullptr;
    for (auto list : {&m_fixedPills, &m_pills}) {
        for (auto& p : *list) {
            if (p.action && p.enabled && nodeShown(p.node) && nodeContains(p.node, world)) hit = &p;
        }
    }
    return hit;
}

bool CommentsOverlay::ccTouchBegan(CCTouch* touch, CCEvent* e) {
    auto loc = touch->getLocation();
    // The post box takes its own touches (its input node registers below us).
    if (isOpen() && !covered() && m_input && nodeShown(m_input) && m_scroll->containsWorldPoint(loc)
        && nodeContains(m_input, loc)) {
        return false;
    }
    if (!WaveOverlay::ccTouchBegan(touch, e)) return false;
    // A tap anywhere else leaves the box (it only ever sees its own touches).
    if (m_input) m_input->defocus();
    m_pressed = pillAt(loc);
    m_drag.began(m_scroll, loc);
    return true;
}

void CommentsOverlay::ccTouchMoved(CCTouch* touch, CCEvent*) {
    if (m_drag.moved(touch->getLocation())) m_pressed = nullptr;
}

void CommentsOverlay::ccTouchEnded(CCTouch* touch, CCEvent* e) {
    WaveOverlay::ccTouchEnded(touch, e);
    m_drag.ended();
    auto pressed = m_pressed;
    m_pressed = nullptr;
    if (!pressed || !pressed->action || !pressed->enabled || !nodeContains(pressed->node, touch->getLocation())) return;
    if (!pressed->silent) sfx::click(sfx::sound::DEFAULT_SELECT);
    auto action = pressed->action; // may rebuild the list, and the pill with it
    action();
}

void CommentsOverlay::textChanged(CCTextInputNode*) {
    updateEditor();
}

void CommentsOverlay::enterPressed(CCTextInputNode*) {
    post();
}

} // namespace lazer
