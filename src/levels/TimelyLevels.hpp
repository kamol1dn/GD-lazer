#pragma once

#include <Geode/Geode.hpp>

// GD's daily, weekly and event levels ("timely" levels), for song select's
// pages for them. GD's own page for each (DailyLevelPage) does the work,
// hidden: it asks the server which level is current and how long it has
// left, downloads it, keeps the countdown, knows when the reward can be
// claimed, and offers to skip to a newer one. Those are the parts a page of
// our own would have to guess at (GD keeps your level until its reward is
// claimed, even after a newer one is set), so they stay GD's; song select
// reads the page's state off it every frame.
namespace lazer::timely {

enum class State {
    Loading,    // asking the server, or the level is on its way
    Ready,      // a level to play
    Waiting,    // its reward was claimed: nothing until the next one
    Failed,     // the server couldn't be reached
};

struct Status {
    State state = State::Loading;
    GJGameLevel* level = nullptr;   // GD's copy of the current level (Ready)
    int dailyID = 0;                // the one you have
    int activeID = 0;               // the server's current one (differs: a newer one exists)
    int secondsLeft = 0;            // until the next one (0: not known, or an event)
    bool completed = false;         // beaten as this daily (GD's own record)
    bool claimable = false;         // GD's claim button is up
    bool skippable = false;         // GD offers to skip to the newer one
    bool downloading = false;       // the level's data is still on its way
    // Changes whenever any of the above does (polled).
    unsigned version = 0;
};

// Runs GD's page for the type under `host` (so its timers and animations
// tick), making it (or making it again) when needed.
void attach(GJTimedLevelType type, cocos2d::CCNode* host);
// Takes it back out when the host leaves; it keeps its state for next time.
void detach(GJTimedLevelType type);
Status status(GJTimedLevelType type);
// After a play: GD's page rebuilds its node (the claim button appears).
void refresh(GJTimedLevelType type);
// After a failure: asks again.
void retry(GJTimedLevelType type);
// Presses GD's claim: GD hands out the reward and shows it, then moves on
// to the next level if one is set.
void claim(GJTimedLevelType type);
// GD's skip to the newer level.
void skip(GJTimedLevelType type);
// "1d 3h 22m" / "3h 22m 05s" (what the page shows).
std::string timeLeft(int seconds);

} // namespace lazer::timely
