#pragma once
// player avatars: fetches headshots from the roblox thumbnail cdn,
// decodes via stb_image, caches as d3d11 srvs. adapted from the
// reference PlayerAvatars implementation.

#include <cstdint>
#include <string>
#include <d3d11.h>

namespace playertools
{
	namespace avatars
	{
		// pump on the render thread: uploads decoded pngs to textures
		void tick();

		// returns the cached avatar srv for a user id, or nullptr.
		// queues a background fetch on first request.
		ID3D11ShaderResourceView* get(std::int64_t user_id);

		// username -> user id lookup (cached). 0 = unknown.
		std::int64_t lookup_user_id(const std::string& username);

		// drop all cached textures (call on eject)
		void clear();
	}
}
