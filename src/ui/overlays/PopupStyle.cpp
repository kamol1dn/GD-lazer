// Restyles GD's popups (FLAlertLayer and all its subclasses: rewards, info,
// confirmations, ...) the osu! way: dark rounded panel, Outfit text, flat
// buttons. GD's logic and art (chests, icons) keep working underneath:
// replaced backgrounds are hidden, never removed.

#include "../core/RoundedBox.hpp"
#include "../core/Text.hpp"
#include "../core/Theme.hpp"
#include "Dialog.hpp"

#include <Geode/Geode.hpp>
#include <Geode/modify/ButtonSprite.hpp>
#include <Geode/modify/FLAlertLayer.hpp>
#include <Geode/modify/TextArea.hpp>

#ifdef GEODE_IS_WINDOWS
#include <Windows.h>
#else
#include <dlfcn.h>
#endif

using namespace geode::prelude;

// TextArea keeps no copy of its text: remember it, so a restyled one can be
// rebuilt in another font.
class $modify(LazerTextArea, TextArea) {
    struct Fields {
        std::string text;
        bool known = false;
    };

    void setString(gd::string text) {
        m_fields->text = text;
        m_fields->known = true;
        TextArea::setString(text);
        // Restyled: the new lines are in our distance-field font too.
        if (std::string_view(m_fontFile).ends_with("-sdf.fnt") && m_label) {
            // GD aligns each line by its unscaled width (right for GD's fonts
            // at scale 1): with our scale the lines drift off centre.
            for (auto line : CCArrayExt<CCNode*>(m_label->getChildren())) {
                line->setPositionX(line->getPositionX() * line->getScaleX());
            }
            std::function<void(CCNode*)> apply = [&](CCNode* node) {
                if (auto label = typeinfo_cast<CCLabelBMFont*>(node)) lazer::useHaloShader(label);
                else for (auto child : CCArrayExt<CCNode*>(node->getChildren())) apply(child);
            };
            apply(m_label);
        }
    }
};

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

    // The Outfit font standing in for one of GD's, or null to keep it.
    char const* replacementFor(std::string_view fnt) {
        if (endsWith(fnt, "bigFont.fnt")) return sdfFont(Weight::SemiBold);
        if (endsWith(fnt, "goldFont.fnt")) return sdfFont(Weight::Bold);
        if (endsWith(fnt, "chatFont.fnt")) return sdfFont(Weight::Regular);
        return nullptr;
    }

    float commonHeight(char const* fnt) {
        auto config = FNTConfigLoadFile(fnt);
        return config ? config->m_nCommonHeight : 0;
    }

    // Multi-line text (alert bodies, level descriptions, chat) lays out its lines
    // with its font's metrics, and colour tags colour single letters: swapping the
    // line labels' font throws both off. GD rebuilds it in Outfit instead.
    void restyleTextArea(TextArea* area) {
        auto fields = static_cast<LazerTextArea*>(area)->m_fields.self();
        if (!fields->known) return;
        std::string old = area->m_fontFile;
        auto replacement = replacementFor(old);
        if (!replacement) return;
        float oldHeight = commonHeight(old.c_str());
        float newHeight = commonHeight(replacement);
        if (oldHeight <= 0 || newHeight <= 0) return;
        area->m_fontFile = replacement;
        area->m_scale *= oldHeight / newHeight;
        area->setString(fields->text);
    }

    // Swap GD's bitmap fonts for Outfit, keeping the same line height.
    void restyleLabel(CCLabelBMFont* label) {
        auto file = label->getFntFile();
        if (!file) return;
        auto replacement = replacementFor(file);
        if (!replacement) return;
        std::string_view fnt = file;

        auto oldConfig = label->getConfiguration();
        float oldHeight = oldConfig ? oldConfig->m_nCommonHeight : 0;
        bool gold = endsWith(fnt, "goldFont.fnt");
        auto color = label->getColor();

        label->setFntFile(replacement);
        useHaloShader(label);
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

    // GD's button textures in our colours. A group of buttons (the editor's
    // colour channels, a trigger's modes) shows the unpicked ones grey and the
    // picked one in another colour; red is GD's special button.
    constexpr char const* GREY_BUTTON = "GJ_button_04.png";
    constexpr char const* RED_BUTTON = "GJ_button_05.png";
    constexpr ccColor4B RED {0xcc, 0x33, 0x33, 255};

    ccColor4B buttonColour(std::string_view texture) {
        if (endsWith(texture, GREY_BUTTON)) return theme::DARK3;
        if (endsWith(texture, RED_BUTTON)) return RED;
        return theme::COLOUR3;
    }

    ccColor4B buttonColour(CCScale9Sprite* bg) {
        auto batch = bg->_scale9Image;
        auto texture = batch ? batch->getTexture() : nullptr;
        if (!texture) return theme::COLOUR3;
        // The texture cache keeps one texture per file, the one the 9-slice was made from.
        auto cache = CCTextureCache::sharedTextureCache();
        for (auto file : {GREY_BUTTON, RED_BUTTON}) {
            if (texture == cache->addImage(file, false)) return buttonColour(file);
        }
        return theme::COLOUR3;
    }

    void restyleButton(ButtonSprite* button) {
        // ButtonSprite = 9-slice background + label (+ optional icon).
        for (auto child : CCArrayExt<CCNode*>(button->getChildren())) {
            if (auto bg = typeinfo_cast<CCScale9Sprite*>(child)) {
                replaceWithBox(bg, buttonColour(bg), 4.f, false);
                break;
            }
        }
        if (auto label = button->m_label) {
            restyleLabel(label);
            // GD shrinks its wide bigFont to fit the button; Outfit is narrower,
            // so it comes out too big: cap it (line height) at 60% of the button.
            float cap = button->getContentSize().height * 0.6f;
            float height = label->getScaledContentSize().height;
            if (cap > 0 && height > cap) {
                float f = cap / height;
                label->setScaleX(label->getScaleX() * f);
                label->setScaleY(label->getScaleY() * f);
            }
        }
    }

    void collect(CCNode* node, std::vector<Ref<CCNode>>& out) {
        out.push_back(node);
        for (auto child : CCArrayExt<CCNode*>(node->getChildren())) collect(child, out);
    }

    bool under(CCNode* node, CCNode* root) {
        for (auto n = node; n; n = n->getParent()) {
            if (n == root) return true;
        }
        return false;
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

        // Text areas first: rebuilding one frees its old line labels.
        std::vector<Ref<CCNode>> nodes;
        collect(root, nodes);
        for (auto const& node : nodes) {
            if (auto area = typeinfo_cast<TextArea*>(node.data()); area && node->isVisible()) restyleTextArea(area);
        }
        nodes.clear();
        collect(root, nodes);
        // Restyling a label rebuilds its letters: the ones gathered before are
        // held on to (not freed under the loop) and, no longer in the popup,
        // skipped.
        for (auto const& held : nodes) {
            auto node = held.data();
            if (!under(node, root)) continue;
            if (!node->isVisible() || node == panel) continue;
            bool inInput = hasAncestor(node, root, [](CCNode* n) { return typeinfo_cast<CCTextInputNode*>(n); });
            // Done above (or left alone when its text isn't known).
            if (hasAncestor(node, root, [](CCNode* n) { return typeinfo_cast<TextArea*>(n) || typeinfo_cast<MultilineBitmapFont*>(n); })) continue;

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

bool popupOnTop() {
    auto scene = CCDirector::get()->getRunningScene();
    if (!scene) return false;
    for (auto child : CCArrayExt<CCNode*>(scene->getChildren())) {
        if (!child->isVisible() || child->getUserObject("hidden"_spr)) continue;
        if (typeinfo_cast<FLAlertLayer*>(child) || typeinfo_cast<GJDropDownLayer*>(child) || typeinfo_cast<Dialog*>(child)) return true;
    }
    return false;
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
        // Our own popups (built from Geode's popup classes) opt in with this marker.
        if (!isVanillaClass(this) && !this->getUserObject("restyle"_spr)) return;
        // GD pages we run hidden behind our own UI (profiles, chests).
        if (this->getUserObject("hidden"_spr)) return;
        restylePopup(this);
    }
};

// GD shows which button of a group is picked (the editor's colour channels, the
// channel picker behind a colour trigger's "Color ID", trigger modes) by giving
// each a new background: updateBGImage drops the old 9-slice, adds a visible
// one over our box and lays the label out again for GD's font (#58). A
// restyled button keeps its look, and shows the pick in its box's colour.
class $modify(LazerButtonSprite, ButtonSprite) {
    void updateBGImage(char const* file) {
        auto box = typeinfo_cast<RoundedBox*>(this->getChildByID("restyled-bg"_spr));
        if (!box) return ButtonSprite::updateBGImage(file);
        // The caption stays the same: the layout from before is the right one.
        auto size = this->getContentSize();
        auto position = this->getPosition();
        auto parent = this->getParent();
        auto parentSize = parent ? parent->getContentSize() : CCSize {};
        auto label = m_label;
        float scaleX = label ? label->getScaleX() : 1.f, scaleY = label ? label->getScaleY() : 1.f;
        auto labelPosition = label ? label->getPosition() : CCPoint {};

        ButtonSprite::updateBGImage(file);

        if (m_BGSprite) m_BGSprite->setVisible(false);
        if (label) {
            label->setScaleX(scaleX);
            label->setScaleY(scaleY);
            label->setPosition(labelPosition);
        }
        this->setContentSize(size);
        this->setPosition(position);
        if (parent) parent->setContentSize(parentSize);
        box->setFillColor(buttonColour(file ? file : ""));
    }
};

} // namespace lazer
