#include "Text.hpp"

#include <Geode/Geode.hpp>

using namespace geode::prelude;

namespace lazer {

namespace {
    CCLabelBMFont* makeLabel(std::string const& text, char const* font, float size) {
        auto label = CCLabelBMFont::create(text.c_str(), font);
        // Scale by the font's line height rather than the text's bounds, so
        // every label of the same `size` gets the same scale.
        float lineHeight = label->getConfiguration()->m_nCommonHeight / CC_CONTENT_SCALE_FACTOR();
        if (lineHeight > 0) label->setScale(size / lineHeight);
        return label;
    }
}

CCLabelBMFont* makeText(std::string const& text, Weight weight, float size) {
    switch (weight) {
        case Weight::Regular: return makeLabel(text, "outfit-regular.fnt"_spr, size);
        case Weight::SemiBold: return makeLabel(text, "outfit-semibold.fnt"_spr, size);
        case Weight::Bold: return makeLabel(text, "outfit-bold.fnt"_spr, size);
    }
    return nullptr;
}

CCLabelBMFont* makeIcon(char const* glyph, float size) {
    return makeLabel(glyph, "icons.fnt"_spr, size);
}

} // namespace lazer
