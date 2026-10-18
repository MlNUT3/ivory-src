//============ Copyright ImMagic, All rights reserved ============//
//
// Purpose: thin adapter between the overlay renderer and the vmenu
//          framework (fgui/fvar/felements). The framework owns the menu,
//          watermark, hotkey list and config UI rendering.
//
//================================================================//

#include "Menu.h"

#include <windows.h>

#include <imgui/imgui.h>
#include <imgui/backends/imgui_impl_win32.h>
#include <imgui/backends/imgui_impl_dx11.h>

#include <core/diagnostics/bootlog.h>
#include <core/utility/nvstream/nvstream.h>
#include <features/visuals/geometry/wallcheck.h>
#include <features/system/settings/settings.h>
#include <features/system/notifications/notifications.h>
#include <features/system/config/config.h>

#include <ui/menufw/headers/includes.h>

// ============================================================================
// Theme - kept for API compatibility; the framework styles itself
// ============================================================================
void Menu::ApplyTheme(int index)
{
	// the vmenu framework manages its own look (gui->set_style)
	(void)index;
}

// ============================================================================
// Init / shutdown / plumbing
// ============================================================================
bool Menu::Initialize(HWND hWnd, ID3D11Device* pDevice, ID3D11DeviceContext* pDeviceContext)
{
	IMGUI_CHECKVERSION();
	ImGui::CreateContext();

	ImGuiIO& io = ImGui::GetIO();
	io.IniFilename = nullptr;

	if (!ImGui_ImplWin32_Init(hWnd))
		return false;
	if (!ImGui_ImplDX11_Init(pDevice, pDeviceContext))
		return false;

	Menu::m_bInitialized = true;
	bootlog("menu: framework adapter initialized");
	return true;
}

void Menu::Render()
{
	DrawAll(true);
}

// glue the framework's globals to Ivory's var/elements


void Menu::DrawAll(bool menu_open)
{
    static bool s_last_menu_open = false;
    if (menu_open && !s_last_menu_open)
    {
        Menu::m_bShowMenu = true;
    }
    s_last_menu_open = menu_open;

    // map our state onto the framework
    var->gui.menu_open = Menu::m_bShowMenu && menu_open;
    if (Menu::m_bShowWatermark != var->gui.show_watermark)
        Menu::m_bShowWatermark = var->gui.show_watermark;

    // the framework renders the menu
    gui->render();

    wallcheck::render();
    notifications::render();
}