#pragma once

#include "../core/Easing.hpp"
#include "../core/ScrollArea.hpp"
#include "WaveOverlay.hpp"

#include <Geode/Geode.hpp>
#include <functional>
#include <memory>
#include <string>
#include <unordered_set>
#include <vector>

namespace lazer {

// "Your levels" and "your lists" (GD's "my levels" and "my lists") as osu!'s
// beatmap listing (osu.Game/Overlays/BeatmapListingOverlay + Overlays/
// BeatmapListing/), in the create button's orange: a titled page of cards
// (Beatmaps/Drawables/Cards/BeatmapCardNormal) for the levels and lists on
// this device, with a search box, GD's folders as a filter row and a "new"
// button on top. Your levels can also show the ones you uploaded, fetched a
// page at a time as you scroll.
//
// Loading is made to feel quick: placeholder cards shimmer where the results
// will go, a fresh search keeps the old cards (dimmed) until the new ones
// arrive, the next page is fetched before it's scrolled to, and cards come
// in one after the other.
//
// It sits over GD's own LevelBrowserLayer, which stays hidden underneath and
// does the work: it asks GameLevelManager for pages and gets the results as
// its LevelManagerDelegate; the hooks in LevelListingOverlayHooks.cpp hand them here.
class LevelListingOverlay : public WaveOverlay, public TextInputDelegate {
public:
    // Whether GD's browser for this search gets this page: your levels and
    // lists (the online lists are song select's; saved levels and the other
    // local ones keep GD's own screen).
    static bool wants(GJSearchObject* search);
    // The page's colours: osu!'s Orange, for your own things.
    static theme::Scheme schemeFor(GJSearchObject* search);
    // Set when "your levels" or "your lists" goes back: the menu opens its
    // create buttons again, whichever way the player came.
    static bool& backToCreate();

    static LevelListingOverlay* create(LevelBrowserLayer* owner, GJSearchObject* search);

    // GD's LevelManagerDelegate calls, from the LevelBrowserLayer hooks.
    void levelsLoaded(cocos2d::CCArray* items, char const* key);
    void levelsFailed(char const* key);
    void pageInfo(std::string const& info, char const* key);
    // Leaves the page for where the player came from (the menu or song select).
    void goBack();
    bool leaving() const { return m_leaving; }
    // GD's hidden list registered for the wheel when it was (re)built: take it back.
    void claimWheel() { if (m_scroll) m_scroll->claimWheel(); }

    bool ccTouchBegan(cocos2d::CCTouch* touch, cocos2d::CCEvent* e) override;
    void ccTouchMoved(cocos2d::CCTouch* touch, cocos2d::CCEvent* e) override;
    void ccTouchEnded(cocos2d::CCTouch* touch, cocos2d::CCEvent* e) override;

    // TextInputDelegate (the search box).
    void textChanged(CCTextInputNode* node) override;
    void enterPressed(CCTextInputNode* node) override;

protected:
    enum class State { Loading, Loaded, Failed };
    // The filter rows (BeatmapSearchFilterRow): GD's folders, and this device or your uploads.
    enum class Row { Folder, Source, Count };

    // A tappable thing: a card, a filter chip, a tab or a button.
    struct Pill {
        enum class Kind { Button, Chip, Tab, Card };
        Kind kind = Kind::Button;
        cocos2d::CCNode* node = nullptr;
        RoundedBox* bg = nullptr;
        cocos2d::ccColor4B color {};          // resting fill
        cocos2d::ccColor4B hoverColor {};
        cocos2d::ccColor4B activeColor {};    // chips: the fill while chosen
        std::function<void()> action;
        std::function<bool()> active;         // chips: chosen
        std::function<bool()> usable;         // greyed out otherwise
        std::vector<cocos2d::CCLabelBMFont*> tinted; // text and icons coloured with the state
        cocos2d::ccColor3B textColor {255, 255, 255};
        cocos2d::ccColor3B textHover {255, 255, 255};
        cocos2d::ccColor3B textActive {255, 255, 255};
        RoundedBox* rim = nullptr;            // cards: the highlight border while hovered
        Tweened<float> hover {0.f};
        bool enabled = true;
        bool hovered = false;
    };

