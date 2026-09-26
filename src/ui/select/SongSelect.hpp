#pragma once

#include "../../levels/LevelLibrary.hpp"
#include "../core/Easing.hpp"
#include "../core/RoundedBox.hpp"
#include "../core/ScrollArea.hpp"

#include <Geode/Geode.hpp>
#include <Geode/cocos/robtop/mouse_dispatcher/CCMouseDelegate.h>
#include <functional>
#include <map>
#include <unordered_map>
#include <string>
#include <vector>

namespace lazer {

class MenuBackground;

// Play -> classic / platformer: every level of that kind you can play in one list,
// RobTop's and your saved ones, after osu!'s song select
// (osu.Game/Screens/Select/SongSelect.cs):
//   left:   the selected level's title wedge and details (progress, songs via
//           GD's own song widget, so Jukebox's controls appear too, per-level
//           options, leaderboard, description)
//   right:  a curved carousel of level panels, with search and filters on top
//   bottom: footer with back / random / level page / play
// The selected level's song previews and its thumbnail becomes the blurred
// background. Playing a level (or backing out of it) comes back here.
// (GD's CCLayer is already a CCMouseDelegate.)
class SongSelect : public cocos2d::CCLayer, public CustomSongDelegate, public LeaderboardManagerDelegate {
public:
    static cocos2d::CCScene* scene(levels::Kind kind);
    // The kind last opened (where gameplay and level pages return to).
    static cocos2d::CCScene* scene();
    static SongSelect* create(levels::Kind kind);

    // Set while the player came from song select, so leaving gameplay or GD's
    // level page returns here instead of GD's own screens.
    static bool& returnsHere();

    void update(float dt) override;
    void onEnter() override;
    void onExit() override;
    void keyBackClicked() override;
    void keyDown(cocos2d::enumKeyCodes key, double timestamp) override;
    void scrollWheel(float y, float x) override;
    void registerWithTouchDispatcher() override;
    bool ccTouchBegan(cocos2d::CCTouch* touch, cocos2d::CCEvent*) override;
    void ccTouchMoved(cocos2d::CCTouch* touch, cocos2d::CCEvent*) override;
    void ccTouchEnded(cocos2d::CCTouch* touch, cocos2d::CCEvent*) override;
    void ccTouchCancelled(cocos2d::CCTouch* touch, cocos2d::CCEvent* e) override { ccTouchEnded(touch, e); }

    // CustomSongDelegate (for the embedded song widget).
    void songIDChanged(int) override {}
    int getActiveSongID() override;
    gd::string getSongFileName() override { return ""; }
    LevelSettingsObject* getLevelSettings() override { return nullptr; }

    // LeaderboardManagerDelegate.
    void updateUserScoreFinished() override {}
    void updateUserScoreFailed() override {}
    void loadLeaderboardFinished(cocos2d::CCArray* scores, char const* key) override;
    void loadLeaderboardFailed(char const* key) override;

protected:
    // Saved first: most players mostly play online levels.
    enum class Group { Saved, Official, Liked };
    enum class Board { Hidden, Loading, Loaded, Failed };
    enum class LoaderPhase { In, Out, Pushed, Cancelling };

    struct Panel {
        size_t entry;
        cocos2d::CCNode* root;
        RoundedBox* bg;
        RoundedBox* thumb;
        Tweened<float> active {0.f};
        Tweened<float> hover {0.f};
        Tweened<float> thumbAlpha {0.f};
        bool hovered = false;
        bool seen = false;        // still in view this frame
        float visibleMs = 0;      // thumbnails load once a panel has settled in view
        bool thumbRequested = false;
    };

    struct Button {
        cocos2d::CCNode* node;
        RoundedBox* bg;
        cocos2d::ccColor4B color;
        std::function<void()> action;
        Tweened<float> hover {0.f};
        bool hovered = false;
        bool selected = false; // lit tab
        ScrollArea* clip = nullptr; // inside a scroll area: only hit while visible in it
    };

    bool init(levels::Kind kind);
    void buildFilter();
    void buildFooter();
    Button& addButton(std::vector<Button>& list, cocos2d::CCNode* parent, char const* glyph, std::string const& label,
                      cocos2d::CCPoint pos, float height, cocos2d::ccColor4B color, std::function<void()> action,
                      float skew = 0.f);
    void applyFilter();
    void select(size_t visibleIndex, bool scroll = true);
    void selectRandom();
    // Play: osu!'s PlayerLoader, then the level.
    void start();
    void buildLoader(levels::Entry const& e);
    void updateLoader(float dt);
    void cancelLoader();
    // Fades a node tree, relative to each node's opacity when first faded.
    void setTreeOpacity(cocos2d::CCNode* node, float factor);
    void openLevelPage();
    void back();
    void toggleFolders();
    void closeFolders();
    void confirmDeleteUnhearted();
    void loadLeaderboard();
    void buildDetails(float top, float bottom);
    // Mirrors the hidden song widget into the song card.
    void updateSongCard();

