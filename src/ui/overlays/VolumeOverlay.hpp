#pragma once

namespace lazer::volume {

// osu!'s volume control (osu.Game/Overlays/VolumeOverlay.cs): the mouse wheel
// changes the volume wherever nothing else scrolls in the mod's own screens,
// and anywhere in the menus with Alt held. Three meters pop in at the left
// (effects, music, interface sounds) and fade a second after the last change;
// scrolling over one of them changes that one. PC only (phones have no wheel).

// A wheel event one of the mod's scroll views used: the volume control leaves
// it alone. Called by the mod's own CCMouseDelegates when they consume the wheel.
void markWheelHandled();

} // namespace lazer::volume
