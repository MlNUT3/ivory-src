#pragma once
#include <sdk/cache/core/cache.h>
#include <unordered_map>
#include <mutex>
#include <sdk/math/math.h>

namespace aimbot
{
	inline cache::entity_t player{};
	inline cache::entity_t sticky_target{};
	inline cache::entity_t last_target{};
	inline std::unordered_map<std::uint64_t, math::vector3> previous_positions{};

	inline math::vector3 aim_point_3d{};
	inline bool has_aim_point = false;

	inline std::mutex target_mtx{};

	inline cache::entity_t get_player()
	{
		std::lock_guard<std::mutex> lock(target_mtx);
		return player;
	}

	inline std::uint64_t get_player_address()
	{
		std::lock_guard<std::mutex> lock(target_mtx);
		return player.instance.address;
	}

	inline void set_player(const cache::entity_t& e)
	{
		std::lock_guard<std::mutex> lock(target_mtx);
		player = e;
	}

	inline void clear_player()
	{
		std::lock_guard<std::mutex> lock(target_mtx);
		player = cache::entity_t{};
	}

	inline cache::entity_t get_sticky_target()
	{
		std::lock_guard<std::mutex> lock(target_mtx);
		return sticky_target;
	}

	inline void set_sticky_target(const cache::entity_t& e)
	{
		std::lock_guard<std::mutex> lock(target_mtx);
		sticky_target = e;
	}

	inline void clear_sticky_target()
	{
		std::lock_guard<std::mutex> lock(target_mtx);
		sticky_target = cache::entity_t{};
	}

	inline bool get_aim_point(math::vector3& out)
	{
		std::lock_guard<std::mutex> lock(target_mtx);
		if (!has_aim_point) return false;
		out = aim_point_3d;
		return true;
	}

	inline void set_aim_point(const math::vector3& p)
	{
		std::lock_guard<std::mutex> lock(target_mtx);
		aim_point_3d = p;
		has_aim_point = true;
	}

	inline void clear_aim_point()
	{
		std::lock_guard<std::mutex> lock(target_mtx);
		has_aim_point = false;
	}

	void run();
}