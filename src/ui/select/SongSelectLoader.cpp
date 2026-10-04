#include "SongSelectInternal.hpp"

#include "../../audio/Sfx.hpp"
#include "../core/Theme.hpp"
#include "../menu/MenuBackground.hpp"
#include "../overlays/Dialog.hpp"

#include <algorithm>

using namespace geode::prelude;

namespace lazer {

SongSelect* SongSelect::pageLoader(GJGameLevel* level, std::function<void()> launch) {
    auto ret = new SongSelect();
    if (!ret->CCLayer::init()) {
        delete ret;
        return nullptr;
    }
    ret->autorelease();
    ret->m_pageLaunch = std::move(launch);
    ret->m_win = CCDirector::get()->getWinSize();
    ret->m_k = unitScale();
    ret->m_loaderLevel = level;
    ret->m_withSong = false; // GD's page still handles song/download decisions.
    auto source = CCLayerGradient::create({34, 26, 50, 255}, {8, 8, 12, 255});
    source->setContentSize(ret->m_win);
    // MenuBackground borrows its source; keep it alive for every capture.
    source->setVisible(false);
    ret->addChild(source, -10);
    ret->m_background = MenuBackground::create(source, BACKGROUND_DIM, true, true);
    ret->addChild(ret->m_background, -1);
    // The shared animator moves these song-select containers as it fades out.
    ret->m_wedge = CCNode::create();
    ret->m_carousel = CCNode::create();
    ret->addChild(ret->m_wedge);
    ret->addChild(ret->m_carousel);
    auto entry = levels::fromLevel(level, false);
    levels::resolve(entry);
    ret->buildLoader(entry);
    ret->m_starting = true;
    ret->m_loaderPhase = LoaderPhase::In;
    ret->m_uiAlpha.set(0);
    ret->m_loaderScale.set(0.7f);
    ret->m_loaderScale.to(1, 650, Easing::OutQuint);
    ret->m_loaderAlpha.set(0);
    ret->m_loaderAlpha.to(1, 500, Easing::OutQuint);
    ret->m_metaAlpha.set(0);
    ret->m_spinnerAlpha.set(1);
    ret->m_spinnerScale.set(1);
    ret->m_dimTween.set(BACKGROUND_DIM);
    ret->m_dimTween.to(LOADER_DIM, 800, Easing::OutQuint);
    ret->setTouchEnabled(true);
    ret->scheduleUpdate();
    sfx::play(sfx::sound::MENU_PLAY_SELECT);
    return ret;
}

void SongSelect::start() {
    if (!m_hasSelection || m_starting) return;
    auto const& e = m_entries[m_visible[m_selected]];
    // A pack opens (or closes) instead of playing.
    if (e.packHeader) {
        expandPack(e.pack == m_expandedPack ? -1 : e.pack);
        return;
    }
    if (!e.level) return;
    auto level = e.level;
    int songID = level ? level->m_songID : 0;
    auto songs = MusicDownloadManager::sharedState();
    if (e.official || songID <= 0 || songs->isSongDownloaded(songID)) return play(true);

    // What downloading means: the song's size when GD knows it, and the extras.
    std::string what = "the song";
    if (auto info = songs->getSongInfoObject(songID); info && info->m_fileSize > 0) {
        what = fmt::format("the song ({:.1f} MB)", info->m_fileSize);
    }
    auto count = [](std::string const& ids) {
        int n = 0;
        for (auto part : utils::string::split(ids, ",")) if (!part.empty()) n++;
        return n;
    };
    // m_songIDs includes the main song.
    int moreSongs = std::max(0, count(level->m_songIDs) - 1);
    int sfx = count(level->m_sfxIDs);
    std::vector<std::string> extras;
    if (moreSongs > 0) extras.push_back(fmt::format("{} more song{}", moreSongs, moreSongs == 1 ? "" : "s"));
    if (sfx > 0) extras.push_back(fmt::format("{} sound effect{}", sfx, sfx == 1 ? "" : "s"));
    for (size_t i = 0; i < extras.size(); i++) what += (i + 1 == extras.size() ? " and " : ", ") + extras[i];

    Ref<SongSelect> self = this;
    Dialog::show(icon::MUSIC, "Song not downloaded",
        fmt::format("{} needs {} to play with music.", e.name, what), {
        {"Download and play", Dialog::Kind::Ok, [self] { self->play(true); }},
        {"Play without music", Dialog::Kind::Ok, [self] { self->play(false); }},
        {"Cancel", Dialog::Kind::Cancel, nullptr},
    });
}

void SongSelect::play(bool withSong) {
    if (!m_hasSelection || m_starting) return;
    auto const& e = m_entries[m_visible[m_selected]];
    if (e.packHeader || !e.level) return;
    m_withSong = withSong;
    sfx::play(sfx::sound::MENU_PLAY_SELECT);
    closeMenu();
    m_starting = true;
    // Disable immediately, before another touch or keyboard event can reach
    // the fields. The normal update restores them after loader cancellation.
    m_searchEnabled = false;
    if (m_search) {
        m_search->defocus();
        m_search->setEnabled(false);
    }
    if (m_pageInput) {
        m_pageInput->defocus();
        m_pageInput->setEnabled(false);
    }
    m_pressed = nullptr;
    m_loaderLevel = e.level;

    // osu!'s PlayerLoader: song select fades away, the level's card scales in
    // over its background, and after a short wait the level starts.
    m_uiRoots.clear();
    m_baseOpacity.clear();
    for (auto child : CCArrayExt<CCNode*>(this->getChildren())) {
        // Below zero: the background and its shading, which stay.
        if (child->getZOrder() >= 0) m_uiRoots.push_back(child);
    }
    buildLoader(e);
    m_loaderMs = 0;
    m_loaderPhase = LoaderPhase::In;
    m_uiAlpha.set(1);
    m_uiAlpha.to(0, 300, Easing::OutQuint);
    m_loaderScale.set(0.7f);
    m_loaderScale.to(1, 650, Easing::OutQuint);
    m_loaderAlpha.set(0);
    m_loaderAlpha.to(1, 500, Easing::OutQuint);
    m_metaAlpha.set(0);
    m_spinnerAlpha.set(1);
    m_spinnerScale.set(1);
    dropLevel();
    m_dimTween.set(BACKGROUND_DIM);
    m_dimTween.to(LOADER_DIM, 800, Easing::OutQuint);
    // The preview fades while the card scales in, and goes before the level is
    // built: GD loads a level's song into the music slot the preview holds, and
    // when it's the same song it keeps the preview's stream as the level's
    // (seeking it to the level's start), so stopping the preview afterwards
    // would take the level's song with it (silent first attempt).
    auto engine = FMODAudioEngine::sharedEngine();
    if (engine->isMusicPlaying(0)) {
        g_resume = {std::string(engine->getActiveMusic(0)), engine->getMusicTimeMS(0)};
        engine->fadeOutMusic(LEVEL_LOAD_AT / 1000.f, 0);
    }
    if (needsDownloads(e)) startDownloads(e);
}

// The level's data, and its song unless playing without.
bool SongSelect::needsDownloads(levels::Entry const& e) const {
    if (levels::readyToPlay(e)) return false;
    return m_withSong || std::string(e.level->m_levelString).empty();
}

void SongSelect::buildLoader(levels::Entry const& e) {
    float k = m_k;
    m_loader = CCNode::create();
    m_loader->setPosition(m_win / 2);
    this->addChild(m_loader, 50);

    // osu!'s BeatmapMetadataDisplay: title, artist, the background in a
    // rounded box with a spinner, then the difficulty and a details grid.
    auto main = CCNode::create();
    m_loader->addChild(main);
    auto centred = [&](CCNode* parent, CCNode* node, float y) {
        node->setAnchorPoint({0.5f, 0.5f});
        node->setPosition({0, y});
        parent->addChild(node);
    };
    float italic = 8.f; // Outfit has no italic: lean it like osu!'s italic Torus.
    auto title = makeText(e.name, Weight::SemiBold, 40 * k);
    title->setSkewX(italic);
    fit(title, m_win.width * 0.8f);
    centred(main, title, 150 * k);
    std::string song = e.songArtist.empty() ? e.songTitle : e.songTitle + " - " + e.songArtist;
    auto artist = makeText(song, Weight::Regular, 26 * k);
    artist->setSkewX(italic);
    artist->setColor(theme::CONTENT2);
    fit(artist, m_win.width * 0.8f);
    centred(main, artist, 110 * k);

    CCSize boxSize {300 * k, 60 * k};
    auto box = RoundedBox::create(boxSize, 10 * k, {40, 36, 52, 255});
    box->setShadow(12 * k, {0, 0, 0, 120});
    centred(main, box, 48 * k);
    Ref<RoundedBox> boxRef = box;
    levelThumbnail(e, packList(), [boxRef](CCTexture2D* texture) {
        if (texture && boxRef->getParent()) boxRef->setTexture(texture);
    });
    auto shade = RoundedBox::create(boxSize, 10 * k, {0, 0, 0, 110});
    centred(main, shade, 48 * k);
    // The glyph doesn't sit in the middle of its label's line box: spin a holder
    // around the glyph's own centre instead.
    m_spinner = CCNode::create();
    auto glyph = makeIcon(icon::ROTATE, 24 * k);
    glyph->setAnchorPoint({0, 0});
    if (auto letter = glyph->getChildByType<CCSprite>(0)) {
        glyph->setPosition(-letter->getPosition() * glyph->getScale());
    }
    m_spinner->addChild(glyph);
    m_spinner->setPosition({0, 48 * k});
    main->addChild(m_spinner);
    m_loaderStatus = nullptr;
    m_loaderFill = nullptr;
    if (needsDownloads(e)) {
        // Downloading first: the spinner moves up for a status line and a bar.
        m_spinner->setPositionY(60 * k);
        m_loaderStatus = makeText("downloading", Weight::Regular, 14 * k);
        m_loaderStatus->setColor(theme::CONTENT2);
        centred(main, m_loaderStatus, 38 * k);
        m_loaderBarW = boxSize.width - 40 * k;
        auto track = RoundedBox::create({m_loaderBarW, 4 * k}, 2 * k, {255, 255, 255, 40});
        centred(main, track, 26 * k);
        m_loaderFill = RoundedBox::create({4 * k, 4 * k}, 2 * k, theme::COLOUR3);
        m_loaderFill->setAnchorPoint({0, 0.5f});
        m_loaderFill->setPosition({-m_loaderBarW / 2, 26 * k});
        main->addChild(m_loaderFill);
    }

    auto meta = CCNode::create();
    m_loader->addChild(meta);
    m_loaderMeta = meta;

    // Difficulty: face, name and stars (or moons).
    std::vector<std::pair<char const*, std::string>> diff;
    if (e.stars > 0) diff.push_back({rewardIcon(e), std::to_string(e.stars)});
    if (!e.platformer) diff.push_back({icon::CLOCK, levels::lengthName(e.length)});
    if (e.coins > 0) diff.push_back({icon::COINS, fmt::format("{}/{}", e.coinsCollected, e.coins)});
    auto face = difficultyFace(e, 40 * k);
    auto diffRow = infoRow(diff, 20 * k, {255, 255, 255});
    float rowW = 40 * k + 12 * k + diffRow->getContentSize().width;
    face->setPosition({-rowW / 2 + 20 * k, -10 * k});
    meta->addChild(face);
    diffRow->setPosition({-rowW / 2 + 52 * k, -10 * k});
    meta->addChild(diffRow);

    // Details grid: label on the left of the centre, value on the right.
    float y = -52 * k;
    auto row = [&](char const* label, std::string const& value) {
        if (value.empty()) return;
        auto l = makeText(label, Weight::Regular, 17 * k);
        l->setColor(theme::LIGHT1);
        l->setAnchorPoint({1, 0.5f});
        l->setPosition({-6 * k, y});
        meta->addChild(l);
        auto v = makeText(value, Weight::SemiBold, 17 * k);
        v->setAnchorPoint({0, 0.5f});
        v->setPosition({6 * k, y});
        fit(v, m_win.width * 0.35f);
        meta->addChild(v);
        y -= 24 * k;
    };
    row("creator", e.creator);
    row("song", e.songTitle);
    if (!e.official) row("level id", std::to_string(e.id));
    if (e.platformer) row("best", e.bestTime > 0 ? formatTime(e.bestTime) : "");
    else row("best", e.normalPercent > 0 ? fmt::format("{}%", e.normalPercent) : "");

    setTreeOpacity(m_loader, 0);
}

void SongSelect::setTreeOpacity(CCNode* node, float factor) {
    if (auto rgba = dynamic_cast<CCRGBAProtocol*>(node)) {
        auto [it, fresh] = m_baseOpacity.try_emplace(node, rgba->getOpacity());
        rgba->setOpacity(static_cast<GLubyte>(it->second * std::clamp(factor, 0.f, 1.f)));
        // Labels pass their opacity on to their letters themselves, and so do
        // nodes cascading it (carousel panels): their children follow.
        if (typeinfo_cast<CCLabelBMFont*>(node) || rgba->isCascadeOpacityEnabled()) return;
    }
    for (auto child : CCArrayExt<CCNode*>(node->getChildren())) setTreeOpacity(child, factor);
}

void SongSelect::cancelLoader() {
    if (!m_starting || m_loaderPhase != LoaderPhase::In) return;
    // Downloads carry on in the background (as from GD's level page).
    m_downloading = false;
    stopListening();
    sfx::play(sfx::sound::DEFAULT_SELECT);
    m_loaderPhase = LoaderPhase::Cancelling;
    m_loaderMs = 0;
    // Not built yet: the preview is only fading, bring it back. Built: its
    // slot is the level's now, the preview starts again once the level is dropped.
    if (m_levelLoad != LevelLoad::Loaded) restorePreview();
    m_uiAlpha.to(1, 300, Easing::OutQuint);
    m_loaderAlpha.to(0, 300, Easing::OutQuint);
    m_loaderScale.to(0.7f, 600, Easing::OutQuint);
    m_dimTween.to(BACKGROUND_DIM, 400, Easing::OutQuint);
}

void SongSelect::updateLoader(float dt) {
    float ms = dt * 1000.f;
    m_loaderMs += ms;
    // The frame after the level was built is a long one. The wait counts it
    // (the level loaded during it), but the animations carry on from where
    // they stood instead of jumping to their ends.
    float animDt = std::min(dt, 0.1f);
    for (auto t : {&m_uiAlpha, &m_loaderAlpha, &m_loaderScale, &m_metaAlpha, &m_spinnerAlpha, &m_spinnerScale, &m_dimTween}) {
        t->update(animDt);
    }

    switch (m_loaderPhase) {
        case LoaderPhase::In:
            // The details follow the card in (MetadataInfo's delayed fade).
            if (m_loaderMs >= 500 && m_metaAlpha.target() < 1) m_metaAlpha.to(1, 500, Easing::OutQuint);
            if (m_downloading) {
                updateDownloads(dt);
                // The wait picks up again once everything is here.
                m_loaderMs = std::min(m_loaderMs, PUSH_DELAY - 700.f);
                if (m_downloadFailed && (m_failedMs += ms) >= 1800) {
                    cancelLoader();
                    return;
                }
            } else if (m_levelLoad == LevelLoad::Waiting && m_loaderMs >= LEVEL_LOAD_AT) {
                // Built next frame, so this one shows the status first.
                m_levelLoad = LevelLoad::Queued;
                if (m_loaderStatus) m_loaderStatus->setString("loading level");
            } else if (m_levelLoad == LevelLoad::Queued) {
                loadLevel();
            } else if (m_levelLoad == LevelLoad::Loaded && m_loaderMs >= PUSH_DELAY) {
                // pushWhenLoaded: loaded and the wait is over. ContentOut: the
                // card shrinks and fades.
                m_loaderPhase = LoaderPhase::Out;
                m_loaderMs = 0;
                m_loaderScale.to(0.7f, CONTENT_OUT * 2, Easing::OutQuint);
                m_loaderAlpha.to(0, CONTENT_OUT, Easing::OutQuint);
            }
            break;
        case LoaderPhase::Out:
            if (m_loaderMs >= CONTENT_OUT) {
                m_loaderPhase = LoaderPhase::Pushed;
                if (m_pageLaunch) {
                    // Defer the scene-changing native action until this update ends.
                    Ref<SongSelect> self = this;
                    Loader::get()->queueInMainThread([self] {
                        auto launch = self->m_pageLaunch;
                        if (!self->isRunning()) return;
                        self->removeFromParent();
                        self->m_pageLaunch = nullptr;
                        if (launch) launch();
                    });
                    return;
                }
                m_previewDelay = -1;
                returnsHere() = true;
                if (!m_levelScene) {
                    // Not built (PlayLayer::scene gave nothing back): as before,
                    // build it now, with nothing of the preview left playing.
                    FMODAudioEngine::sharedEngine()->stopAllMusic(true);
                    CCDirector::get()->replaceScene(CCTransitionFade::create(0.4f, PlayLayer::scene(m_loaderLevel, false, false)));
                    break;
                }
                // The level's song is loaded and nothing else plays: it starts
                // it once it begins.
                auto transition = CCTransitionFade::create(0.4f, m_levelScene);
                m_levelScene = nullptr;
                m_levelLoad = LevelLoad::Waiting;
                CCDirector::get()->replaceScene(transition);
            }
            break;
        case LoaderPhase::Cancelling:
            if (m_loaderMs >= 400) {
                setTreeOpacity(m_loader, 0);
                for (auto root : m_uiRoots) setTreeOpacity(root, 1);
                m_loader->removeFromParent();
                m_loader = m_loaderMeta = m_spinner = nullptr;
                m_loaderStatus = nullptr;
                m_loaderFill = nullptr;
                m_uiRoots.clear();
                m_baseOpacity.clear();
                m_wedge->setPositionX(0);
                m_carousel->setPositionX(0);
                m_background->setDim(BACKGROUND_DIM);
                m_starting = false;
                // Song select is back and still: a good moment to let go of
                // the level, if it was built, and to start the preview again.
                if (m_levelLoad == LevelLoad::Loaded) {
                    dropLevel();
                    restorePreview();
                }
                // Results that arrived meanwhile.
                if (m_browseDirty) onBrowseChanged();
                return;
            }
            break;
        case LoaderPhase::Pushed:
            break;
    }

    float ui = m_uiAlpha.get();
    for (auto root : m_uiRoots) {
        if (root != m_loader) setTreeOpacity(root, ui);
    }
    // Song select's sides slide away as they fade.
    m_wedge->setPositionX(-60 * m_k * (1 - ui));
    m_carousel->setPositionX(100 * m_k * (1 - ui));

    if (m_loader) {
        m_loader->setScale(m_loaderScale.get());
        float alpha = m_loaderAlpha.get();
        setTreeOpacity(m_loader, alpha);
        if (m_loaderMeta) setTreeOpacity(m_loaderMeta, alpha * m_metaAlpha.get());
        if (m_spinner) {
            m_spinner->setRotation(m_spinner->getRotation() + animDt * 300.f);
            m_spinner->setScale(m_spinnerScale.get());
            setTreeOpacity(m_spinner, alpha * m_spinnerAlpha.get());
        }
    }
    m_background->setDim(m_dimTween.get());
}

// osu!'s PlayerLoader loads the Player in the background while its card shows
// (prepareNewPlayer) and pushes it once it's loaded and the wait is over. GD
// can only build a level on this thread, in one go: the frame this runs in
// stalls, but it does so while the loader is up (osu! shows a spinner there
// too), and the push afterwards is only the transition.
void SongSelect::loadLevel() {
    if (m_pageLaunch) {
        // Let LevelInfoLayer start the level after the card; its native checks
        // and other mods' onPlay hooks must still run.
        m_levelLoad = LevelLoad::Loaded;
        return;
    }
    // The preview (faded by now) leaves its music slot to the level: see play().
    FMODAudioEngine::sharedEngine()->stopAndRemoveMusic(0);
    m_previewPath.clear();

    // Mods hooking PlayLayer::init run now too, not at the push.
    m_levelScene = PlayLayer::scene(m_loaderLevel, false, false);
    m_levelLoad = LevelLoad::Loaded;

    // PlayLayer::init makes itself GameManager's current level (PlayLayer::get()),
    // and that stays: mods queue work from init that asks for it on the next
    // frame (Custom Keybinds does, and crashed on a null level). GD pausing it
    // when the game loses focus is stopped below until it's entered. Its updates
    // and actions don't run before then (cocos pauses them for a node that isn't
    // running), and it registers for touches and keys only in onEnter.

    // Loaded: the spinner goes (LoadingSpinner.PopOut).
    m_spinnerAlpha.to(0, 250, Easing::OutQuint);
    m_spinnerScale.to(0.6f, 500, Easing::OutQuint);
    if (m_loaderStatus) m_loaderStatus->setString("ready");
}

// Cancelled after the level was built: it was never entered, so there is no
// onExit to undo what onEnter registers, and onQuit is for leaving a level that
// was played (it saves progress and goes back to the menus). What the director
// does to any scene it drops is cleanup() then release: cleanup stops the
// actions and schedules the level (and mods) set up in init, which would keep
// it alive, and the release destroys it (PlayLayer's destructor). GameManager
// still points at it: drop that, and the song it loaded.
void SongSelect::dropLevel() {
    m_levelLoad = LevelLoad::Waiting;
    if (!m_levelScene) return;
    FMODAudioEngine::sharedEngine()->stopAndRemoveMusic(0);
    if (auto layer = m_levelScene->getChildByType<PlayLayer>(0)) {
        auto gm = GameManager::get();
        if (gm->m_playLayer == layer) gm->m_playLayer = nullptr;
        if (gm->m_gameLayer == layer) gm->m_gameLayer = nullptr;
    }
    m_levelScene->cleanup();
    m_levelScene = nullptr;
}

void SongSelect::startDownloads(levels::Entry const& e) {
    m_downloading = true;
    m_downloadFailed = false;
    m_failedMs = 0;
    m_downloadProgress.set(0);
    auto level = e.level;
    if (std::string(level->m_levelString).empty()) {
        auto glm = GameLevelManager::sharedState();
        glm->m_levelDownloadDelegate = this;
        glm->downloadLevel(e.id, false, 0);
    }
    int songID = level->m_songID;
    auto songs = MusicDownloadManager::sharedState();
    if (m_withSong && songID > 0 && !songs->isSongDownloaded(songID)) {
        songs->addMusicDownloadDelegate(this);
        // GD's song widget when it's this level's: it also fetches the level's
        // extra songs and SFX. Otherwise just the song.
        auto w = m_songWidget;
        if (w && w->m_downloadBtn && w->m_songInfoObject && w->m_songInfoObject->m_songID == songID) {
            w->onDownload(w->m_downloadBtn);
        } else {
            songs->downloadSong(songID);
        }
    }
    updateDownloads(0);
}

void SongSelect::updateDownloads(float dt) {
    if (!m_downloading || m_downloadFailed || !m_loaderLevel) return;
    auto level = m_loaderLevel.data();
    auto songs = MusicDownloadManager::sharedState();
    int songID = level->m_songID;
    bool levelReady = !std::string(level->m_levelString).empty();
    bool songReady = !m_withSong || songID <= 0 || songs->isSongDownloaded(songID);
    int percent = songReady ? 100 : songs->getDownloadProgress(songID);

    // GD reports no progress for the level's data (it's small): it counts as a fifth.
    float progress = (levelReady ? 0.2f : 0.f) + 0.8f * std::clamp(percent / 100.f, 0.f, 1.f);
    if (progress != m_downloadProgress.target()) m_downloadProgress.to(progress, 300, Easing::OutQuint);
    m_downloadProgress.update(dt);

    if (m_loaderStatus) {
        std::string status;
        if (!levelReady) status = "downloading level";
        if (!songReady) {
            std::string song = percent > 0 ? fmt::format("song {}%", percent) : "song";
            status = status.empty() ? "downloading " + song : status + " and " + song;
        }
        // Everything's here: the level is built next.
        if (status.empty()) status = "loading level";
        if (m_loaderStatus->getString() != status) m_loaderStatus->setString(status.c_str());
    }
    if (m_loaderFill) {
        float h = m_loaderFill->getContentSize().height;
        m_loaderFill->setContentSize({std::max(h, m_loaderBarW * m_downloadProgress.get()), h});
    }

    if (levelReady && songReady) {
        m_downloading = false;
        stopListening();
        if (m_loaderFill) {
            float h = m_loaderFill->getContentSize().height;
            m_loaderFill->setContentSize({m_loaderBarW, h});
        }
        // Song select plays it straight away from now on.
        for (auto& entry : m_entries) {
            if (entry.level == level && songID > 0 && songs->isSongDownloaded(songID)) entry.songPath = songs->pathForSong(songID);
        }
    }
}

void SongSelect::levelDownloadFinished(GJGameLevel* level) {
    auto glm = GameLevelManager::sharedState();
    if (glm->m_levelDownloadDelegate == this) glm->m_levelDownloadDelegate = nullptr;
    // GD may hand back a fresh copy rather than the saved level: keep its data.
    if (level && m_loaderLevel && level != m_loaderLevel.data()
        && level->m_levelID.value() == m_loaderLevel->m_levelID.value()
        && std::string(m_loaderLevel->m_levelString).empty()) {
        m_loaderLevel->m_levelString = level->m_levelString;
    }
}

void SongSelect::levelDownloadFailed(int) {
    auto glm = GameLevelManager::sharedState();
    if (glm->m_levelDownloadDelegate == this) glm->m_levelDownloadDelegate = nullptr;
    downloadFailed("couldn't download the level");
}

void SongSelect::downloadSongFailed(int id, GJSongError) {
    if (m_loaderLevel && id == m_loaderLevel->m_songID) downloadFailed("couldn't download the song");
}

void SongSelect::downloadFailed(char const* message) {
    if (!m_downloading || m_downloadFailed) return;
    m_downloadFailed = true;
    if (m_loaderStatus) {
        m_loaderStatus->setString(message);
        m_loaderStatus->setColor({255, 110, 110});
    }
}

void SongSelect::stopListening() {
    auto glm = GameLevelManager::sharedState();
    if (glm->m_levelDownloadDelegate == this) glm->m_levelDownloadDelegate = nullptr;
    MusicDownloadManager::sharedState()->removeMusicDownloadDelegate(this);
}

} // namespace lazer
