#pragma once
// Rivals-first teamcheck (Popstar/Foulz reference): Rivals marks teammates
// with a "TeammateLabel" under HumanoidRootPart, and its Team fields are
// FFA-style noise. When a Rivals place is detected, ONLY the TeammateLabel
// check runs - the generic team/team_color comparisons never do.
#include <cstdint>
#include <chrono>
#include <sdk/cache/core/cache.h>
#include <sdk/game/game.h>
#include <sdk/sdk.h>

namespace rivals
{
	// Rivals place ids - cached (1 RPM max per thread), call once per tick
	// and share the result across every entity check
	inline bool is_rivals_place()
	{
		static thread_local std::uint64_t s_pid = 0;
		static thread_local std::chrono::steady_clock::time_point s_at{};
		const auto now = std::chrono::steady_clock::now();
		if (s_pid == 0 || std::chrono::duration<float>(now - s_at).count() > 2.0f)
		{
			s_pid = 0;
			if (game::datamodel && game::datamodel->address != 0)
				s_pid = game::datamodel->get_place_id();
			s_at = now;
		}
		return s_pid == 17625359962ULL || s_pid == 133215910299950ULL || s_pid == 117398147513099ULL;
	}

	// TeammateLabel scan. No place check inside - callers gate on
	// is_rivals_place() once, then use only this.
	inline bool is_teammate(const cache::entity_t& entity)
	{
		if (entity.humanoid_root_part.address == 0)
			return false;
		try
		{
			rbx::c_instance hrp(entity.humanoid_root_part.address);
			return hrp.find_first_child("TeammateLabel") != 0;
		}
		catch (...)
		{
			return false;
		}
	}
}
