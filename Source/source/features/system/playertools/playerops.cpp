// player operations: teleport + spectate.

#include "playerops.h"

#include <sdk/cache/core/cache.h>
#include <sdk/game/game.h>
#include <core/memory/memory.h>
#include <sdk/offsets/offsets.h>

namespace playertools
{
	namespace ops
	{
		namespace
		{
			std::uint64_t s_spectate_target = 0;
			std::uint64_t s_saved_subject = 0;
		}

		bool teleport_to(std::uint64_t target_instance)
		{
			if (target_instance == 0)
				return false;

			cache::entity_t local = cache::get_local_player();
			if (local.instance.address == 0 || local.instance.address == target_instance)
				return false;
			if (!local.humanoid_root_part.is_valid())
				return false;

			// resolve the destination under the lock, but do the game-memory
			// write outside it (RPM/wpm under the global cache lock stalls
			// every other reader)
			math::vector3 dst{};
			bool found = false;
			{
				std::lock_guard<std::mutex> lock(cache::mtx);
				for (const auto& ent : cache::players)
				{
					if (ent.instance.address == target_instance)
					{
						// a stale cache entry can carry the (0,0,0) "unknown"
						// sentinel - teleporting to the map origin would be
						// a very visible failure
						if (ent.position.x == 0.0f && ent.position.y == 0.0f && ent.position.z == 0.0f)
							return false;
						dst = ent.position;
						found = true;
						break;
					}
				}
			}
			if (!found)
				return false;

			dst.y += 3.0f;
			local.humanoid_root_part.get_primitive().set_position(dst);
			return true;
		}

		void spectate(std::uint64_t target_instance)
		{
			if (game::camera < 0x10000)
				return;

			if (target_instance == 0)
			{
				stop_spectate();
				return;
			}

			std::uint64_t humanoid_addr = 0;
			{
				std::lock_guard<std::mutex> lock(cache::mtx);
				for (const auto& ent : cache::players)
				{
					if (ent.instance.address == target_instance && ent.humanoid.is_valid())
					{
						humanoid_addr = ent.humanoid.address;
						break;
					}
				}
			}
			if (!humanoid_addr)
				return;

			if (s_spectate_target == 0)
				s_saved_subject = memory->read<std::uint64_t>(game::camera + Offsets::Camera::CameraSubject);

			s_spectate_target = target_instance;
			memory->write<std::uint64_t>(game::camera + Offsets::Camera::CameraSubject, humanoid_addr);
		}

		bool is_spectating(std::uint64_t target_instance)
		{
			return s_spectate_target != 0 && s_spectate_target == target_instance;
		}

		void stop_spectate()
		{
			if (s_spectate_target == 0)
				return;
			s_spectate_target = 0;
			if (game::camera >= 0x10000 && s_saved_subject != 0)
				memory->write<std::uint64_t>(game::camera + Offsets::Camera::CameraSubject, s_saved_subject);
			s_saved_subject = 0;
		}

		std::string display_for(std::uint64_t target_instance)
		{
			std::lock_guard<std::mutex> lock(cache::mtx);
			for (const auto& ent : cache::players)
			{
				if (ent.instance.address == target_instance)
					return ent.display_name.empty() ? ent.name : ent.display_name;
			}
			return "unknown";
		}
	}
}
