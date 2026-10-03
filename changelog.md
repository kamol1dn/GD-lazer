# Changelog

## v0.6.2

- Fixed: tapping the search box while a level was loading from song select focused it through the loader (thanks airalics for the report)
- Fixed: backing out of a level page reached from a creator's profile (opened from song select's level page) jumped back to song select instead of the creator's levels (thanks airalics for the report)
- Fixed: a profile's "comment history" button did nothing (thanks airalics for the report)
- Fixed: the level page's creator button opened a player that couldn't load when GD's own creator button wasn't available: it looked the profile up by user ID instead of account ID
- The intro stays in time with its theme: the theme is loaded before the intro starts, and the timeline follows the audio's position through long frames (thanks airalics for the report)

## v0.6.1

- Friends: a toolbar button opens a page where you search players by name or user ID (ten a page) or browse your friends, as cards two across with their icon, name, moderator badge and stars, moons, demons and user coins; a card opens their profile (souply, thanks Gilsonbro for asking)
- Settings > Layout: "Use player cube colors" draws the big menu cube in your icon's own colours instead of the ones picked to stand out against the disc (souply, thanks cycle00 for the report)
- Browsing online is song select: search, featured, lists, hall of fame, magic, recent and sent are the same screen as your levels, with the results as the carousel on the right (the next page arrives as you scroll), the selected level's details on the left (its description, song, progress, comments and leaderboard) and play, random and the level page in the footer. Lists open like map packs, a row that unfolds into its levels. Above the carousel: the search box, levels or lists, a sort menu, a filters menu with GD's options as chips (on every page, not just the search), refresh, and a pager beside the count that steps through the pages or jumps to a typed one
- Levels open on their own page instead of GD's: the level's picture across the top with its rating, difficulty, stars, coins, song, downloads and likes, then play (GD downloads it first if it must), heart, like, comments, add to list, copy the ID and the song's download; below, its description, details (creator, song, length, objects, version, when it was uploaded), your progress (normal and practice, or a platformer's best time, with attempts, jumps and orbs) and its scores (top, this week or friends, loaded on request since that also uploads your best). "GD's page" shows the real thing for anything else
- Ventilla support: with JoseII's Ventilla installed, its live radio can be the menu's music instead of your levels' songs. The music player gets a radio button to switch between the two, shows the stream's current track, and the logo's visualiser follows the radio; Settings > Audio has the same switch and Ventilla's own options as rows of their own: the radio's volume and fade, where else it plays (levels, practice, the pause menu, the editor, the background, GD's shops and secret rooms) and its buttons (thanks BlueCrafter12 for the suggestion)
- macOS: the mod builds for Macs too (Apple Silicon and Intel), with GD's own cursor and dialogs in place of the osu! cursor and Lazer's graphics and parental dialogs for now. The port is souply's work, including the next two
- The menu's background blur is cheaper: both blur passes run at a quarter of the screen with half the texture reads, and a level image that has finished fading in keeps its blur instead of being blurred again every frame. Macs went from 20 to 60 fps on the menu (souply's optimisation, taken to every platform)
- Updating from Lazer settings checks the download is this mod at that version, for this platform, GD and Geode, before it replaces the installed one (souply)
- Paths: a path you haven't bought is locked until you buy it (it was looked up under the wrong item, so bought and unbought paths could swap), and the page says stars and moons, not orbs, since those are what fill a path (thanks Gecko7030 for the report)
- Search: buttons other mods add to GD's search screen (Integrated Demonlist's, Level Grind's...) sit after refresh on the search page (thanks L4mbads for the suggestion)
- Pause menu: more room between "paused" and the level's name, and between the song progress and the music and effects sliders (thanks L4mbads for the suggestion)
- Fixed: buttons other mods put on GD's end-level screen itself (a hide-the-screen button, a death tracker) showed through the results screen; they're hidden and forwarded to its footer like the rest (thanks L4mbads for the report)
- The osu! cursor shows exactly when the system cursor would: it reads the cursor GD and other mods actually set (and Windows' own hide count) instead of following GD's requests, so a mod's menu over gameplay (QOLMod, Eclipse) leaves it the way vanilla would when it closes (thanks L4mbads for the report)
- Fixed: crash on Settings > Audio > Soundtracks (thanks 1mercdev for the report)
- Fixed: the pause menu staying up (and piling up) after unpausing with a key such as space, with Custom Keybinds (thanks 1mercdev for the report)
- Fixed: the search page's demon kind (easy to extreme) had no effect, every demon came back (thanks TraesherHDx for the report)
- Fixed: the cursor sitting in the middle of the screen during a level with GD's lock cursor on and show cursor off (thanks Kierek3 for the report)
- Fixed: punctuation (. , ! ? @ # and the rest) couldn't be typed into the mod's search boxes and the comment editor; GD's text field only let letters and digits through (thanks GClav and LJxG0D for the report)
- Fixed: a profile said "add friend" for people who already are: it read their request-privacy setting as the relationship

## v0.6.0

- You're one of few valuable testers: expect rare crashes. A line on the main menu says so. Please report anything that breaks
- Play, create and browse menus rearranged: daily, weekly and the event level sit together in the play row; gauntlets and map packs are in the toolbar next to home; the right side runs achievements, statistics, leaderboards, quests, paths, then chests, vault and treasure room, then the music player and mods
- Browse: search, featured, lists, hall of fame, magic, recent and sent open as osu!-style listing pages with level cards, filters, sort tabs and endless scrolling
- Create: my levels and my lists open as orange pages with cards for each level (song, length, objects, verified / uploaded), folders, search, a new level / new list button and your uploaded levels
- Comments: a saved level's comments open as an osu!-style page (newest or top, post your own, like or dislike), also from a level's info button, with the level's description, ID and dates on top. Names open the writer's profile
- Song select opens fast with thousands of saved levels, and a browse button opens online search
- Playing from song select: the level loads while the loader is on screen, so it starts right after it. The preview picks up where the song was when you come back
- Leaderboards open as osu!'s rankings page: top 100, friends, global and creators, sorted by stars, moons, demons or user coins, with each player's icon, name and stats
- Paths open as an osu!-style page: every path with its art, rank and progress, the chosen one with its ten ranks and rewards, unlock, activate and claim its chest
- GD's shops open as osu!-style pages: your orbs or diamonds in the header, every item as a card with its price and whether you own it, and the shopkeeper still talks
- Daily chests are a popup instead of a full page: both chests side by side, a ready one opens by itself
- Pause menu and level complete screen the osu! way (Settings > Lazer UI > Gameplay > "Lazer pause and results", off by default while it's new): continue, retry and quit, practice, the volume sliders, your attempts, jumps, time and rewards, other mods' pause buttons kept
- Page headers come alive with a wash of the page's colour and osu!'s drifting triangles, and the profile cover uses the player's colours
- Updates are checked, downloaded and installed in one dialog: changelog, progress bar, restart prompt
- Exiting the game or a level, graphics options and parental controls open as osu!'s dialogs (PC)
- The cursor has opinions: shake it fast for a speech bubble, spin it past a full turn and it gets dizzy
- Icons sit centred in their buttons (the music player's controls and every close button used to sit low)
- Fixed: crash when playing from song select with Custom Keybinds installed
- Fixed: crash opening parental controls on Windows
- Fixed: crash on quit with BetterInfo installed
- Fixed: black cursor and missing thumbnails after switching fullscreen
- Fixed: a profile opened from a comment showed behind the page

## v0.5.6

- Menu sounds have their own volume now: Settings > Audio > Volume > Interface sounds is theirs alone, so with GD's SFX off the menus still make sound
- Buttons other mods add to GD's creator hub (GDDP's Demon Progression, BetterInfo...) show up in the toolbar, since Lazer UI hides the hub

## v0.5.5

- Song select has osu!'s scrollbar: drag it, or tap beside it to jump. Held, it widens, follows your finger and shows where you are in the list: the position, first letter, difficulty or progress, by the sort
- Delete a saved level from song select with the bin next to the heart
- A level whose song isn't downloaded asks first: download and play, play without music, or cancel, instead of downloading everything straight away
- Song select's confirmations are osu!'s dialogs now. Hold the red button to confirm a deletion
- Blocked songs page (Settings > Lazer UI > Music > Blocked songs): unblock songs one by one or all at once
- GD's popups are easier to read: text keeps its colours and stays centred, a dark outline keeps it readable over bright images, and button text is no longer oversized
- Fixed: the menu underneath GD's popups (daily, weekly, gauntlets...) still reacted to clicks
- Fixed: a profile opened from level comments showed behind the level info
- Fixed: the osu! cursor was drawn under Eclipse's menu (PC)

## v0.5.4

- The osu! cursor tilts as it moves, more the faster you move it. "Cursor rotation" in Settings > Lazer UI > Cursor turns off both the tilt and the drag spin
- Fixed: the main levels' songs didn't play in the menu music player or song select on Android
- The main levels' songs show their level in the now playing card

## v0.5.3

- Sharper text everywhere in Lazer UI: letters and icons are now drawn from distance fields, so they stay crisp at any size instead of going blotchy when shrunk
- The cube on the logo has a clean edge instead of a stair-stepped one
- osu!'s cursor on PC: it shrinks and glows pink when you click, turns to follow a drag, and taps. It appears once the game has loaded. Settings > Lazer UI > Cursor turns it off, changes its size or turns off the drag rotation
- Lazer UI's settings are now the first section in settings

## v0.5.2

- <cr>**IMPORTANT (Android): if nothing below shows up after updating, update by hand once.**</c> Older versions of the updater change the version number but keep running the old code. Close Geometry Dash, download `kamol1dn.lazer-ui.geode` from github.com/kamol1dn/GD-lazer/releases, **delete** the old one in `Android/media/com.geode.launcher/game/geode/mods` and copy the new one in (don't just overwrite). After that, updates apply properly
- Play a level that isn't downloaded yet straight from song select: the loader downloads the level and its song with a progress bar, then starts it (no more detour through GD's level page)
- A close button on every page, and a back button at the bottom of settings (phones had no visible way out)
- The main levels' songs play in the menu music player too
- Like and dislike posts on other players' profiles
- Fixed: the loading circle wobbled instead of spinning in place

## v0.5.1

- <cr>**IMPORTANT (Android): if nothing below shows up after updating, update by hand once.**</c> Older versions of the updater change the version number but keep running the old code. Close Geometry Dash, download `kamol1dn.lazer-ui.geode` from github.com/kamol1dn/GD-lazer/releases, **delete** the old one in `Android/media/com.geode.launcher/game/geode/mods` and copy the new one in (don't just overwrite). After that, updates apply properly
- Fixed: Enter and Space on the main menu opened GD's main levels; they now press the logo, like osu!
- Fixed: the home button in the toolbar didn't close the open page; it now closes it, and goes back a menu when nothing is open
- Fixed: Escape did nothing in song select
- Fixed: closing friend requests, friends or messages from your profile opened a second profile page
- Fixed: "view profile" in the account card did nothing while statistics, achievements, rewards or quests were open
- Fixed: the settings (and other pages) stopped scrolling with the mouse wheel after another page had been opened
- Stronger parallax by default: background 6% and menu 1.5% on PC, 8% and 2% on phones (reset the sliders in Settings > Lazer UI > Parallax to get the new values)

## v0.5.0

- <cr>**IMPORTANT (Android): if nothing below shows up after updating, update by hand once.**</c> Older versions of the updater change the version number but keep running the old code. Close Geometry Dash, download `kamol1dn.lazer-ui.geode` from github.com/kamol1dn/GD-lazer/releases, **delete** the old one in `Android/media/com.geode.launcher/game/geode/mods` and copy the new one in (don't just overwrite). After that, updates apply properly
- Tilt parallax on phones: tilt the phone and the menu background moves with it, like the mouse does on PC (the gravity sensor or accelerometer; can be turned off)
- The menu buttons move with the parallax too, a little less than the background
- Settings > Lazer UI > Parallax: background and menu parallax amounts, and the tilt toggle on phones
- Music carries over between the menu and song select: entering song select keeps the menu's song playing and selects its level; going back, the menu keeps playing what song select was on

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
