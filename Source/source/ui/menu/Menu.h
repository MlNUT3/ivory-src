//============ Copyright ImMagic, All rights reserved ============//
//
// Purpose: Card-based macOS-style menu (sidebar + module cards)
//
//================================================================//

#pragma once

#include <D3D11.h>
class Menu
{
public:
	static bool Initialize(HWND hWnd, ID3D11Device* pDevice, ID3D11DeviceContext* pDeviceContext);
	static void Shutdown();
	static void ApplyTheme(int index);
	inline static struct ImFont* m_pIconFont = nullptr;
	inline static bool m_bInitialized = false;

	inline static bool m_bShowMenu = true;
	inline static bool m_bShowWatermark = true;

public:
	static void Render();
	static void DrawAll(bool menu_open);
	static bool HandleMessage(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam);

public:
	// Internal helper, when using hooked directx
	static void InvalidateDeviceObjects();
	static void CreateDeviceObjects();

public:
	static void	DrawWatermark();
	static void	DrawMenu();
};
