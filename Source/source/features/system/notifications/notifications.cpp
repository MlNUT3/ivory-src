#include "notifications.h"
#include <external/imgui/addons/imgui_notification.h>

namespace notifications
{
    void add(const std::string& text, float duration)
    {
        ImNotify::Print(NotifyLevel::Info, "%s", text.c_str());
    }

    void add_welcome()
    {
        // Startup notifications removed
    }

    void add_hit(const std::string& player_name, float damage)
    {
        char buf[128];
        snprintf(buf, sizeof(buf), "Hit %s for %.0f dmg", player_name.c_str(), damage);
        ImNotify::Print(NotifyLevel::Info, "%s", buf);
    }

    void add_kill(const std::string& player_name, const std::string& weapon)
    {
        char buf[128];
        if (!weapon.empty())
            snprintf(buf, sizeof(buf), "Killed %s with %s", player_name.c_str(), weapon.c_str());
        else
            snprintf(buf, sizeof(buf), "Killed %s", player_name.c_str());
        ImNotify::Print(NotifyLevel::Success, "%s", buf);
    }

    void render()
    {
        // Old notification rendering removed - now handled by ImNotify
    }
}
