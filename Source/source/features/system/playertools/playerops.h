#pragma once
// player operations: teleport + spectate driven from the players tab.

#include <cstdint>
#include <string>

namespace playertools
{
	namespace ops
	{
		// teleport the local player to the target's position (+3 studs up)
		bool teleport_to(std::uint64_t target_instance);

		// spectate a player by instance address (0 = stop, back to local)
		void spectate(std::uint64_t target_instance);
		bool is_spectating(std::uint64_t target_instance);
		void stop_spectate();

		// display name for the tab list
		std::string display_for(std::uint64_t target_instance);
	}
}
