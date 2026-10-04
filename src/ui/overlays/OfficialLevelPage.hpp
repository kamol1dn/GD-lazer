#pragma once
#include "../../levels/LevelLibrary.hpp"
#include <functional>
namespace lazer {
void showOfficialLevelPage(levels::Entry const& entry, std::function<void()> play);
}
