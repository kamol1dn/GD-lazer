// Restyles GD's popups (FLAlertLayer and all its subclasses: rewards, info,
// confirmations, ...) the osu! way: dark rounded panel, Outfit text, flat
// buttons. GD's logic and art (chests, icons) keep working underneath:
// replaced backgrounds are hidden, never removed.

#include "../core/RoundedBox.hpp"
#include "../core/Text.hpp"
#include "../core/Theme.hpp"

#include <Geode/Geode.hpp>
#include <Geode/modify/FLAlertLayer.hpp>

#ifdef GEODE_IS_WINDOWS
#include <Windows.h>
#else
#include <dlfcn.h>
#endif

using namespace geode::prelude;

namespace lazer {

namespace {
    // Only restyle GD's own popup classes; other mods' popups keep their design.
    bool isVanillaClass(CCObject* obj) {
#ifdef GEODE_IS_WINDOWS
        static uintptr_t start = 0, end = 0;
        if (!start) {
            start = geode::base::get();
            auto dos = reinterpret_cast<IMAGE_DOS_HEADER*>(start);
            auto nt = reinterpret_cast<IMAGE_NT_HEADERS*>(start + dos->e_lfanew);
            end = start + nt->OptionalHeader.SizeOfImage;
        }
        auto vtable = *reinterpret_cast<uintptr_t*>(obj);
        return vtable >= start && vtable < end;
#else
        // The vtable lives in whichever binary defines the class: GD's own
        // (libcocos2dcpp.so on Android) or another mod's library.
        Dl_info info {};
        if (!dladdr(*reinterpret_cast<void**>(obj), &info)) return false;
        return reinterpret_cast<uintptr_t>(info.dli_fbase) == geode::base::get();
#endif
    }

    bool endsWith(std::string_view s, std::string_view suffix) {
        return s.size() >= suffix.size() && s.substr(s.size() - suffix.size()) == suffix;
    }

    bool hasAncestor(CCNode* node, CCNode* stop, auto pred) {
        for (auto n = node->getParent(); n && n != stop; n = n->getParent()) {
            if (pred(n)) return true;
        }
        return false;
    }

    // Swap GD's bitmap fonts for Outfit, keeping the same line height.
    void restyleLabel(CCLabelBMFont* label) {
        auto file = label->getFntFile();
        if (!file) return;
        std::string_view fnt = file;
        char const* replacement = nullptr;
        if (endsWith(fnt, "bigFont.fnt")) replacement = "outfit-semibold.fnt"_spr;
        else if (endsWith(fnt, "goldFont.fnt")) replacement = "outfit-bold.fnt"_spr;
        else if (endsWith(fnt, "chatFont.fnt")) replacement = "outfit-regular.fnt"_spr;
        if (!replacement) return;

        auto oldConfig = label->getConfiguration();
        float oldHeight = oldConfig ? oldConfig->m_nCommonHeight : 0;
        bool gold = endsWith(fnt, "goldFont.fnt");
        auto color = label->getColor();

        label->setFntFile(replacement);
        auto newConfig = label->getConfiguration();
        float newHeight = newConfig ? newConfig->m_nCommonHeight : 0;
        if (oldHeight > 0 && newHeight > 0) {
            float ratio = oldHeight / newHeight;
            label->setScaleX(label->getScaleX() * ratio);
            label->setScaleY(label->getScaleY() * ratio);
        }
        // goldFont's colour is baked into its texture; titles become white like osu!.
        label->setColor(gold ? theme::CONTENT1 : color);
    }

