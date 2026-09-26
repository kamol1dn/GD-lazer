#pragma once

#include <Geode/cocos/include/cocos2d.h>
#include <optional>

namespace lazer::tilt {

// How far the phone is tilted from the way it's being held, in the screen's
// axes: x > 0 when the right edge dips, y > 0 when the top edge dips. Roughly
// sin(angle), so 0.25 is about 15 degrees. The resting angle is learnt over a
// couple of seconds, so holding the phone at any angle settles back to 0.
//
// Reads Android's gravity sensor (the gyroscope fused with the accelerometer),
// or the plain accelerometer on phones without one. Returns nothing on other
// platforms, or if the phone has no sensor, so callers can fall back to the mouse.
//
// The sensor runs while at least one caller holds it on (acquire / release),
// so it isn't draining the battery outside the menus.
void acquire();
void release();
std::optional<cocos2d::CCPoint> get(float dt);

} // namespace lazer::tilt
