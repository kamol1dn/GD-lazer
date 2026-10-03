#pragma once

#include "../core/ScrollArea.hpp"
#include "WaveOverlay.hpp"

#include <Geode/Geode.hpp>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

namespace lazer {

// A level's comments as an osu!-style page (osu.Game/Overlays/Comments/
// CommentsContainer, as shown in the beatmap overlay): how many there are,
// a box to post your own, sort tabs, the comments themselves with osu!'s
// vote pill (plus a dislike pill, since GD has those), and a "show more"
// button for the next page.
//
// Above them, what GD's InfoLayer shows besides the comments: the level's
// description, ID and dates, and a button to GD's own page (its extra info
// and other mods' buttons live there).
//
// GD's own GameLevelManager loads, posts and likes: this page is its
// LevelCommentDelegate / CommentUploadDelegate while it's open. GD keeps raw
// pointers to those, so they're cleared on exit and destruction.
//
// Every InfoLayer GD opens for a level is routed here (see the hook in
// CommentsOverlayHooks.cpp); profile comments and level lists keep GD's.
class CommentsOverlay : public WaveOverlay, public cocos2d::CCKeypadDelegate,
                        public LevelCommentDelegate, public CommentUploadDelegate, public TextInputDelegate {
public:
    // Opens over the running scene, for a saved online level. `gdLayer` is
    // GD's InfoLayer this stands in for (created, not shown): it's kept
    // hidden inside the page for as long as the page is open.
    static bool present(GJGameLevel* level, InfoLayer* gdLayer = nullptr);
    static bool presentHistory(GJUserScore* player);
    // Whether this GD InfoLayer is a level's comments, which this page shows.
    static bool wants(InfoLayer* layer);

    ~CommentsOverlay() override;
    bool ccTouchBegan(cocos2d::CCTouch* touch, cocos2d::CCEvent* e) override;
    void ccTouchMoved(cocos2d::CCTouch* touch, cocos2d::CCEvent* e) override;
    void ccTouchEnded(cocos2d::CCTouch* touch, cocos2d::CCEvent* e) override;
    void keyBackClicked() override;
    void onEnter() override;
    void onExit() override;

    // LevelCommentDelegate (GameLevelManager::getLevelComments).
    void loadCommentsFinished(cocos2d::CCArray* comments, char const* key) override;
    void loadCommentsFailed(char const* key) override;
    void updateUserScoreFinished() override {}
    void setupPageInfo(gd::string info, char const* key) override;
    // CommentUploadDelegate (GameLevelManager::uploadLevelComment).
    void commentUploadFinished(int parentID) override;
    void commentUploadFailed(int parentID, CommentError error) override;
    void commentDeleteFailed(int id, int parentID) override {}
    // TextInputDelegate (the post box).
    void textChanged(CCTextInputNode* node) override;
    void enterPressed(CCTextInputNode* node) override;

protected:
    // GD's comment modes: 0 most recent, 1 most liked (osu!'s New / Top).
    enum class Sort { Recent = 0, Top = 1 };
    enum class State { Loading, Loaded, Failed };

    // A tappable thing in the scroll content: a rounded button, a tab, a
    // vote pill or a bare link.
    struct Pill {
        cocos2d::CCNode* node = nullptr;
        RoundedBox* bg = nullptr;              // null for links
        cocos2d::ccColor4B color {};           // resting fill
        cocos2d::ccColor4B hoverColor {};
        std::function<void()> action;          // none: just a label
        std::vector<cocos2d::CCLabelBMFont*> tinted; // recoloured on hover
        cocos2d::ccColor3B textColor {255, 255, 255};
        cocos2d::ccColor3B textHover {255, 255, 255};
        int tag = 0;
        bool enabled = true;
        bool silent = false;                   // plays its own sound
        bool hovered = false;
    };

    bool init(GJGameLevel* level, GJUserScore* player = nullptr);
    void onUpdate(float dt) override;
    void onClosed() override;

    // Requests page `page` in the current sort (appending to what's shown).
    void load(int page);
    // Drops what's shown and loads the first page again.
    void reload(bool resetCache);
    void post();
    void vote(GJComment* comment, bool like);
    void askSignIn(char const* what);
    // Rebuilds the comments and the footer (the top part stays).
    void rebuild();
    // The description, ID, dates and GD's page (what InfoLayer shows besides comments).
    float buildInfo(float y);
    // GD's own InfoLayer for this level, over the page.
    void openGDPage();
    float buildCounter(float y);
    float buildEditor(float y);
    float buildSortHeader(float y);
    float buildComment(GJComment* comment, float y);
    float buildFooter(float y);
    // Re-reads the login state and the typed text into the post box.
    void updateEditor();
    Pill& addPill(std::vector<Pill>& list, cocos2d::CCNode* parent, cocos2d::CCSize size, float radius,
                  cocos2d::CCPoint pos, cocos2d::CCPoint anchor, cocos2d::ccColor4B color,
                  cocos2d::ccColor4B hoverColor, std::function<void()> action);
    Pill* pillAt(cocos2d::CCPoint world);
    // Another overlay (a profile opened from a name) is on top.
    bool covered();
    bool loggedIn() const;

    geode::Ref<GJGameLevel> m_level;
    int m_levelID = 0;
    bool m_history = false;
    geode::Ref<GJUserScore> m_historyPlayer;
    CommentKeyType commentType() const { return m_history ? CommentKeyType::User : CommentKeyType::Level; }
    Sort m_sort = Sort::Recent;
    State m_state = State::Loading;
    std::string m_key;                 // GD's key for the request in flight
    std::string m_lastKey;             // and for the page that last arrived
    int m_page = 0;                    // next page to request
    int m_total = -1;                  // comments on the server (-1: not known yet)
    int m_pendingTotal = -1;           // from setupPageInfo, for the page being loaded
    float m_loadingMs = 0;
    geode::Ref<cocos2d::CCArray> m_comments;
    std::unordered_map<int, bool> m_votes; // comment ID -> liked, voted this session
    bool m_posting = false;
    bool m_includePercent = false;
    bool m_wasLoggedIn = false;
    bool m_dirty = false;

    ScrollArea* m_scroll = nullptr;
    ScrollDragger m_drag;
    cocos2d::CCNode* m_list = nullptr; // the comments and the footer, rebuilt
    float m_listTop = 0;
    float m_pad = 0;
    std::vector<Pill> m_fixedPills;    // counter, editor, sort header
    std::vector<Pill> m_pills;         // in m_list
    Pill* m_pressed = nullptr;
    std::vector<cocos2d::CCNode*> m_spinners;

    cocos2d::CCLabelBMFont* m_idLabel = nullptr;
    std::string m_idText;
    float m_copiedMs = 0;              // "copied" shows instead of the ID for this long
    cocos2d::CCLabelBMFont* m_countLabel = nullptr;
    RoundedBox* m_countBg = nullptr;
    geode::TextInput* m_input = nullptr;
    std::string m_placeholder;
    size_t m_postPill = 0, m_signInPill = 0, m_percentPill = 0; // indices into m_fixedPills
    RoundedBox* m_percentBox = nullptr;
    cocos2d::CCNode* m_percentCheck = nullptr;
    cocos2d::CCNode* m_postSpinner = nullptr;
};

} // namespace lazer
