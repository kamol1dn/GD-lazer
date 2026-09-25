#include "Text.hpp"

#include <Geode/Geode.hpp>

#include <algorithm>
#include <vector>

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

// Greedy word wrap using the label's own measurements.
CCNode* makeWrappedText(std::string const& text, float size, float maxWidth, ccColor3B color) {
    auto holder = CCNode::create();
    std::vector<std::string> lines;
    std::string line, word;
    auto measure = [&](std::string const& s) {
        auto l = makeText(s, Weight::Regular, size);
        return l->getScaledContentSize().width;
    };
    auto flushWord = [&] {
        if (word.empty()) return;
        std::string candidate = line.empty() ? word : line + " " + word;
        if (!line.empty() && measure(candidate) > maxWidth) {
            lines.push_back(line);
            line = word;
        } else {
            line = candidate;
        }
        word.clear();
    };
    for (char c : text) {
        if (c == ' ' || c == '\n') {
            flushWord();
            if (c == '\n') { lines.push_back(line); line.clear(); }
        } else {
            word += c;
        }
    }
    flushWord();
    if (!line.empty()) lines.push_back(line);

    float lineHeight = size * 1.15f;
    float width = 0;
    for (size_t i = 0; i < lines.size(); i++) {
        auto l = makeText(lines[i], Weight::Regular, size);
        l->setColor(color);
        l->setAnchorPoint({0, 1});
        l->setPosition({0, -lineHeight * i});
        holder->addChild(l);
        width = std::max(width, l->getScaledContentSize().width);
    }
    holder->setContentSize({width, lineHeight * lines.size()});
    return holder;
}

} // namespace lazer
