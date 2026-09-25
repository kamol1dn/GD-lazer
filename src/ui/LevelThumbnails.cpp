#include "LevelThumbnails.hpp"

#include <Geode/Geode.hpp>
#include <Geode/ui/LazySprite.hpp>
#include <Geode/utils/web.hpp>

#include <thread>
#include <unordered_map>
#include <unordered_set>

using namespace geode::prelude;

namespace lazer::thumbnails {

namespace {
    // "medium" is plenty for a blurred, dimmed backdrop and ~100 KB per level.
    constexpr auto URL = "https://levelthumbs.prevter.me/thumbnail/{}/medium";

    using Callback = std::function<void(CCTexture2D*)>;

    struct State {
        std::unordered_map<int, Ref<CCTexture2D>> textures;
        std::unordered_set<int> missing; // 404s: this session won't ask again
        std::unordered_map<int, std::vector<Callback>> waiting;
        std::unordered_map<int, Ref<LazySprite>> decoding;
    };

    State& state() {
        static State s;
        return s;
    }

    std::filesystem::path cachePath(int id) {
        return Mod::get()->getSaveDir() / "thumbnails" / fmt::format("{}.webp", id);
    }

    void finish(int id, CCTexture2D* texture, bool missing) {
        auto& s = state();
        if (texture) s.textures[id] = texture;
        if (missing) s.missing.insert(id);
        auto node = s.waiting.extract(id);
        if (node.empty()) return;
        for (auto& cb : node.mapped()) cb(texture);
    }

    void decode(int id, std::filesystem::path const& path) {
        auto sprite = LazySprite::create({1, 1}, false);
        state().decoding[id] = sprite;
        sprite->setLoadCallback([id, path](Result<> res) {
            auto& s = state();
            Ref<LazySprite> sprite = s.decoding[id];
            s.decoding.erase(id);
            CCTexture2D* texture = res && sprite ? sprite->getTexture() : nullptr;
            if (!res) {
                log::warn("Couldn't decode thumbnail for level {}: {}", id, res.unwrapErr());
                std::error_code ec;
                std::filesystem::remove(path, ec); // corrupt or unsupported: fetch again next time
            }
            finish(id, texture, false);
            // Don't destroy the sprite from inside its own callback.
            Loader::get()->queueInMainThread([sprite] {});
        });
        sprite->loadFromFile(path);
    }
}

void fetch(int levelID, Callback callback) {
    auto& s = state();
    if (levelID <= 0 || s.missing.contains(levelID)) return callback(nullptr);
    if (auto it = s.textures.find(levelID); it != s.textures.end()) return callback(it->second);

    auto& waiting = s.waiting[levelID];
    waiting.push_back(std::move(callback));
    if (waiting.size() > 1) return; // already on its way

    auto path = cachePath(levelID);
    std::error_code ec;
    if (std::filesystem::exists(path, ec)) {
        decode(levelID, path);
        return;
    }

    // Download on a worker thread. (Geode's coroutine-based web API crashes
    // this MSVC version's code generator, so use the blocking call instead.)
    std::thread([levelID, path] {
        auto res = web::WebRequest().timeout(std::chrono::seconds(15)).getSync(fmt::format(URL, levelID));
        bool saved = false;
        if (res.ok()) {
            std::error_code ec;
            std::filesystem::create_directories(path.parent_path(), ec);
            auto written = file::writeBinary(path, res.data());
            if (!written) log::warn("Couldn't cache thumbnail for level {}: {}", levelID, written.unwrapErr());
            saved = bool(written);
        }
        // 404 = nobody made a thumbnail for this level yet. Anything else
        // (offline, server hiccup) may work next time.
        bool missing = res.code() == 404;
        Loader::get()->queueInMainThread([levelID, path, saved, missing] {
            if (saved) decode(levelID, path);
            else finish(levelID, nullptr, missing);
        });
    }).detach();
}

void fetchFirst(std::vector<int> levelIDs, std::function<void(CCTexture2D*, int)> callback) {
    if (levelIDs.empty()) return callback(nullptr, 0);
    int id = levelIDs.front();
    fetch(id, [id, rest = std::vector<int>(levelIDs.begin() + 1, levelIDs.end()), callback](CCTexture2D* texture) mutable {
        if (texture) return callback(texture, id);
        fetchFirst(std::move(rest), std::move(callback));
    });
}

} // namespace lazer::thumbnails