    void updateCarousel(float dt);
    Panel& makePanel(size_t entry);
    void updateWedge(bool animate = true);
    // Rebuilds the details for the same level, keeping the scroll position.
    void refreshDetails();
    void previewSong();
    float itemTop(size_t visibleIndex) const;
    float viewHeight() const;
    size_t panelAt(cocos2d::CCPoint world);
    Button* buttonAt(cocos2d::CCPoint world);
    bool hittable(Button const& b, cocos2d::CCPoint world);

    float m_k = 1;
    cocos2d::CCSize m_win;
    float m_footerH = 0;
    float m_leftW = 0, m_rightW = 0;
    float m_carouselTop = 0, m_carouselBottom = 0; // screen y
    float m_panelH = 0, m_spacing = 0;

    levels::Kind m_kind = levels::Kind::Classic;
    std::vector<levels::Entry> m_entries;
    std::vector<size_t> m_visible; // filtered + sorted entry indices
    size_t m_selected = 0;         // index into m_visible
    bool m_hasSelection = false;
    Group m_group = Group::Saved;
    int m_folder = 0;              // 0 = all folders
    levels::Sort m_sort = levels::Sort::Default;
    std::string m_query;

    MenuBackground* m_background = nullptr;
    cocos2d::CCNode* m_carousel = nullptr;
    std::map<size_t, Panel> m_panels; // by visible index
    float m_scroll = 0, m_scrollTarget = 0;
    bool m_touchDown = false, m_dragging = false;
    cocos2d::CCPoint m_touchStart, m_touchLast;
    float m_dragVelocity = 0;

    cocos2d::CCNode* m_wedge = nullptr;     // title + details, rebuilt on selection
    Tweened<float> m_wedgeAlpha {0.f};
    geode::TextInput* m_search = nullptr;
    cocos2d::CCLabelBMFont* m_countLabel = nullptr;
    cocos2d::CCLabelBMFont* m_sortLabel = nullptr;
    std::vector<Button> m_tabs;
    std::vector<Button> m_buttons; // footer + sort
    std::vector<Button> m_wedgeButtons; // heart, level options, leaderboard (rebuilt per level)
    std::vector<Button> m_folderItems;  // the open folder dropdown
    Button* m_pressed = nullptr;
    size_t m_folderButton = 0;           // index in m_buttons
    cocos2d::CCLabelBMFont* m_folderLabel = nullptr;
    cocos2d::CCNode* m_folderMenu = nullptr;
    ScrollArea* m_details = nullptr;
    ScrollDragger m_detailsDrag;
    CustomSongWidget* m_songWidget = nullptr;
    struct SongCard {
        cocos2d::CCLabelBMFont* title = nullptr;
        cocos2d::CCLabelBMFont* artist = nullptr;
        cocos2d::CCLabelBMFont* info = nullptr;
        RoundedBox* track = nullptr;
        RoundedBox* fill = nullptr;
        float textW = 0, barW = 0;
        float buttonX = 0, buttonY = 0;
        std::vector<size_t> buttons; // indices in m_wedgeButtons
        size_t download = SIZE_MAX, cancel = SIZE_MAX, getInfo = SIZE_MAX, jukebox = SIZE_MAX;
        size_t more = SIZE_MAX, infoBtn = SIZE_MAX, remove = SIZE_MAX;
    } m_songCard;
    Board m_board = Board::Hidden;
    int m_boardLevel = 0;               // level the leaderboard was loaded for
    geode::Ref<cocos2d::CCArray> m_boardScores;
    bool m_starting = false;
    bool m_refreshPending = false;

    // The loader shown between pressing play and the level.
    cocos2d::CCNode* m_loader = nullptr;
    cocos2d::CCNode* m_loaderMeta = nullptr;
    cocos2d::CCNode* m_spinner = nullptr;
    geode::Ref<GJGameLevel> m_loaderLevel;
    LoaderPhase m_loaderPhase = LoaderPhase::In;
    float m_loaderMs = 0;
    std::vector<cocos2d::CCNode*> m_uiRoots;               // song select's own nodes, faded out
    std::unordered_map<cocos2d::CCNode*, GLubyte> m_baseOpacity;
    Tweened<float> m_uiAlpha {1.f};
    Tweened<float> m_loaderAlpha {0.f};
    Tweened<float> m_loaderScale {0.7f};
    Tweened<float> m_metaAlpha {0.f};
    Tweened<float> m_dimTween {0.55f};     // a details refresh waiting for the touch to end

    float m_previewDelay = -1;     // debounce before the selected song starts
    std::string m_previewPath;
    int m_backgroundRequest = 0;
    float m_enterMs = 0;
    float m_wheelClaimMs = 0;
};

} // namespace lazer