    RoundedBox* replaceWithBox(CCNode* old, ccColor4B color, float radius, bool shadow) {
        auto parent = old->getParent();
        if (!parent) return nullptr;
        auto size = CCSize(old->getContentSize().width * old->getScaleX(),
                           old->getContentSize().height * old->getScaleY());
        auto box = RoundedBox::create(size, radius, color);
        box->setAnchorPoint(old->getAnchorPoint());
        box->setPosition(old->getPosition());
        if (old->isIgnoreAnchorPointForPosition()) {
            box->setAnchorPoint({0, 0});
        }
        if (shadow) box->setShadow(radius * 1.5f, {0, 0, 0, 110});
        box->setID("restyled-bg"_spr);
        parent->addChild(box, old->getZOrder());
        // Keep draw order: sit exactly where the old background was.
        box->setOrderOfArrival(old->getOrderOfArrival());
        old->setVisible(false);
        return box;
    }

    void restyleButton(ButtonSprite* button) {
        // ButtonSprite = 9-slice background + label (+ optional icon).
        for (auto child : CCArrayExt<CCNode*>(button->getChildren())) {
            if (auto bg = typeinfo_cast<CCScale9Sprite*>(child)) {
                replaceWithBox(bg, theme::COLOUR3, 4.f, false);
                break;
            }
        }
        if (button->m_label) restyleLabel(button->m_label);
    }

    void collect(CCNode* node, std::vector<CCNode*>& out) {
        out.push_back(node);
        for (auto child : CCArrayExt<CCNode*>(node->getChildren())) collect(child, out);
    }

    void restylePopup(FLAlertLayer* popup) {
        auto root = popup->m_mainLayer;
        if (!root) return;

        // The main panel: the largest 9-slice directly in the main layer.
        CCScale9Sprite* panel = nullptr;
        float largest = 0;
        for (auto child : CCArrayExt<CCNode*>(root->getChildren())) {
            if (auto s = typeinfo_cast<CCScale9Sprite*>(child)) {
                auto size = s->getContentSize();
                float area = size.width * s->getScaleX() * size.height * s->getScaleY();
                if (area > largest) { largest = area; panel = s; }
            }
        }
        if (panel) replaceWithBox(panel, theme::BACKGROUND4, 10.f, true);

        std::vector<CCNode*> nodes;
        collect(root, nodes);
        for (auto node : nodes) {
            if (!node->isVisible() || node == panel) continue;
            bool inInput = hasAncestor(node, root, [](CCNode* n) { return typeinfo_cast<CCTextInputNode*>(n); });

            if (auto button = typeinfo_cast<ButtonSprite*>(node)) {
                restyleButton(button);
            } else if (auto label = typeinfo_cast<CCLabelBMFont*>(node)) {
                bool inButton = hasAncestor(node, root, [](CCNode* n) { return typeinfo_cast<ButtonSprite*>(n); });
                if (!inButton && !inInput) restyleLabel(label);
            } else if (auto inner = typeinfo_cast<CCScale9Sprite*>(node)) {
                // Translucent inner boxes (GD's "square02") become dark rounded panels.
                bool inButton = hasAncestor(node, root, [](CCNode* n) { return typeinfo_cast<ButtonSprite*>(n); });
                if (!inButton && !inInput && inner->getOpacity() < 255) {
                    auto box = replaceWithBox(inner, theme::BACKGROUND6, 6.f, false);
                    if (box) box->setOpacity(std::min<GLubyte>(255, inner->getOpacity() * 2));
                }
            }
        }
    }
}

class $modify(LazerPopup, FLAlertLayer) {
    struct Fields {
        bool styled = false;
    };

    void onEnter() {
        FLAlertLayer::onEnter();
        // GD's FLAlertLayer::onEnter shares its address with other layers'
        // identical onEnter (e.g. ProfilePage's comment list), so this hook
        // also runs for non-popups. Check the real type before touching members.
        if (!typeinfo_cast<FLAlertLayer*>(static_cast<CCNode*>(this))) return;
        if (m_fields->styled) return;
        m_fields->styled = true;
        if (!Mod::get()->getSettingValue<bool>("restyle-popups")) return;
        if (!isVanillaClass(this)) return;
        // GD pages we run hidden behind our own UI (profiles, chests).
        if (this->getUserObject("hidden"_spr)) return;
        restylePopup(this);
    }
};

} // namespace lazer