    // One result: a level or a list.
    struct Card {
        geode::Ref<cocos2d::CCObject> item;
        int thumbLevel = 0;            // level whose thumbnail to show (a list's first)
        cocos2d::CCNodeRGBA* root = nullptr;
        RoundedBox* bg = nullptr;      // the body; the thumbnail dimmed behind the text once it loads
        RoundedBox* thumb = nullptr;
        cocos2d::CCNode* fallback = nullptr; // shown instead when there's no thumbnail
        size_t pill = 0;               // index into m_cardPills
        Tweened<float> appear {0.f};
        float delayMs = 0;             // before it appears (cards come in one after the other)
        bool started = false;          // its fade-in has begun
        float x = 0, top = 0;          // from the left and the top of the cards area
        bool thumbRequested = false;
        bool shimmer = true;           // the thumbnail's placeholder is still animating
    };

    bool init(LevelBrowserLayer* owner, GJSearchObject* search);
    void onUpdate(float dt) override;

    // Asks GD for a page (replacing what's shown when `fresh`).
    void request(GJSearchObject* search, bool fresh);
    // Drops the results and loads the first page of the current controls.
    void startSearch();
    void loadMore();
    void refresh();
    // Adds a page's items as cards and settles the state after it.
    void ingest(cocos2d::CCArray* items);
    // Asks GD for the page after the current one, so it's ready when scrolled to.
    void prefetchNext();
    void dropPrefetch();
    void openItem(cocos2d::CCObject* item);
    // Your levels and lists: the ones on this device (not their uploads).
    bool local() const { return !m_online; }
    // Fills the page with your levels or lists that match the folder and search.
    void showLocal();
    // Builds a few of the cards showLocal queued (thousands would stall a frame).
    void buildPending();
    // GD's "my online levels": what you uploaded.
    GJSearchObject* onlineSearch();
    // GD's own "new level" / "new list".
    void createNew();
    // GD's folders your levels (or lists) are in, for the folder row.
    void readFolders();

    // Building: the top part once, cards as they arrive, the footer per state.
    void buildSearchControl();
    void buildFilterRow(Row row);
    void buildStrip();
    // Buttons other mods add beside GD's "new" button (GDShare's import), on
    // the strip after "new". Once every hook on GD's browser has run.
    void addModButtons();
    void addCard(cocos2d::CCObject* item);
    void addSkeleton(cocos2d::CCNode* parent, int count, float y);
    void rebuildFooter();
    void clearCards();
    // Places the sections top to bottom.
    void relayout();
    void layoutCards();
    void placeCard(Card& card);
    float cardsHeight() const;
    // Stacks the rows that apply (the folders only for this device's levels).
    void layoutRows();
    bool rowShown(Row row) const;
    Pill& addPill(std::vector<Pill>& list, cocos2d::CCNode* parent, cocos2d::CCSize size, float radius,
                  cocos2d::CCPoint pos, cocos2d::CCPoint anchor, cocos2d::ccColor4B color,
                  cocos2d::ccColor4B hoverColor, std::function<void()> action);
    // A rounded tab with an optional icon, its text and fills set for the strip.
    Pill& addTab(std::vector<Pill>& list, cocos2d::CCNode* parent, char const* glyph, std::string const& text,
                 float height, cocos2d::CCPoint pos, cocos2d::CCPoint anchor, std::function<void()> action);
    Pill* pillAt(cocos2d::CCPoint world);
    void updatePill(Pill& p, bool hovered, float dt);
    bool optionActive(Row row, int option) const;
    void toggleOption(Row row, int option);
    void requestThumbnail(Card& card);
    void showThumbnail(Card& card, cocos2d::CCTexture2D* texture);
    bool nearView(float top, float height) const;
    void queueSearch(float delayMs);

