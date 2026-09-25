#include "Account.hpp"

#include <Geode/Geode.hpp>

using namespace geode::prelude;

namespace lazer::account {

namespace {
    // Never shown, kept for the whole session and never freed, so a request
    // finishing late can't call back into a destroyed page.
    AccountLayer* page() {
        static auto layer = [] { auto l = AccountLayer::create(); l->retain(); return l; }();
        return layer;
    }

    AccountHelpLayer* helpPage() {
        static auto layer = [] { auto l = AccountHelpLayer::create(); l->retain(); return l; }();
        return layer;
    }
}

bool loggedIn() {
    return GJAccountManager::get()->m_accountID > 0;
}

bool busy() {
    // GD greys out its save / load buttons while a request is running.
    auto button = page()->m_backupButton;
    return loggedIn() && button && !button->isEnabled();
}

std::string username() {
    std::string name = GJAccountManager::get()->m_username;
    if (name.empty()) name = GameManager::get()->m_playerName;
    return name.empty() ? "guest" : name;
}

void save() { page()->onBackup(nullptr); }
void load() { page()->onSync(nullptr); }
void refreshLogin() { helpPage()->onReLogin(nullptr); }
void manage() { helpPage()->onAccountManagement(nullptr); }
void unlink() { helpPage()->onUnlink(nullptr); }
void logIn() { page()->onLogin(nullptr); }
void registerAccount() { page()->onRegister(nullptr); }

} // namespace lazer::account
