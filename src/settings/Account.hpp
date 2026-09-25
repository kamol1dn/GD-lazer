#pragma once

#include <string>

namespace lazer::account {

// GD's account actions, run through GD's own (hidden) AccountLayer and
// AccountHelpLayer: GD does the server requests and shows its confirmation,
// login and result popups.
bool loggedIn();
// A save or load is in progress.
bool busy();
std::string username();

void save();
void load();
void refreshLogin();
void manage();
void unlink();
void logIn();
void registerAccount();

} // namespace lazer::account
