#include "SongSelectInternal.hpp"

#include "../../audio/MusicPlayer.hpp"
#include "../../audio/Sfx.hpp"
#include "../core/Quips.hpp"
#include "../menu/MenuBackground.hpp"

#include <algorithm>
#include <random>

using namespace geode::prelude;

namespace lazer {

levels::Entry const* SongSelect::selectedEntry() const {
    if (!m_hasSelection || m_selected >= m_visible.size() || m_visible[m_selected] >= m_entries.size()) return nullptr;
    return &m_entries[m_visible[m_selected]];
}

// Rows follow one another with osu!'s spacing, and the open pack stands
// apart with room above it and below its last level (osu! gives its expanded
// set twice the spacing; more here, so the open one is plain to see).
void SongSelect::layoutRows() {
    m_rowTops.assign(m_visible.size() + 1, 0.f);
    float y = 0;
    for (size_t i = 0; i < m_visible.size(); i++) {
        if (i > 0) {
            auto const& above = m_entries[m_visible[i - 1]];
            auto const& row = m_entries[m_visible[i]];
            bool opening = row.packHeader && row.pack == m_expandedPack;
            bool closing = !above.packHeader && above.pack >= 0 && row.packHeader;
            y += (opening || closing) ? PACK_OPEN_GAP * m_k : m_spacing;
        }
        m_rowTops[i] = y;
        y += rowHeight(i);
    }
    m_rowTops[m_visible.size()] = y;
}

float SongSelect::rowHeight(size_t visibleIndex) const {
    if (packMode() && visibleIndex < m_visible.size()) {
        auto const& e = m_entries[m_visible[visibleIndex]];
        if (e.packHeader) return PACK_HEADER_HEIGHT * m_k;
        if (e.pack >= 0) return PACK_LEVEL_HEIGHT * m_k;
    }
    return m_panelH;
}

float SongSelect::itemTop(size_t visibleIndex) const {
    if (visibleIndex < m_rowTops.size()) return m_rowTops[visibleIndex];
    return visibleIndex * (m_panelH + m_spacing);
}

float SongSelect::viewHeight() const {
    return m_carouselTop - m_carouselBottom;
}

void SongSelect::select(size_t visibleIndex, bool scroll) {
    if (m_visible.empty()) return;
    visibleIndex = std::min(visibleIndex, m_visible.size() - 1);
    bool changed = !m_hasSelection || visibleIndex != m_selected;
    m_selected = visibleIndex;
    m_hasSelection = true;

    if (scroll) m_scrollTarget = itemTop(visibleIndex) + rowHeight(visibleIndex) / 2 - viewHeight() / 2;
    if (!changed) return;
    // The rows were rebuilt and it's the same level: nothing to redo (the
    // caller refreshes the details if they changed).
    auto const& entry = m_entries[m_visible[visibleIndex]];
    SelectionKey key {entry.id, entry.official, entry.packHeader};
    if (key == m_lastSelection) return;
    m_lastSelection = key;

    // Song and coins for the details, the preview and play.
    levels::resolve(m_entries[m_visible[visibleIndex]]);
    auto const& e = m_entries[m_visible[visibleIndex]];
    remembered().selectedId = e.id;
    remembered().selectedOfficial = e.official;
    remembered().selectedHeader = e.packHeader;
    updateWedge();
    m_previewDelay = PREVIEW_DELAY;

    // Background: the level's thumbnail.
    int request = ++m_backgroundRequest;
    Ref<SongSelect> self = this;
    levelThumbnail(e, packList(), [self, request](CCTexture2D* texture) {
        if (self->m_backgroundRequest != request) return;
        self->m_background->setImage(texture);
    });
}

bool SongSelect::selectSong(std::string const& path, int songID) {
    // Prefer the level last selected here, if it's one with this song.
    auto& r = remembered();
    levels::Entry const* found = nullptr;
    for (auto& e : m_entries) {
        // Only levels with this song have their song file checked.
        if (e.songID != songID) continue;
        levels::resolve(e);
        if (e.songPath != path) continue;
        if (!found || (e.id == r.selectedId && e.official == r.selectedOfficial)) found = &e;
    }
    if (!found) return false;
    r.selectedId = found->id;
    r.selectedOfficial = found->official;
    m_previewPath = path; // already playing: select() mustn't restart it
    m_hasSelection = false;
    applyFilter();
    if (m_hasSelection && m_entries[m_visible[m_selected]].songPath == path) return true;

    // The filters hide it: show everything in its group instead.
    m_group = found->official ? Group::Official : Group::Saved;
    m_folder = 0;
    m_query.clear();
    if (m_search) m_search->setString("");
    r.group = static_cast<int>(m_group);
    r.folder = 0;
    r.query.clear();
    r.selectedId = found->id;
    r.selectedOfficial = found->official;
    m_hasSelection = false;
    applyFilter();
    return true;
}

void SongSelect::selectRandom() {
    static std::mt19937 rng {std::random_device {}()};
    // Five presses in a few seconds: the cursor has opinions.
    if (quips::spam("random", 5, 3.f)) {
        quips::sayLine(RANDOM_SPAM_CURSOR[std::uniform_int_distribution<size_t>(0, RANDOM_SPAM_CURSOR.size() - 1)(rng)]);
    }
    if (packMode()) {
        // Another pack, opened.
        std::vector<int> others;
        for (size_t v : m_visible) {
            auto const& e = m_entries[v];
            if (e.packHeader && e.pack != m_expandedPack) others.push_back(e.pack);
        }
        if (others.empty()) return;
        sfx::play(sfx::sound::DEFAULT_SELECT);
        expandPack(others[std::uniform_int_distribution<size_t>(0, others.size() - 1)(rng)]);
        return;
    }
    if (m_visible.size() < 2) return;
    size_t index = std::uniform_int_distribution<size_t>(0, m_visible.size() - 2)(rng);
    if (index >= m_selected) index++;
    sfx::play(sfx::sound::DEFAULT_SELECT);
    select(index);
}

void SongSelect::restorePreview() {
    if (m_leaving) return;
    auto engine = FMODAudioEngine::sharedEngine();
    if (!m_previewPath.empty() && engine->isMusicPlaying(0)) {
        engine->fadeInMusic(0.3f, 0);
        return;
    }
    m_previewPath.clear();
    previewSong();
}

void SongSelect::openLevelPage() {
    if (!m_hasSelection) return;
    auto const& e = m_entries[m_visible[m_selected]];
    // An online list: GD's own list page (its reward, its like button), over
    // this; its back button comes back here.
    if (e.packHeader && onlineMode()) {
        auto pack = packOf(e);
        if (!pack || !pack->list) return;
        sfx::play(sfx::sound::DEFAULT_SELECT);
        closeMenu();
        browsingOnline() = true;
        CCDirector::get()->pushScene(CCTransitionFade::create(0.5f, LevelListLayer::scene(pack->list)));
        return;
    }
    // RobTop's levels have no level page: straight into the level. A pack opens.
    if (e.official || e.packHeader || !e.level) {
        start();
        return;
    }
    sfx::play(sfx::sound::DEFAULT_SELECT);
    closeMenu();
    returnsHere() = true;
    openingLevelPage() = true;
    CCDirector::get()->replaceScene(CCTransitionFade::create(0.5f, LevelInfoLayer::scene(e.level, false)));
}

void SongSelect::browseOnline() {
    if (m_starting || onlineMode()) return;
    closeMenu();
    // The online search page (song select too), with the search text already
    // searched; backing out of it comes back here.
    returnsHere() = false;
    browsingOnline() = true;
    browse::Request request;
    request.searchPage = true;
    request.query = m_query;
    auto scene = onlineScene(request);
    onlineReturn() = m_kind;
    m_leaving = true;
    m_previewDelay = -1;
    CCDirector::get()->replaceScene(CCTransitionFade::create(0.5f, scene));
}

void SongSelect::back() {
    sfx::play(sfx::sound::DEFAULT_SELECT);
    returnsHere() = false;
    closeMenu();
    m_leaving = true;
    m_previewDelay = -1;

    // The online pages opened from a song select go back to it (its preview
    // carries on from whatever plays).
    if (onlineMode() && onlineReturn()) {
        auto kind = *onlineReturn();
        onlineReturn().reset();
        browsingOnline() = false;
        auto scene = CCScene::create();
        scene->addChild(SongSelect::create(kind));
        CCDirector::get()->replaceScene(CCTransitionFade::create(0.5f, scene));
        return;
    }
    browsingOnline() = false;

    // The menu carries on with whatever is playing (osu! keeps the track going).
    auto engine = FMODAudioEngine::sharedEngine();
    std::string playing = engine->getActiveMusic(0);
    if (!playing.empty() && engine->isMusicPlaying(0)) {
        // Whatever plays was previewed here, so its level has its song path.
        auto it = std::find_if(m_entries.begin(), m_entries.end(), [&](auto const& e) {
            return e.resolved && e.songPath == playing;
        });
        if (it != m_entries.end()) {
            int songID = it->official ? MusicPlayer::officialSongID(it->level->m_audioTrack) : it->level->m_songID;
            MusicPlayer::Track track {songID, playing, it->songTitle, it->songArtist, {}};
            // (RobTop's level IDs aren't online IDs: no thumbnails to look up for them.)
            if (!it->official) track.levels.push_back({it->id, it->name, it->creator});
            MusicPlayer::get().adopt(std::move(track));
        }
    }
    CCDirector::get()->replaceScene(CCTransitionFade::create(0.5f, MenuLayer::scene(false)));
}

void SongSelect::previewSong() {
    if (!m_hasSelection) return;
    auto const& e = m_entries[m_visible[m_selected]];
    if (e.songPath.empty() || e.songPath == m_previewPath) return;
    m_previewPath = e.songPath;
    auto engine = FMODAudioEngine::sharedEngine();
    engine->playMusic(e.songPath, true, 0.5f, 0);
    // No preview points in GD: start a little way in, where most songs have got
    // going. Back from a play (or a cancelled one) of this song: from where it was.
    unsigned length = engine->getMusicLengthMS(0);
    unsigned start = static_cast<unsigned>(length * 0.35f);
    if (g_resume.path == e.songPath && g_resume.ms > 0 && g_resume.ms + 1000 < length) start = g_resume.ms;
    g_resume = {};
    if (length > 0) engine->setMusicTimeMS(start, true, 0);
}

} // namespace lazer