    LevelBrowserLayer* m_owner = nullptr;
    State m_state = State::Loading;
    geode::Ref<GJSearchObject> m_current;   // the page in flight or last loaded
    bool m_currentFresh = false;            // it replaces the cards (a retry does the same)
    std::string m_key;                      // GD's key for the request in flight
    std::string m_lastKey;                  // and for the page that last arrived
    float m_loadingMs = 0;
    bool m_more = true;                     // another page may exist
    int m_total = -1;                       // items on the server (-1: not known)
    int m_pendingTotal = -1, m_pendingEnd = -1; // from pageInfo, for the page in flight
    bool m_stale = false;                   // the cards shown are the last search's, dimmed
    bool m_leaving = false;
    bool m_dirty = false;
    float m_searchDelay = -1;               // ms until the queued search runs (-1: none)

    // The page after the current one, asked for ahead of time.
    geode::Ref<GJSearchObject> m_prefetch;
    std::string m_prefetchKey;
    geode::Ref<cocos2d::CCArray> m_prefetched;
    bool m_prefetchReady = false;
    bool m_prefetchFailed = false;
    int m_prefetchTotal = -1, m_prefetchEnd = -1;

    // What's shown.
    std::string m_query;
    bool m_lists = false;                   // your lists, not your levels
    geode::Ref<GJSearchObject> m_mineSearch; // what GD's browser was opened with
    bool m_online = false;                  // your uploads instead of this device's levels
    int m_folder = 0;                       // GD's folder shown (0: all)
    std::vector<int> m_folders;             // the folder row's: 0, then the folders in use
    std::vector<std::string> m_folderNames;
    std::vector<geode::Ref<cocos2d::CCObject>> m_toBuild; // cards still to build
    size_t m_toBuildNext = 0;

    ScrollArea* m_scroll = nullptr;
    ScrollDragger m_drag;
    float m_pad = 0;
    geode::TextInput* m_input = nullptr;
    cocos2d::CCNode* m_searchRow = nullptr;    // the search box, its magnifier and its cross
    cocos2d::CCNode* m_rowsHolder = nullptr;   // the filter rows
    std::vector<cocos2d::CCNode*> m_rowNodes;  // per Row: its label and chips
    std::vector<float> m_rowHeights;
    float m_rowsHeight = 0;
    cocos2d::CCLayerColor* m_controlBg = nullptr;
    cocos2d::CCNode* m_stripHolder = nullptr;  // the strip, moved as the control's height changes
    float m_stripNextX = 0;                    // where the next button after "new" goes
    bool m_modButtonsAdded = false;
    cocos2d::CCLabelBMFont* m_countLabel = nullptr;
    size_t m_clearPill = SIZE_MAX;             // index into m_fixedPills
    RoundedBox* m_progressTrack = nullptr;     // the loading bar under the strip
    RoundedBox* m_progressBar = nullptr;
    float m_progressT = 0;
    cocos2d::CCNodeRGBA* m_list = nullptr;     // the cards
    Tweened<float> m_listAlpha {1.f};
    cocos2d::CCNode* m_footer = nullptr;       // placeholders / messages after them
    float m_footerHeight = 0;
    float m_cardsTop = 0;
    float m_cardW = 0, m_cardH = 0;
    int m_columns = 1;
    size_t m_pageStart = 0;                    // first card of the page being added
    std::vector<Card> m_cards;
    std::unordered_set<int> m_seen;            // level / list IDs shown
    std::vector<Pill> m_fixedPills;            // search control and strip
    std::vector<Pill> m_cardPills;
    std::vector<Pill> m_footerPills;
    Pill* m_pressed = nullptr;
    std::vector<RoundedBox*> m_shimmer;        // the footer's placeholders
    float m_shimmerPhase = 0;
    std::shared_ptr<char> m_alive;             // thumbnail callbacks check it
};

} // namespace lazer
