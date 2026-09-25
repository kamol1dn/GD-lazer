# Lazer UI

> [!IMPORTANT]
> **This is a completely vibecoded project.** It was built almost entirely with AI, as a personal experiment to make Geometry Dash look and feel like osu!lazer.
>
> **It is not intended for Geode's official mod index, and never will be.** Please do not submit it there or ask for it to be added.
>
> Builds are made by GitHub Actions from this repo's source: grab the `.geode` from [Releases](https://github.com/kamol1dn/GD-lazer/releases) or the latest [Actions](https://github.com/kamol1dn/GD-lazer/actions) run. They are not reviewed by Geode, so install manually and at your own risk. Early copies shared before CI was set up were local builds.

## An apology

I submitted an early version of this mod to the Geode index as a local build, without knowing how the process works. That was my mistake, and I'm sorry to the Geode reviewers for the extra work it caused. The listing has been taken down, and this mod will stay off the index.

I also understand the concern about how closely it follows osu!lazer's design. Credit for that design belongs to ppy and the osu! team.

## About

A Geode mod that rebuilds Geometry Dash's menus in the style of osu!lazer: a music-reactive main menu, a song-select screen for your levels, full-screen overlays for settings, chests, achievements and stats, and an intro and outro, all animated and with sounds.

> [!WARNING]
> **Alpha version, for testing.** Lazer UI is early and under active development, and it is **prone to crashes**. It hooks deep into GD's menus, so expect bugs, crashes and unfinished screens, especially alongside other menu mods. Back up your save (or use cloud save) before trying it, and please report crashes with the crash log.

It replaces GD's menus, not its gameplay. GD's own screens and handlers still run underneath, so progress, saving, account features and other mods keep working.

> Not affiliated with or endorsed by ppy or osu!. Animation behaviour is adapted from the MIT-licensed [osu!](https://github.com/ppy/osu) and [osu-framework](https://github.com/ppy/osu-framework) source code.

## Features

**Main menu**
- Pulsing logo with an audio visualiser, beat-synced side flashes and drifting triangles
- Button bar with **play**, **create** and **browse** submenus, replacing GD's confusing creator hub:
  - play: main levels, daily, gauntlets, map packs, the tower
  - create: my levels, new level, my lists
  - browse: search, featured, lists, hall of fame
- Toolbar with every vanilla and mod menu button, plus leaderboards, quests, paths, weekly, event, vault and treasure room
- Blurred, parallax background that crossfades to the playing level's thumbnail

**Music player**
- The menu plays the downloaded songs of your saved levels instead of the menu loop
- Now-playing card with previous / play / next / shuffle, a seek bar and a song ticker
- Block songs you never want to hear (sound-effect packs and the like); tracks under 30 s are skipped

**Song select (play → main levels)**
- RobTop's levels and your saved levels in one curved carousel, with search, groups and sorting
- The selected level's details, a preview of its song, and its thumbnail as the background
- Playing a level, or backing out of GD's level page, returns to song select

**Overlays**
- Searchable settings covering GD's options and the mod's own, including account actions (save, load, refresh login, unlink)
- Daily chests, achievements (filters, search, categories), statistics
- Account card and a redesigned profile page for any player
- GD's popups restyled to match

**Startup and exit**
- Black loading screen with a spinner, then an animated intro while the first song fades in
- Outro when quitting

**Sound:** hover and click sounds on every control, with their own volume setting.

## Optional integrations

None of these are required. The matching extras appear when a mod is installed.

| Mod | Adds |
|---|---|
| Level Thumbnails (`cdc.level_thumbnails`) | Level thumbnails for backgrounds and song-select panels |
| Separate Dual Icons (`weebify.separate_dual_icons`) | Player 2 icons on the account card and your profile |
| Better Progression (`itzkiba.better_progression`) | Level badge and EXP bar |
| Globed (`dankmeme.globed2`) | A proper "multiplayer" toolbar button |

[Image Plus](https://github.com/Prevter/ImagePlus) (`prevter.imageplus`) is required, to decode the WebP thumbnails.

## How it works

- **`early-load`** is set so the mod can restyle GD's loading screen from its first frame. Nothing else runs early. At that point the mod's own resources aren't loaded yet, so the loading screen is drawn entirely in code.
- **GD layers run hidden.** Several overlays drive GD's own layers (RewardsPage, ProfilePage, AccountLayer, CreatorLayer...) kept hidden and non-interactive, and call their handlers. GD's logic, networking and saving are never reimplemented.
- **Networking:** only level thumbnails, fetched from the Level Thumbnails community server (`levelthumbs.prevter.me`) and cached on disk. No accounts, analytics or other requests.
- **Settings:** everything can be switched off. `enabled` turns the whole mod off; the intro/outro, music player, popup restyle, profile restyle, background dim, blur and triangles each have their own toggle.

## Building

Requires the [Geode SDK](https://docs.geode-sdk.org/) (v5.10.1) and its CLI.

```bash
geode sdk install-binaries
cmake -B build -A x64
cmake --build build --config RelWithDebInfo
```

The build installs the `.geode` into your GD mods folder.

Icon glyphs are baked into a bitmap font at build time. To add one, list it in `tools/gen_icons.py` and run it: the script updates both `src/ui/core/Text.hpp` and the charset in `mod.json`.

## Source layout

```
src/
  main.cpp            main menu (MenuLayer hook), toolbar, submenus, intro / outro hookup
  audio/              UI sounds, menu music player, audio analysis (beats, spectrum)
  levels/             level library for song select
  integrations/       level thumbnails, optional mod integrations
  settings/           settings content, account actions, GD option mapping
  ui/core/            shared building blocks: easing, text, rounded boxes, scroll areas
  ui/menu/            logo, button system, background, toolbar, music card, account card
  ui/overlays/        full-screen overlays and popup restyling
  ui/select/          song select
  ui/startup/         loading screen and intro
tools/gen_icons.py    icon font generator
```

## Credits

- UI sounds from [osu-resources](https://github.com/ppy/osu-resources) by ppy Pty Ltd, [CC-BY-NC 4.0](https://creativecommons.org/licenses/by-nc/4.0/), converted to Ogg Vorbis. This is why the mod must stay free.
- Motion and layout adapted from [osu!](https://github.com/ppy/osu) and [osu-framework](https://github.com/ppy/osu-framework) (MIT).
- Font: [Outfit](https://github.com/Outfitio/Outfit-Fonts) (SIL Open Font License).
- Icons: [Font Awesome Free](https://fontawesome.com/) (solid; icons CC BY 4.0, font SIL OFL).
