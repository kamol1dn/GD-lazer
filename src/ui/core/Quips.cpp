#include "Quips.hpp"

#include "MenuCursor.hpp"

#include "../../audio/MusicPlayer.hpp"

#include <Geode/Geode.hpp>
#include <Geode/binding/FMODAudioEngine.hpp>
#include <array>
#include <chrono>
#include <random>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace lazer::quips {

namespace {
    // Seconds between remarks, whatever asks.
    constexpr double COOLDOWN_S = 4.0;

    struct Topic {
        std::string_view name;
        std::vector<char const*> lines;
    };

    // Lower case throughout, like the cursor's other lines.
    std::vector<Topic> const& topics() {
        static std::vector<Topic> const all {
            {"exit", {"don't go", "leaving already?", "it's dark out there", "i'll be here. alone.", "fine. go. see if i care"}},
            {"icons", {"dress up time", "new look, new you", "the cube fashion show", "ooh, what are we wearing today?"}},
            {"settings", {"still not right?", "fiddling again?", "tweak away", "the settings. my favourite."}},
            {"leaderboards", {"you're on there. somewhere.", "numbers, but ranked", "top 100? one day", "scroll down. further. further."}},
            {"achievements", {"so close on some of these", "look at all that shiny", "the trophy cabinet"}},
            {"stats", {"numbers go up", "that's a lot of attempts", "i counted them all myself", "the graphs don't lie"}},
            {"quests", {"quests. plural. ugh.", "orbs don't collect themselves", "a to-do list, but fun"}},
            {"paths", {"choose wisely", "walk the path", "every path leads to more spikes"}},
            {"chests", {"free stuff!", "chest o'clock", "what's the timer say?"}},
            {"chest-open", {"ooh, shiny", "what's inside?", "jackpot?", "loot!"}},
            {"vault", {"that's gd's, not mine", "type carefully", "the keeper hates typos"}},
            {"treasure", {"keys? you have keys?", "loot time", "shiny things behind that door"}},
            {"profile-own", {"looking good", "that's you!", "handsome", "nice stats. i've seen them."}},
            {"profile-other", {"who's this?", "stalking, are we?", "say hi from me"}},
            {"music-skip", {"nothing good today, huh", "a music critic, i see", "dj mode", "let a song finish once"}},
            {"logo-poke", {"ow", "stop poking me", "the logo has feelings", "it's not a button. ok it is. still."}},
            {"shop", {"spend responsibly", "the shopkeeper says hi", "orbs well spent?", "treat yourself"}},
            {"delete-unhearted", {"bye bye levels", "spring cleaning", "gone. reduced to atoms.", "they had a good run"}},
            {"gauntlets", {"gauntlets. also not the map packs.", "run the gauntlet", "five levels. no mercy."}},
            {"daily", {"a level a day", "fresh out the oven", "today's special"}},
            {"weekly", {"the weekly. good luck.", "it's a demon. you knew that.", "seven days to cry"}},
            {"event", {"an event! fancy", "limited time. no pressure."}},
            {"create", {"an artist!", "make something cool", "the editor awaits"}},
            {"browse", {"so many levels, so little time", "the whole internet of levels", "find something good"}},
            {"idle", {"hello? anyone there?", "i'm still here", "...bored", "did you fall asleep?", "just me and the triangles then"}},
            {"midnight", {"shouldn't you be asleep?", "it's late. one more level though", "night owl", "the sun will be up soon"}},
            {"early", {"early bird", "coffee first?", "morning! already?"}},
            {"hour", {"you've been here an hour", "an hour already. impressive.", "one hour. blink twice if you need water"}},
            {"two-hours", {"two hours. touch grass? just a thought", "two hours. i'm proud of you. worried, but proud."}},
        };
        return all;
    }

    std::mt19937& rng() {
        static std::mt19937 r {std::random_device {}()};
        return r;
    }

    double now() {
        using namespace std::chrono;
        return duration<double>(steady_clock::now().time_since_epoch()).count();
    }

    double g_lastSaid = -1000;
}

void sayLine(std::string const& line) {
    double t = now();
    if (t - g_lastSaid < COOLDOWN_S) return;
    g_lastSaid = t;
    cursorSay(line);
}

void sayNow(std::string const& line) {
    g_lastSaid = now();
    cursorSay(line);
}

// Dash's lines, measured from GD's Dash.mp3 (the grid is in IntroSequence):
// "Geometry Dash" at 15.0 s into the first drop at 16.15 s; in the build to
// the second drop (61.15 s) it counts "three, two, one" on the bars at
// 53.65, 55.52 and 57.40 s and says "Geometry Dash" again at 60.05 s. Said
// 60 ms early, the bubble takes a moment to pop in.
// Only when the position runs through a mark on its own: a seek jumps over
// it, another song isn't Dash, and Dash coming round again says them again.
void followSong() {
    struct Line {
        unsigned ms;
        char const* text;
    };
    static constexpr std::array<Line, 5> LINES {{
        {14940, "geometry dash"},
        {53590, "three"},
        {55465, "two"},
        {57340, "one"},
        {59990, "geometry dash"},
    }};
    static bool following = false;
    static unsigned lastPos = 0;

    auto engine = FMODAudioEngine::sharedEngine();
    bool dash = engine->isMusicPlaying(0)
        && geode::utils::string::endsWith(std::string(engine->getActiveMusic(0)), MusicPlayer::introFile());
    if (!dash) {
        following = false;
        return;
    }
    unsigned pos = engine->getMusicTimeMS(0);
    if (following && pos > lastPos && pos - lastPos < 500) {
        for (auto const& line : LINES) {
            if (lastPos < line.ms && pos >= line.ms) sayNow(line.text);
        }
    }
    following = true;
    lastPos = pos;
}

void say(char const* topic, float chance) {
    if (chance < 1.f && std::uniform_real_distribution<float>(0.f, 1.f)(rng()) > chance) return;
    for (auto const& t : topics()) {
        if (t.name != topic || t.lines.empty()) continue;
        sayLine(t.lines[std::uniform_int_distribution<size_t>(0, t.lines.size() - 1)(rng())]);
        return;
    }
    geode::log::warn("No quips for {}", topic);
}

bool spam(char const* key, int count, float seconds) {
    static std::unordered_map<std::string, std::vector<double>> presses;
    auto& list = presses[key];
    double t = now();
    list.push_back(t);
    while (!list.empty() && list.front() < t - seconds) list.erase(list.begin());
    if (static_cast<int>(list.size()) >= count) {
        list.clear();
        return true;
    }
    return false;
}

} // namespace lazer::quips
