#pragma once

#include <Geode/cocos/include/cocos2d.h>

namespace lazer {

// osu!'s ParallaxContainer: an offset towards the mouse (on phones, with the
// tilt) and a matching zoom so the moved content still covers its edges.
// The amount (ParallaxAmount) comes from the float setting `setting`, in percent,
// read every frame so the settings sliders apply live.
class Parallax {
public:
    explicit Parallax(char const* setting) : m_setting(setting) {}

    // `size` is the area the content covers (usually the screen).
    void update(float dt, cocos2d::CCSize const& size);

    cocos2d::CCPoint offset() const { return m_offset; }
    float scale() const { return m_scale; }

    // Where the pointer is, as -1..1 of the way to each edge after osu!'s soft
    // falloff, or the phone's tilt on phones (nothing with "Tilt parallax" off).
    // Shared by every Parallax in a frame.
    static cocos2d::CCPoint input(float dt, cocos2d::CCSize const& size);

private:
    char const* m_setting;
    cocos2d::CCPoint m_offset {0, 0};
    float m_scale = 1.f;
};

} // namespace lazer
