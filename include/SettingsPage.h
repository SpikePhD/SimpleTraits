#pragma once

// "Simple Traits / Settings" page in SKSE Menu Framework's Mod Control Panel.
// Optional: without the framework, players edit SimpleTraits.user.json.
namespace ST::SettingsPage {
    // Call on kDataLoaded. Returns false when the framework is not installed.
    bool Register();
}
