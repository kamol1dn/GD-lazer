#pragma once

#include <string>

// The cursor's remarks around the menus (its speech bubble, see MenuCursor):
// a topic picks one of its lines at random. Nothing without the cursor.
namespace lazer::quips {

// Says one of the topic's lines. `chance` is how often it bothers (1 =
// always); a few seconds' cooldown keeps it from chattering.
void say(char const* topic, float chance = 1.f);
// Says exactly this, with the same cooldown.
void sayLine(std::string const& line);
// Says this now, cooldown or not (a line timed to the music can't wait), and
// starts the cooldown over.
void sayNow(std::string const& line);

// Every frame while the cursor shows: Dash's voice (MDK's song, the intro's)
// echoed by the cursor as it's heard, whenever GD's music channel plays it.
void followSong();

// Counts presses of `key`: true on the `count`th within `seconds` (then it
// starts over), for lines about hammering a button.
bool spam(char const* key, int count, float seconds);

} // namespace lazer::quips
