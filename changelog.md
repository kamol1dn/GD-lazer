# Changelog

## v0.4.3

- <cr>**IMPORTANT!!! Android players: this updater was broken.**</c> Updates since v0.3.0 changed the version number but kept running the old v0.3.0 code. If your logo is still pink with "GD" on it, updating here won't fix it. Reinstall once by hand:
    1. Close Geometry Dash completely
    2. Download `kamol1dn.lazer-ui.geode` from github.com/kamol1dn/GD-lazer/releases
    3. In a file manager, open `Android/media/com.geode.launcher/game/geode/mods`, **delete** the old `kamol1dn.lazer-ui.geode`, then copy the downloaded one in (delete first, don't just overwrite)
    4. Start the game: the logo shows your cube in your colours. From then on the updater works properly (Windows was never affected)

## v0.4.2

- Fixed: on Android, updates from the in-game updater could keep running the old version (the version number changed but nothing else did). If you're on Android and don't have the new logo, reinstall this version once by hand; later updates apply properly

## v0.4.1

- Starting a level plays osu!'s loader: song select fades away, the level's card scales in over its background, then the level starts (back cancels)

## v0.4.0

- The logo takes your icon's colours, with your cube in place of the "GD" text
- New intro: the logo draws itself to the opening of osu!'s triangles theme; outro says "see you next time"
- RobTop's levels (main levels and the Tower) have screenshots in song select, as panel thumbnails and the background
- Song select groups: saved (the default), official and liked; "all" is gone
- Folder dropdown for GD's saved-level folders
- Heart levels from song select; "liked" shows only hearted levels
- Delete unhearted levels (keeps levels in folders), from the footer
- Level details: a song card (download, extra songs and SFX, and Jukebox's song switching when it's installed), per-level low detail mode and disable shake, and the level's leaderboard on request (loading it syncs your progress)
- Attempts, jumps, downloads and likes on one line; the details scroll
- Fixed: pressing play let the preview song run on into the level

## v0.3.1

- Quests page fits small screens (phones with a big UI scale)
- Update prompts use the Lazer popup style
- Settings sidebar stays collapsed on touchscreens instead of covering the settings

## v0.3.0

- Play menu: separate song selects for classic and platformer levels (replacing "main levels" and "the tower")
- Platformer song select includes the Tower's levels and shows moons and best times
- Quests page in the Lazer style: quest cards with progress bars, diamond rewards and claiming, opened from the toolbar
- Updates itself: checks GitHub on start and offers to install new versions (Lazer settings > Updates)

## v0.2.0

- Android support (arm64 and armv7); releases ship one .geode for Windows and Android
- UI scale setting (Lazer UI > Layout), 150% by default on Android where screens are small
- Fixed a crash when pressing back on Android
- Toolbar buttons work on touchscreens
- Popups from other mods keep their own design on Android too

## v0.1.0

First alpha release, for testing. Expect crashes.

- osu!lazer-style main menu: logo, visualiser, button bar with play / create / browse submenus, toolbar
- Menu music player with level thumbnail backgrounds, now-playing card, song blocking
- Song select for RobTop's levels and saved levels
- Overlays: settings, daily chests, achievements, statistics, account card, profiles
- Restyled popups
- Loading screen, intro and outro
- UI sounds
- Optional integrations: Level Thumbnails, Separate Dual Icons, Better Progression, Globed
