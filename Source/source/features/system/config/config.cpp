#include "config.h"
#include <features/system/settings/settings.h>
#include <ui/menu/settings/functions.h>
#include <ui/menu/settings/theme.h>
#include <mutex>
#include <random>
#include <cstring>
#include <unordered_map>
#include <chrono>
#include <sdk/cache/core/cache.h>
#include <sdk/game/game.h>
#include <windows.h>
#include <wininet.h>
#pragma comment(lib, "wininet.lib")
#include <shlobj.h>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <filesystem>
#include <imgui/imgui.h>

namespace config
{
	std::string get_base_directory()
	{
		return "C:\\Ivory";
	}

	std::string get_config_directory()
	{
		return "C:\\Ivory\\Configs";
	}

	std::string get_dumps_directory()
	{
		return "C:\\Ivory\\Dumps";
	}

	bool ensure_dumps_directory()
	{
		std::string dir = get_dumps_directory();
		try
		{
			std::filesystem::create_directories(dir);
			return true;
		}
		catch (...)
		{
			return false;
		}
	}

	std::string get_resources_directory()
	{
		return "C:\\Ivory\\Resources";
	}

	bool ensure_resources_directory()
	{
		std::string dir = get_resources_directory();
		try
		{
			std::filesystem::create_directories(dir);
			return true;
		}
		catch (...)
		{
			return false;
		}
	}

	std::string get_config_path(const std::string& name)
	{
		std::string dir = get_config_directory();
		if (dir.empty())
			return "";

		std::string filename = name;
		if (filename.find(".json") == std::string::npos)
			filename += ".json";

		return dir + "\\" + filename;
	}

	bool ensure_config_directory()
	{
		std::string dir = get_config_directory();
		if (dir.empty())
			return false;

		try
		{
			std::filesystem::create_directories(dir);
			return true;
		}
		catch (...)
		{
			return false;
		}
	}

	std::vector<config_info_t> get_config_list()
	{
		// Called every frame while the configs tab is open â€” cache the directory
		// scan for a short window so we're not doing filesystem I/O per frame.
		static std::mutex cache_mu;
		static std::vector<config_info_t> cached;
		static std::chrono::steady_clock::time_point last_scan{};
		static bool has_scanned = false;
		{
			std::lock_guard<std::mutex> lk(cache_mu);
			auto now = std::chrono::steady_clock::now();
			if (has_scanned && (now - last_scan) < std::chrono::milliseconds(1000))
				return cached;
			last_scan = now;
		}

		std::vector<config_info_t> configs;
		std::string dir = get_config_directory();

		if (!dir.empty() && std::filesystem::exists(dir))
		{
			try
			{
				for (const auto& entry : std::filesystem::directory_iterator(dir))
				{
					if (entry.is_regular_file() && entry.path().extension() == ".json")
					{
						std::string stem = entry.path().stem().string();
						if (stem == "theme" || stem == "autoload_theme" || stem == "autoload" || stem == "window_state" || stem.find("theme") != std::string::npos)
						{
							continue;
						}
						config_info_t info;
						info.name = stem;
						info.path = entry.path().string();
						configs.push_back(info);
					}
				}
			}
			catch (...)
			{
			}
		}

		std::lock_guard<std::mutex> lk(cache_mu);
		cached = configs;
		has_scanned = true;
		return cached;
	}

	static void write_color(std::ostringstream& ss, const char* name, const float col[4])
	{
		ss << "    \"" << name << "\": [" << col[0] << ", " << col[1] << ", " << col[2] << ", " << col[3] << "],\n";
	}

	// Atomic file write: write to a .tmp sibling, verify the stream, then
	// rename over the target. A crash/disk-full mid-save previously truncated
	// the live config in place with the old version already gone.
	static bool write_file_atomic(const std::string& path, const std::string& content)
	{
		const std::string tmp = path + ".tmp";
		{
			std::ofstream file(tmp, std::ios::binary | std::ios::trunc);
			if (!file.is_open())
				return false;
			file.write(content.data(), (std::streamsize)content.size());
			file.close();
			if (!file.good())
			{
				try { std::filesystem::remove(tmp); } catch (...) {}
				return false;
			}
		}
		std::error_code ec;
		std::filesystem::rename(tmp, path, ec);
		if (ec)
		{
			// rename can fail across filesystems - fall back to a plain replace
			try
			{
				std::filesystem::copy_file(tmp, path, std::filesystem::copy_options::overwrite_existing, ec);
				if (!ec)
				{
					try { std::filesystem::remove(tmp); } catch (...) {}
					return true;
				}
			}
			catch (...) {}
			try { std::filesystem::remove(tmp); } catch (...) {}
			return false;
		}
		return true;
	}

	static std::string extract_section(const std::string& json, const char* section_name)
	{
		std::string search = "\"" + std::string(section_name) + "\": {";
		size_t start = json.find(search);
		if (start == std::string::npos)
			return "";

		start = json.find("{", start);
		if (start == std::string::npos)
			return "";

		int depth = 0;
		size_t pos = start;
		for (; pos < json.length(); pos++)
		{
			if (json[pos] == '{')
				depth++;
			else if (json[pos] == '}')
			{
				depth--;
				if (depth == 0)
					break;
			}
		}

		if (pos < json.length())
			return json.substr(start, pos - start + 1);
		return "";
	}

	static void read_color(const std::string& json, const char* name, float col[4])
	{
		std::string search = "\"" + std::string(name) + "\": [";
		size_t pos = json.find(search);
		if (pos != std::string::npos)
		{
			pos += search.length();
			size_t end = json.find("]", pos);
			if (end != std::string::npos)
			{
				std::string values = json.substr(pos, end - pos);
				sscanf_s(values.c_str(), "%f, %f, %f, %f", &col[0], &col[1], &col[2], &col[3]);
			}
		}
	}

	static void write_float(std::ostringstream& ss, const char* name, float value)
	{
		ss << "    \"" << name << "\": " << value << ",\n";
	}

	static float read_float(const std::string& json, const char* name, float default_value)
	{
		std::string search = "\"" + std::string(name) + "\": ";
		size_t pos = json.find(search);
		if (pos != std::string::npos)
		{
			pos += search.length();
			size_t end = json.find_first_of(",\n}", pos);
			if (end != std::string::npos)
			{
				std::string value = json.substr(pos, end - pos);
				return static_cast<float>(atof(value.c_str()));
			}
		}
		return default_value;
	}

	static void write_int(std::ostringstream& ss, const char* name, int value)
	{
		ss << "    \"" << name << "\": " << value << ",\n";
	}

	static int read_int(const std::string& json, const char* name, int default_value)
	{
		std::string search = "\"" + std::string(name) + "\": ";
		size_t pos = json.find(search);
		if (pos != std::string::npos)
		{
			pos += search.length();
			size_t end = json.find_first_of(",\n}", pos);
			if (end != std::string::npos)
			{
				std::string value = json.substr(pos, end - pos);
				return atoi(value.c_str());
			}
		}
		return default_value;
	}

	static void write_bool(std::ostringstream& ss, const char* name, bool value)
	{
		ss << "    \"" << name << "\": " << (value ? "true" : "false") << ",\n";
	}

	static bool read_bool(const std::string& json, const char* name, bool default_value)
	{
		std::string search = "\"" + std::string(name) + "\": ";
		size_t pos = json.find(search);
		if (pos != std::string::npos)
		{
			pos += search.length();
			std::string value = json.substr(pos, 5);
			if (value.find("true") == 0)
				return true;
			if (value.find("false") == 0)
				return false;
		}
		return default_value;
	}
	static void write_string(std::ostringstream& ss, const char* name, const char* value)
	{
		ss << "    \"" << name << "\": \"" << value << "\",\n";
	}

	static std::string read_string(const std::string& json, const char* name, const std::string& default_value)
	{
		std::string search = "\"" + std::string(name) + "\": \"";
		size_t pos = json.find(search);
		if (pos != std::string::npos)
		{
			pos += search.length();
			size_t end = json.find("\"", pos);
			if (end != std::string::npos)
			{
				return json.substr(pos, end - pos);
			}
		}
		return default_value;
	}

	static void write_string_vector(std::ostringstream& ss, const char* name, const std::vector<std::string>& vec)
	{
		ss << "    \"" << name << "\": [";
		for (size_t i = 0; i < vec.size(); i++)
		{
			ss << "\"" << vec[i] << "\"";
			if (i + 1 < vec.size())
				ss << ", ";
		}
		ss << "],\n";
	}

	static std::vector<std::string> read_string_vector(const std::string& json, const char* name)
	{
		std::vector<std::string> vec;
		std::string search = "\"" + std::string(name) + "\": [";
		size_t pos = json.find(search);
		if (pos != std::string::npos)
		{
			pos += search.length();
			size_t end = json.find("]", pos);
			if (end != std::string::npos)
			{
				std::string elements_str = json.substr(pos, end - pos);
				size_t el_start = 0;
				while (true)
				{
					size_t q_start = elements_str.find("\"", el_start);
					if (q_start == std::string::npos)
						break;
					size_t q_end = elements_str.find("\"", q_start + 1);
					if (q_end == std::string::npos)
						break;
					vec.push_back(elements_str.substr(q_start + 1, q_end - q_start - 1));
					el_start = q_end + 1;
				}
			}
		}
		return vec;
	}

	static void write_priority_settings(std::ostringstream& ss, const char* priority_name, const settings::visuals::priority_settings_t& settings)
	{
		ss << "    \"" << priority_name << "\": {\n";
		write_bool(ss, "box", settings.box);
		write_int(ss, "box_type", settings.box_type);
		write_int(ss, "box_style", settings.box_style);
		write_float(ss, "corner_box_length", settings.corner_box_length);
		write_bool(ss, "box_inline", settings.box_inline);
		write_bool(ss, "box_gradient", settings.box_gradient);
		write_int(ss, "box_gradient_type", settings.box_gradient_type);
		write_float(ss, "box_pulse_speed", settings.box_pulse_speed);
		write_color(ss, "colour", settings.colour);
		write_color(ss, "box_colour_mid", settings.box_colour_mid);
		write_color(ss, "box_colour_low", settings.box_colour_low);
		write_bool(ss, "box_fill", settings.box_fill);
		write_int(ss, "box_fill_style", settings.box_fill_style);
		write_bool(ss, "box_fill_gradient", settings.box_fill_gradient);
		write_bool(ss, "box_fill_gradient_spin", settings.box_fill_gradient_spin);
		write_float(ss, "box_fill_gradient_speed", settings.box_fill_gradient_speed);
		write_int(ss, "box_fill_gradient_type", settings.box_fill_gradient_type);
		write_int(ss, "box_fill_gradient_mask", settings.box_fill_gradient_mask);
		write_float(ss, "box_fill_pulse_speed", settings.box_fill_pulse_speed);
		write_color(ss, "box_fill_colour", settings.box_fill_colour);
		write_color(ss, "box_fill_colour_mid", settings.box_fill_colour_mid);
		write_color(ss, "box_fill_colour_low", settings.box_fill_colour_low);
		write_bool(ss, "username", settings.username);
		write_int(ss, "username_type", settings.username_type);
		write_int(ss, "name_transform", settings.name_transform);
		write_int(ss, "name_bracket_style", settings.name_bracket_style);
		write_color(ss, "username_colour", settings.username_colour);
		write_bool(ss, "distance", settings.distance);
		write_color(ss, "distance_colour", settings.distance_colour);
		write_bool(ss, "tool", settings.tool);
		write_color(ss, "tool_colour", settings.tool_colour);
		write_bool(ss, "healthbar", settings.healthbar);
		write_int(ss, "healthbar_style", settings.healthbar_style);
		write_float(ss, "healthbar_padding", settings.healthbar_padding);
		write_int(ss, "healthbar_gradient_type", settings.healthbar_gradient_type);
		write_float(ss, "healthbar_pulse_speed", settings.healthbar_pulse_speed);
		write_bool(ss, "healthbar_text", settings.healthbar_text);
		write_color(ss, "healthbar_text_colour", settings.healthbar_text_colour);
		write_bool(ss, "healthbar_lerp", settings.healthbar_lerp);
		write_bool(ss, "value_follow", settings.value_follow);
		write_bool(ss, "ignore_full", settings.ignore_full);
		write_bool(ss, "bar_fold", settings.bar_fold);
		write_float(ss, "bar_size", settings.bar_size);
		write_color(ss, "healthbar_colour", settings.healthbar_colour);
		write_color(ss, "healthbar_colour_mid", settings.healthbar_colour_mid);
		write_color(ss, "healthbar_colour_low", settings.healthbar_colour_low);
		write_bool(ss, "tracers", settings.tracers);
		write_int(ss, "tracers_origin", settings.tracers_origin);
		write_float(ss, "tracers_thickness", settings.tracers_thickness);
		write_color(ss, "tracers_colour", settings.tracers_colour);
		write_bool(ss, "chams", settings.chams);
		write_int(ss, "chams_type", settings.chams_type);
		write_int(ss, "chams_texture", settings.chams_texture);
		write_int(ss, "mesh_shader", settings.mesh_shader);
		write_bool(ss, "mesh_outline", settings.mesh_outline);
		write_int(ss, "mesh_outline_mode", settings.mesh_outline_mode);
		write_int(ss, "mesh_outline_style", settings.mesh_outline_style);
		write_float(ss, "mesh_outline_thickness", settings.mesh_outline_thickness);
		write_bool(ss, "mesh_outline_only", settings.mesh_outline_only);
		write_bool(ss, "mesh_accessories", settings.mesh_accessories);
		write_float(ss, "mesh_depth_bias", settings.mesh_depth_bias);
		write_bool(ss, "chams_corpse", settings.chams_corpse);
		write_bool(ss, "chams_health_based", settings.chams_health_based);
		write_bool(ss, "chams_flash", settings.chams_flash);
		write_float(ss, "chams_flash_speed", settings.chams_flash_speed);
		write_color(ss, "chams_flash_colour", settings.chams_flash_colour);
		write_bool(ss, "chams_fade", settings.chams_fade);
		write_float(ss, "chams_fade_speed", settings.chams_fade_speed);
		write_color(ss, "chams_colour", settings.chams_colour);
		write_color(ss, "chams_outline_colour", settings.chams_outline_colour);
		write_bool(ss, "chams_outline_glow", settings.chams_outline_glow);
		write_color(ss, "chams_glow_colour", settings.chams_glow_colour);
		write_float(ss, "chams_glow_radius", settings.chams_glow_radius);
		write_float(ss, "chams_glow_alpha", settings.chams_glow_alpha);
		write_float(ss, "chams_glow_intensity", settings.chams_glow_intensity);
		write_bool(ss, "skeleton", settings.skeleton);
		write_color(ss, "skeleton_colour", settings.skeleton_colour);
		write_color(ss, "skeleton_outline_colour", settings.skeleton_outline_colour);
		write_bool(ss, "head_dot", settings.head_dot);
		write_int(ss, "head_type", settings.head_type);
		write_int(ss, "head_mode", settings.head_mode);
		write_bool(ss, "circular_head_dot", settings.circular_head_dot);
		write_color(ss, "head_dot_colour", settings.head_dot_colour);
		write_color(ss, "head_dot_outline_colour", settings.head_dot_outline_colour);
		write_float(ss, "head_dot_size", settings.head_dot_size);
		write_bool(ss, "corpse", settings.corpse);
		write_color(ss, "corpse_colour", settings.corpse_colour);
		write_color(ss, "corpse_outline_colour", settings.corpse_outline_colour);
		write_bool(ss, "corpse_names", settings.corpse_names);
		write_color(ss, "corpse_names_colour", settings.corpse_names_colour);
		write_bool(ss, "offscreen_arrows", settings.offscreen_arrows);
		write_int(ss, "arrow_type", settings.arrow_type);
		write_int(ss, "arrow_mode", settings.arrow_mode);
		write_float(ss, "offscreen_arrows_size", settings.offscreen_arrows_size);
		write_float(ss, "offscreen_arrows_radius", settings.offscreen_arrows_radius);
		write_float(ss, "offscreen_arrows_position", settings.offscreen_arrows_position);
		write_float(ss, "arrows_max_dist", settings.arrows_max_dist);
		write_bool(ss, "arrows_glow", settings.arrows_glow);
		write_color(ss, "offscreen_arrows_colour", settings.offscreen_arrows_colour);
		write_color(ss, "arrows_glow_colour", settings.arrows_glow_colour);
		write_bool(ss, "movement_trails", settings.movement_trails);
		write_float(ss, "movement_trails_thickness", settings.movement_trails_thickness);
		write_float(ss, "movement_trails_length", settings.movement_trails_length);
		write_color(ss, "movement_trails_colour", settings.movement_trails_colour);
		write_bool(ss, "view_angle_lines", settings.view_angle_lines);
		write_float(ss, "view_angle_lines_thickness", settings.view_angle_lines_thickness);
		write_float(ss, "view_angle_lines_length", settings.view_angle_lines_length);
		write_color(ss, "view_angle_lines_colour", settings.view_angle_lines_colour);
		write_bool(ss, "sound_esp", settings.sound_esp);
		write_color(ss, "sound_esp_colour", settings.sound_esp_colour);
		write_float(ss, "sound_esp_radius", settings.sound_esp_radius);
		write_float(ss, "sound_esp_speed", settings.sound_esp_speed);
		write_bool(ss, "footprints", settings.footprints);
		write_bool(ss, "footprints_glow", settings.footprints_glow);
		write_color(ss, "footprints_colour", settings.footprints_colour);
		write_float(ss, "footprints_radius", settings.footprints_radius);
		write_float(ss, "footprints_speed", settings.footprints_speed);
		write_bool(ss, "state_flags", settings.state_flags);
		write_bool(ss, "rig_flags", settings.rig_flags);
		ss << "    },\n";
	}

	static void read_priority_settings(const std::string& visuals_json, const char* priority_name, settings::visuals::priority_settings_t& settings)
	{
		std::string search = "\"" + std::string(priority_name) + "\": {";
		size_t start = visuals_json.find(search);
		if (start == std::string::npos)
			return;

		start = visuals_json.find("{", start);
		if (start == std::string::npos)
			return;

		int depth = 0;
		size_t pos = start;
		for (; pos < visuals_json.length(); pos++)
		{
			if (visuals_json[pos] == '{')
				depth++;
			else if (visuals_json[pos] == '}')
			{
				depth--;
				if (depth == 0)
					break;
			}
		}

		if (pos >= visuals_json.length())
			return;

		std::string section = visuals_json.substr(start, pos - start + 1);

		settings.box = read_bool(section, "box", settings.box);
		settings.box_type = read_int(section, "box_type", settings.box_type);
		settings.box_style = read_int(section, "box_style", settings.box_style);
		settings.corner_box_length = read_float(section, "corner_box_length", settings.corner_box_length);
		settings.box_inline = read_bool(section, "box_inline", settings.box_inline);
		settings.box_gradient = read_bool(section, "box_gradient", settings.box_gradient);
		settings.box_gradient_type = read_int(section, "box_gradient_type", settings.box_gradient_type);
		settings.box_pulse_speed = read_float(section, "box_pulse_speed", settings.box_pulse_speed);
		read_color(section, "colour", settings.colour);
		read_color(section, "box_colour_mid", settings.box_colour_mid);
		read_color(section, "box_colour_low", settings.box_colour_low);
		settings.box_fill = read_bool(section, "box_fill", settings.box_fill);
		settings.box_fill_style = read_int(section, "box_fill_style", settings.box_fill_style);
		settings.box_fill_gradient = read_bool(section, "box_fill_gradient", settings.box_fill_gradient);
		settings.box_fill_gradient_spin = read_bool(section, "box_fill_gradient_spin", settings.box_fill_gradient_spin);
		settings.box_fill_gradient_speed = read_float(section, "box_fill_gradient_speed", settings.box_fill_gradient_speed);
		settings.box_fill_gradient_type = read_int(section, "box_fill_gradient_type", settings.box_fill_gradient_type);
		settings.box_fill_gradient_mask = read_int(section, "box_fill_gradient_mask", settings.box_fill_gradient_mask);
		settings.box_fill_pulse_speed = read_float(section, "box_fill_pulse_speed", settings.box_fill_pulse_speed);
		read_color(section, "box_fill_colour", settings.box_fill_colour);
		read_color(section, "box_fill_colour_mid", settings.box_fill_colour_mid);
		read_color(section, "box_fill_colour_low", settings.box_fill_colour_low);
		settings.username = read_bool(section, "username", settings.username);
		settings.username_type = read_int(section, "username_type", settings.username_type);
		settings.name_transform = read_int(section, "name_transform", settings.name_transform);
		settings.name_bracket_style = read_int(section, "name_bracket_style", settings.name_bracket_style);
		read_color(section, "username_colour", settings.username_colour);
		settings.distance = read_bool(section, "distance", settings.distance);
		read_color(section, "distance_colour", settings.distance_colour);
		settings.tool = read_bool(section, "tool", settings.tool);
		read_color(section, "tool_colour", settings.tool_colour);
		settings.healthbar = read_bool(section, "healthbar", settings.healthbar);
		settings.healthbar_style = read_int(section, "healthbar_style", settings.healthbar_style);
		settings.healthbar_padding = read_float(section, "healthbar_padding", settings.healthbar_padding);
		settings.healthbar_gradient_type = read_int(section, "healthbar_gradient_type", settings.healthbar_gradient_type);
		settings.healthbar_pulse_speed = read_float(section, "healthbar_pulse_speed", settings.healthbar_pulse_speed);
		settings.healthbar_text = read_bool(section, "healthbar_text", settings.healthbar_text);
		read_color(section, "healthbar_text_colour", settings.healthbar_text_colour);
		settings.healthbar_lerp = read_bool(section, "healthbar_lerp", settings.healthbar_lerp);
		settings.value_follow = read_bool(section, "value_follow", settings.value_follow);
		settings.ignore_full = read_bool(section, "ignore_full", settings.ignore_full);
		settings.bar_fold = read_bool(section, "bar_fold", settings.bar_fold);
		settings.bar_size = read_float(section, "bar_size", settings.bar_size);
		read_color(section, "healthbar_colour", settings.healthbar_colour);
		read_color(section, "healthbar_colour_mid", settings.healthbar_colour_mid);
		read_color(section, "healthbar_colour_low", settings.healthbar_colour_low);
		settings.tracers = read_bool(section, "tracers", settings.tracers);
		settings.tracers_origin = read_int(section, "tracers_origin", settings.tracers_origin);
		settings.tracers_thickness = read_float(section, "tracers_thickness", settings.tracers_thickness);
		read_color(section, "tracers_colour", settings.tracers_colour);
		settings.chams = read_bool(section, "chams", settings.chams);
		settings.chams_type = read_int(section, "chams_type", settings.chams_type);
		settings.chams_texture = read_int(section, "chams_texture", settings.chams_texture);
		settings.mesh_shader = read_int(section, "mesh_shader", settings.mesh_shader);
		settings.mesh_outline = read_bool(section, "mesh_outline", settings.mesh_outline);
		settings.mesh_outline_mode = read_int(section, "mesh_outline_mode", settings.mesh_outline_mode);
		settings.mesh_outline_style = read_int(section, "mesh_outline_style", settings.mesh_outline_style);
		settings.mesh_outline_thickness = read_float(section, "mesh_outline_thickness", settings.mesh_outline_thickness);
		settings.mesh_outline_only = read_bool(section, "mesh_outline_only", settings.mesh_outline_only);
		settings.mesh_accessories = read_bool(section, "mesh_accessories", settings.mesh_accessories);
		settings.mesh_depth_bias = read_float(section, "mesh_depth_bias", settings.mesh_depth_bias);
		settings.chams_corpse = read_bool(section, "chams_corpse", settings.chams_corpse);
		settings.chams_health_based = read_bool(section, "chams_health_based", settings.chams_health_based);
		settings.chams_flash = read_bool(section, "chams_flash", settings.chams_flash);
		settings.chams_flash_speed = read_float(section, "chams_flash_speed", settings.chams_flash_speed);
		read_color(section, "chams_flash_colour", settings.chams_flash_colour);
		settings.chams_fade = read_bool(section, "chams_fade", settings.chams_fade);
		settings.chams_fade_speed = read_float(section, "chams_fade_speed", settings.chams_fade_speed);
		read_color(section, "chams_colour", settings.chams_colour);
		read_color(section, "chams_outline_colour", settings.chams_outline_colour);
		settings.chams_outline_glow = read_bool(section, "chams_outline_glow", settings.chams_outline_glow);
		read_color(section, "chams_glow_colour", settings.chams_glow_colour);
		settings.chams_glow_radius = read_float(section, "chams_glow_radius", settings.chams_glow_radius);
		settings.chams_glow_alpha = read_float(section, "chams_glow_alpha", settings.chams_glow_alpha);
		settings.chams_glow_intensity = read_float(section, "chams_glow_intensity", settings.chams_glow_intensity);
		settings.skeleton = read_bool(section, "skeleton", settings.skeleton);
		read_color(section, "skeleton_colour", settings.skeleton_colour);
		read_color(section, "skeleton_outline_colour", settings.skeleton_outline_colour);
		settings.head_dot = read_bool(section, "head_dot", settings.head_dot);
		settings.head_type = read_int(section, "head_type", settings.head_type);
		settings.head_mode = read_int(section, "head_mode", settings.head_mode);
		settings.circular_head_dot = read_bool(section, "circular_head_dot", settings.circular_head_dot);
		read_color(section, "head_dot_colour", settings.head_dot_colour);
		read_color(section, "head_dot_outline_colour", settings.head_dot_outline_colour);
		settings.head_dot_size = read_float(section, "head_dot_size", settings.head_dot_size);
		settings.corpse = read_bool(section, "corpse", settings.corpse);
		read_color(section, "corpse_colour", settings.corpse_colour);
		read_color(section, "corpse_outline_colour", settings.corpse_outline_colour);
		settings.corpse_names = read_bool(section, "corpse_names", settings.corpse_names);
		read_color(section, "corpse_names_colour", settings.corpse_names_colour);
		settings.offscreen_arrows = read_bool(section, "offscreen_arrows", settings.offscreen_arrows);
		settings.arrow_type = read_int(section, "arrow_type", settings.arrow_type);
		settings.arrow_mode = read_int(section, "arrow_mode", settings.arrow_mode);
		settings.offscreen_arrows_size = read_float(section, "offscreen_arrows_size", settings.offscreen_arrows_size);
		settings.offscreen_arrows_radius = read_float(section, "offscreen_arrows_radius", settings.offscreen_arrows_radius);
		settings.offscreen_arrows_position = read_float(section, "offscreen_arrows_position", settings.offscreen_arrows_position);
		settings.arrows_max_dist = read_float(section, "arrows_max_dist", settings.arrows_max_dist);
		settings.arrows_glow = read_bool(section, "arrows_glow", settings.arrows_glow);
		read_color(section, "offscreen_arrows_colour", settings.offscreen_arrows_colour);
		read_color(section, "arrows_glow_colour", settings.arrows_glow_colour);
		settings.movement_trails = read_bool(section, "movement_trails", settings.movement_trails);
		settings.movement_trails_thickness = read_float(section, "movement_trails_thickness", settings.movement_trails_thickness);
		settings.movement_trails_length = read_float(section, "movement_trails_length", settings.movement_trails_length);
		read_color(section, "movement_trails_colour", settings.movement_trails_colour);
		settings.view_angle_lines = read_bool(section, "view_angle_lines", settings.view_angle_lines);
		settings.view_angle_lines_thickness = read_float(section, "view_angle_lines_thickness", settings.view_angle_lines_thickness);
		settings.view_angle_lines_length = read_float(section, "view_angle_lines_length", settings.view_angle_lines_length);
		read_color(section, "view_angle_lines_colour", settings.view_angle_lines_colour);
		settings.sound_esp = read_bool(section, "sound_esp", settings.sound_esp);
		read_color(section, "sound_esp_colour", settings.sound_esp_colour);
		settings.sound_esp_radius = read_float(section, "sound_esp_radius", settings.sound_esp_radius);
		settings.sound_esp_speed = read_float(section, "sound_esp_speed", settings.sound_esp_speed);
		settings.footprints = read_bool(section, "footprints", settings.footprints);
		settings.footprints_glow = read_bool(section, "footprints_glow", settings.footprints_glow);
		read_color(section, "footprints_colour", settings.footprints_colour);
		settings.footprints_radius = read_float(section, "footprints_radius", settings.footprints_radius);
		settings.footprints_speed = read_float(section, "footprints_speed", settings.footprints_speed);
		settings.state_flags = read_bool(section, "state_flags", settings.state_flags);
		settings.rig_flags = read_bool(section, "rig_flags", settings.rig_flags);
	}

	// serialize-capture plumbing: when set, save_config returns the JSON blob
	// through these instead of writing a file (memory-config path)
	static std::string g_serialize_capture;
	static bool g_serialize_mode = false;

	bool save_config(const std::string& name)
	{
		// memory-config path must never depend on the filesystem: the C:\Ivory
		// directory needs admin rights, and a memory save has to work regardless
		std::string path;
		if (!g_serialize_mode)
		{
			if (!ensure_config_directory())
				return false;

			path = get_config_path(name);
			if (path.empty())
				return false;
		}

		std::ostringstream ss;
		ss << "{\n";
		write_bool(ss, "teamcheck", settings::teamcheck);

		ss << "  \"aimbot\": {\n";
		write_bool(ss, "enabled", settings::aimbot::enabled);
		write_int(ss, "keybind", settings::aimbot::keybind);
		write_int(ss, "activation_mode", settings::aimbot::activation_mode);
		write_int(ss, "mode", settings::aimbot::mode);
		write_float(ss, "mouse_sensitivity", settings::aimbot::mouse_sensitivity);
		write_int(ss, "target_parts", settings::aimbot::target_parts);
		write_int(ss, "air_parts", settings::aimbot::air_parts);
		write_float(ss, "fov", settings::aimbot::fov);
		write_bool(ss, "use_fov", settings::aimbot::use_fov);
		write_bool(ss, "dynamic_fov", settings::aimbot::dynamic_fov);
		write_bool(ss, "ring_fov_enabled", settings::aimbot::ring_fov_enabled);
		write_float(ss, "ring_fov_inner", settings::aimbot::ring_fov_inner);
		write_float(ss, "ring_fov_outer", settings::aimbot::ring_fov_outer);
		write_bool(ss, "shake", settings::aimbot::shake);
		write_float(ss, "shake_value", settings::aimbot::shake_value);
		write_bool(ss, "humanize", settings::aimbot::humanize);
 		write_int(ss, "lock_delay", settings::aimbot::lock_delay);
		write_bool(ss, "aim_motion", settings::aimbot::aim_motion);
		write_float(ss, "aim_motion_speed", settings::aimbot::aim_motion_speed);
		write_float(ss, "aim_motion_intensity", settings::aimbot::aim_motion_intensity);
		write_bool(ss, "ping_interpolation", settings::aimbot::ping_interpolation);
		write_float(ss, "aim_deadzone", settings::aimbot::aim_deadzone);
		
		write_float(ss, "human_jitter", settings::aimbot::human_jitter);
		write_float(ss, "overshoot", settings::aimbot::overshoot);
		write_float(ss, "path_wobble", settings::aimbot::path_wobble);
		write_float(ss, "miss_chance", settings::aimbot::miss_chance);
		write_bool(ss, "enable_prediction", settings::aimbot::enable_prediction);
		write_float(ss, "prediction_x", settings::aimbot::prediction_x);
		write_float(ss, "prediction_y", settings::aimbot::prediction_y);
		write_bool(ss, "jump_prediction", settings::aimbot::jump_prediction);
		write_float(ss, "jump_prediction_value", settings::aimbot::jump_prediction_value);
		write_bool(ss, "fall_prediction", settings::aimbot::fall_prediction);
		write_float(ss, "fall_prediction_value", settings::aimbot::fall_prediction_value);
		write_float(ss, "velocity_multiplier", settings::aimbot::velocity_multiplier);
		write_bool(ss, "air_prediction_enabled", settings::aimbot::air_prediction_enabled);
		write_float(ss, "air_prediction_x", settings::aimbot::air_prediction_x);
		write_float(ss, "air_prediction_y", settings::aimbot::air_prediction_y);
		write_float(ss, "prediction_range", settings::aimbot::prediction_range);
		write_bool(ss, "adaptive_prediction", settings::aimbot::adaptive_prediction);
		write_float(ss, "adapt_strength", settings::aimbot::adapt_strength);
		write_float(ss, "adapt_speed", settings::aimbot::adapt_speed);
		write_int(ss, "point_amount", settings::aimbot::point_amount);
		write_bool(ss, "fatigue", settings::aimbot::fatigue);
		write_float(ss, "fatigue_rate", settings::aimbot::fatigue_rate);
		write_float(ss, "fatigue_decay", settings::aimbot::fatigue_decay);
		write_float(ss, "fatigue_move", settings::aimbot::fatigue_move);
		write_float(ss, "fatigue_offset", settings::aimbot::fatigue_offset);
		write_bool(ss, "prediction_dot", settings::aimbot::prediction_dot);
		write_color(ss, "prediction_color", settings::aimbot::prediction_color);
		write_float(ss, "prediction_size", settings::aimbot::prediction_size);
		write_bool(ss, "prediction_outline", settings::aimbot::prediction_outline);
		write_float(ss, "mouse_look", settings::aimbot::mouse_look);
		write_bool(ss, "offset_enabled", settings::aimbot::offset_enabled);
		write_float(ss, "offset_x", settings::aimbot::offset_x);
		write_float(ss, "offset_y", settings::aimbot::offset_y);
		write_float(ss, "shake_z", settings::aimbot::shake_z);
		write_bool(ss, "stick_assist", settings::aimbot::stick_assist);
		write_float(ss, "stick_strength", settings::aimbot::stick_strength);
		write_bool(ss, "assist_slowdown", settings::aimbot::assist_slowdown);
		write_float(ss, "assist_slowdown_speed", settings::aimbot::assist_slowdown_speed);
		write_float(ss, "assist_slowdown_strength", settings::aimbot::assist_slowdown_strength);
		write_bool(ss, "speed_cap", settings::aimbot::speed_cap);
		write_float(ss, "max_speed_px", settings::aimbot::max_speed_px);
		write_float(ss, "min_speed_px", settings::aimbot::min_speed_px);
		write_bool(ss, "teamcheck", settings::aimbot::teamcheck);
		write_bool(ss, "knock_check", settings::aimbot::knock_check);
		write_bool(ss, "disable_on_kill", settings::aimbot::disable_on_kill);
		write_int(ss, "priorities", settings::aimbot::priorities);
		write_bool(ss, "health_check_enabled", settings::aimbot::health_check_enabled);
		write_float(ss, "min_health", settings::aimbot::min_health);
		write_bool(ss, "sticky_aim", settings::aimbot::sticky_aim);
		write_bool(ss, "draw_fov", settings::aimbot::draw_fov);
		write_bool(ss, "fill_fov", settings::aimbot::fill_fov);
		write_color(ss, "fov_circle_colour", settings::aimbot::fov_circle_colour);
		write_color(ss, "fov_outline_colour", settings::aimbot::fov_outline_colour);
		write_color(ss, "fov_fill_colour", settings::aimbot::fov_fill_colour);
		ss << "  },\n";

		ss << "  \"triggerbot\": {\n";
		write_bool(ss, "enabled", settings::triggerbot::enabled);
		write_int(ss, "method", settings::triggerbot::method);
		write_int(ss, "keybind", settings::triggerbot::keybind);
		write_int(ss, "activation_mode", settings::triggerbot::activation_mode);
		write_float(ss, "threshold", settings::triggerbot::threshold);
 		write_int(ss, "delay", settings::triggerbot::delay);
		write_float(ss, "cooldown_ms", settings::triggerbot::cooldown_ms);
		write_bool(ss, "teamcheck", settings::triggerbot::teamcheck);
		write_bool(ss, "knock_check", settings::triggerbot::knock_check);
		write_bool(ss, "guncheck", settings::triggerbot::guncheck);
		write_bool(ss, "draw_fov", settings::triggerbot::draw_fov);
		write_bool(ss, "fill_fov", settings::triggerbot::fill_fov);
		write_color(ss, "fov_circle_colour", settings::triggerbot::fov_circle_colour);
		write_color(ss, "fov_outline_colour", settings::triggerbot::fov_outline_colour);
		write_color(ss, "fov_fill_colour", settings::triggerbot::fov_fill_colour);
		ss << "  },\n";

		// DISABLED silentaim config (kept in code for re-add) if(false)
		if (false) {
		ss << "  \"silentaim\": {\n";
		ss << "  },\n";
		}
		if (false) { // DISABLED raycast_silentaim config
		ss << "  \"raycast_silentaim\": {\n";
		ss << "  },\n";
		}
		ss << "  \"crosshair\": {\n";
		ss << "  },\n";

		ss << "  \"hitsounds\": {\n";
		ss << "  },\n";

		ss << "  \"killsounds\": {\n";
		ss << "  },\n";

		ss << "  \"animationchanger\": {\n";
		ss << "  },\n";

		ss << "  \"materialchanger\": {\n";
		ss << "  },\n";

		ss << "  \"godmode\": {\n";
		ss << "  },\n";

		ss << "  \"visuals\": {\n";
		write_bool(ss, "enabled", settings::visuals::enabled);
		write_int(ss, "keybind", settings::visuals::keybind);
		write_int(ss, "activation_mode", settings::visuals::activation_mode);
		write_priority_settings(ss, "neutral", settings::visuals::neutral);
		write_priority_settings(ss, "friendly", settings::visuals::friendly);
		write_priority_settings(ss, "hostile", settings::visuals::hostile);
		write_bool(ss, "teamcheck", settings::visuals::teamcheck);
		write_int(ss, "esp_font_index", settings::visuals::esp_font_index);
		write_float(ss, "esp_font_size", settings::visuals::esp_font_size);
		write_int(ss, "bounding_type", settings::visuals::bounding_type);
		for (size_t i = 0; i < settings::visuals::render_outlines.size(); i++) {
			std::string name = "render_outline_" + std::to_string(i);
			write_int(ss, name.c_str(), settings::visuals::render_outlines[i]);
		}
		write_float(ss, "skeleton_thickness", settings::visuals::skeleton_thickness);
		write_float(ss, "box_thickness", settings::visuals::box_thickness);
		write_float(ss, "healthbar_thickness", settings::visuals::healthbar_thickness);
		write_int(ss, "distance_unit", settings::visuals::distance_unit);
		write_bool(ss, "distance_check", settings::visuals::distance_check);
		write_float(ss, "max_distance", settings::visuals::max_distance);
		write_bool(ss, "sort_by_status", settings::visuals::sort_by_status);
		write_bool(ss, "knock_check", settings::visuals::knock_check);
		write_bool(ss, "local_player", settings::visuals::local_player);
		write_bool(ss, "tool_check", settings::visuals::tool_check);
		write_bool(ss, "godded_check", settings::visuals::godded_check);
		write_bool(ss, "forcefield_check", settings::visuals::forcefield_check);
		write_bool(ss, "alive_check", settings::visuals::alive_check);
		write_bool(ss, "ragdoll_check", settings::visuals::ragdoll_check);
		write_bool(ss, "visible_check", settings::visuals::visible_check);
		write_bool(ss, "ignore_full_health", settings::visuals::ignore_full_health);
		write_float(ss, "corpse_shift", settings::visuals::corpse_shift);
		write_bool(ss, "enemy_highlight", settings::visuals::enemy_highlight);
		write_color(ss, "enemy_highlight_colour", settings::visuals::enemy_highlight_colour);
		write_bool(ss, "friendly_highlight", settings::visuals::friendly_highlight);
		write_color(ss, "friendly_highlight_colour", settings::visuals::friendly_highlight_colour);
		write_bool(ss, "bar_fill", settings::visuals::bar_fill);
		write_color(ss, "bar_fill_colour", settings::visuals::bar_fill_colour);
		write_bool(ss, "health_check_enabled", settings::visuals::health_check_enabled);
		write_bool(ss, "flags", settings::visuals::flags);
		write_int(ss, "flags_mask", settings::visuals::flags_mask);
		write_color(ss, "flags_state_colour", settings::visuals::flags_state_colour);
		write_color(ss, "flags_colour", settings::visuals::flags_colour);
		write_bool(ss, "target_recolor", settings::visuals::target_recolor);
		write_color(ss, "target_recolor_colour", settings::visuals::target_recolor_colour);
		ss << "  },\n";

		ss << "  \"hitboxexpander\": {\n";
		ss << "  },\n";

		ss << "  \"hit_tracers\": {\n";
		ss << "  },\n";

		ss << "  \"hit_chams\": {\n";
		ss << "  },\n";

		ss << "  \"hit_skeleton\": {\n";
		ss << "  },\n";

		ImGuiStyle& style = ImGui::GetStyle();
		ss << "  \"theme\": {\n";
		write_color(ss, "WindowBg", &style.Colors[ImGuiCol_WindowBg].x);
		write_color(ss, "ChildBg", &style.Colors[ImGuiCol_ChildBg].x);
		write_color(ss, "Header", &style.Colors[ImGuiCol_Header].x);
		write_color(ss, "PopupBg", &style.Colors[ImGuiCol_PopupBg].x);
		write_color(ss, "Text", &style.Colors[ImGuiCol_Text].x);
		write_color(ss, "TextDisabled", &style.Colors[ImGuiCol_TextDisabled].x);
		write_color(ss, "SliderGrab", &style.Colors[ImGuiCol_SliderGrab].x);
		write_color(ss, "SliderGrabActive", &style.Colors[ImGuiCol_SliderGrabActive].x);
		write_color(ss, "Border", &style.Colors[ImGuiCol_Border].x);
		write_color(ss, "Button", &style.Colors[ImGuiCol_Button].x);
		write_color(ss, "ButtonHovered", &style.Colors[ImGuiCol_ButtonHovered].x);
		write_color(ss, "ButtonActive", &style.Colors[ImGuiCol_ButtonActive].x);
		write_color(ss, "FrameBg", &style.Colors[ImGuiCol_FrameBg].x);
		write_color(ss, "FrameBgHovered", &style.Colors[ImGuiCol_FrameBgHovered].x);
		write_color(ss, "FrameBgActive", &style.Colors[ImGuiCol_FrameBgActive].x);
		write_color(ss, "ScrollbarBg", &style.Colors[ImGuiCol_ScrollbarBg].x);
		write_color(ss, "ScrollbarGrab", &style.Colors[ImGuiCol_ScrollbarGrab].x);
		write_color(ss, "menu_color", orok_config.menu_color);
		write_color(ss, "menu_color_secondary", orok_config.menu_color_secondary);
		write_color(ss, "menu_color_text", orok_config.menu_color_text);
		write_color(ss, "menu_color_border", orok_config.menu_color_border);
		write_color(ss, "menu_color_child", orok_config.menu_color_child);
		write_color(ss, "menu_color_button", orok_config.menu_color_button);
		write_color(ss, "menu_color_button_hover", orok_config.menu_color_button_hover);
		write_color(ss, "menu_color_button_active", orok_config.menu_color_button_active);
		write_color(ss, "menu_color_frame", orok_config.menu_color_frame);
		write_color(ss, "menu_color_frame_hover", orok_config.menu_color_frame_hover);
		write_color(ss, "menu_color_frame_active", orok_config.menu_color_frame_active);
		write_color(ss, "menu_color_popup", orok_config.menu_color_popup);
		write_color(ss, "menu_color_header", orok_config.menu_color_header);
		write_color(ss, "menu_color_header_hover", orok_config.menu_color_header_hover);
		write_color(ss, "menu_color_header_active", orok_config.menu_color_header_active);
		write_color(ss, "menu_color_checkmark", orok_config.menu_color_checkmark);
		write_color(ss, "menu_color_slider_grab", orok_config.menu_color_slider_grab);
		write_color(ss, "menu_color_slider_grab_active", orok_config.menu_color_slider_grab_active);
		write_color(ss, "menu_color_scrollbar_bg", orok_config.menu_color_scrollbar_bg);
		write_color(ss, "menu_color_scrollbar_grab", orok_config.menu_color_scrollbar_grab);
		ss << "  },\n";

		ss << "  \"misc\": {\n";
		write_bool(ss, "streamproof", settings::streamproof);
		write_bool(ss, "cursor_in_capture", settings::stealth::cursor_in_capture);
		write_bool(ss, "teamcheck", settings::teamcheck);
		write_bool(ss, "vsync", settings::vsync);
		write_bool(ss, "performance_mode", settings::performance_mode);
		write_bool(ss, "unlock_fps", settings::misc::unlock_fps);
		write_int(ss, "fps_cap", settings::misc::fps_cap);
		write_bool(ss, "auto_rescan", settings::misc::auto_rescan);
		write_bool(ss, "keybind_list", settings::misc::keybind_list);
		write_bool(ss, "keybind_indicator", settings::misc::keybind_indicator);
		write_color(ss, "keybind_indicator_feature_colour", settings::misc::keybind_indicator_feature_colour);
		write_color(ss, "keybind_indicator_mode_colour", settings::misc::keybind_indicator_mode_colour);
		write_float(ss, "keybind_list_pos_x", settings::misc::keybind_list_pos_x);
		write_float(ss, "keybind_list_pos_y", settings::misc::keybind_list_pos_y);
		write_int(ss, "menu_background_style", settings::misc::menu_background_style);
		write_int(ss, "watermark_position", settings::misc::watermark_position);
		write_bool(ss, "explorer_window", settings::misc::explorer_window);
		write_bool(ss, "performance_window", settings::misc::performance_window);
		write_int(ss, "target_thread_sleep", settings::performance::target_thread_sleep);
		write_int(ss, "aim_thread_sleep", settings::performance::aim_thread_sleep);
		write_int(ss, "cache_refresh_delay", settings::performance::cache_refresh_delay);
		write_int(ss, "main_loop_delay", settings::performance::main_loop_delay);
		write_bool(ss, "thread_throttling", settings::performance::thread_throttling);
		write_bool(ss, "hide_console", settings::misc::hide_console);
		write_int(ss, "stealth_module_mode", settings::stealth::module_mode);
		write_bool(ss, "stealth_hide_console", settings::stealth::hide_console);
		write_bool(ss, "stealth_name_threads", settings::stealth::name_threads);
		write_int(ss, "stealth_panic_key", settings::stealth::panic_key);
		write_bool(ss, "bot_support", settings::misc::bot_support);
		write_bool(ss, "only_workspace_bots", settings::misc::only_workspace_bots);
		write_bool(ss, "disable_scrollbars", settings::misc::disable_scrollbars);
		write_int(ss, "preview_model", settings::misc::preview_model);
		write_string(ss, "custom_model_path", settings::misc::custom_model_path);
		write_bool(ss, "sticky_preview", settings::misc::sticky_preview);
		write_bool(ss, "confirm_unload", settings::general::confirm_unload);
		write_int(ss, "theme_index", settings::misc::theme_index);
		write_bool(ss, "custom_colors_enabled", settings::misc::custom_colors_enabled);
		for (int i = 0; i < 14; i++)
			write_color(ss, ("custom_color_" + std::to_string(i)).c_str(), settings::misc::custom_colors[i]);
		ss << "  },\n";

		ss << "  \"notifications\": {\n";
		ss << "  },\n";

		ss << "  \"btools\": {\n";
		ss << "  },\n";

		ss << "    },\n";
		ss << "    },\n";
		ss << "    \"fog\": {\n";
		ss << "    },\n";
		ss << "    },\n";
		ss << "    },\n";
		ss << "    },\n";
		ss << "    },\n";
		ss << "    },\n";
		ss << "    },\n";
		ss << "    },\n";
		ss << "    },\n";
		ss << "    },\n";
		ss << "    },\n";
		ss << "    },\n";
		ss << "    },\n";
		ss << "    },\n";
		ss << "    }\n";
		ss << "  },\n";

		ss << "  \"freezeplayer\": {\n";
		ss << "  },\n";

		ss << "  \"typingcheck\": {\n";
		write_bool(ss, "enabled", settings::typingcheck::enabled);
		ss << "  },\n";

		

		ss << "  \"skinchanger\": {\n";
		ss << "  },\n";

		ss << "  \"dhskinchanger\": {\n";
		ss << "  },\n";

		ss << "  },\n";

		ss << "  \"config_system\": {\n";
		ss << "  },\n";


		ss << "  \"hitsounds\": {\n";
		ss << "  },\n";

		ss << "  \"killsounds\": {\n";
		ss << "  },\n";

		auto get_win_metrics = [](const char* name, float def_px, float def_py, float def_sx, float def_sy,
		                          float& out_px, float& out_py, float& out_sx, float& out_sy) {
			ImGuiWindow* w = ImGui::FindWindowByName(name);
			if (w) {
				out_px = w->Pos.x;
				out_py = w->Pos.y;
				out_sx = w->Size.x;
				out_sy = w->Size.y;
			} else {
				out_px = def_px;
				out_py = def_py;
				out_sx = def_sx;
				out_sy = def_sy;
			}
		};

		float m_px, m_py, m_sx, m_sy;
		get_win_metrics("ImMagic - Menu", g_main_menu_pos.x, g_main_menu_pos.y, g_main_menu_size.x, g_main_menu_size.y, m_px, m_py, m_sx, m_sy);

		float s_px, s_py, s_sx, s_sy;
		get_win_metrics("ImMagic - Settings Window", -1, -1, 580, 440, s_px, s_py, s_sx, s_sy);

		float l_px, l_py, l_sx, l_sy;
		get_win_metrics("ImMagic - Luau Editor", -1, -1, 620, 520, l_px, l_py, l_sx, l_sy);

		float p_px, p_py, p_sx, p_sy;
		get_win_metrics("ImMagic - Preview", -1, -1, 236, 339, p_px, p_py, p_sx, p_sy);

		float pf_px, pf_py, pf_sx, pf_sy;
		get_win_metrics("ImMagic - Performance Window", -1, -1, 480, 550, pf_px, pf_py, pf_sx, pf_sy);

		float sp_px, sp_py, sp_sx, sp_sy;
		get_win_metrics("ImMagic - Spotify", -1, -1, 350, 100, sp_px, sp_py, sp_sx, sp_sy);

		float ex_px, ex_py, ex_sx, ex_sy;
		get_win_metrics("ImMagic - Explorer Window", -1, -1, 720, 480, ex_px, ex_py, ex_sx, ex_sy);

		ss << "  \"windows\": {\n";
		write_float(ss, "menu_pos_x", m_px);
		write_float(ss, "menu_pos_y", m_py);
		write_float(ss, "menu_size_w", m_sx);
		write_float(ss, "menu_size_h", m_sy);

		write_float(ss, "settings_pos_x", s_px);
		write_float(ss, "settings_pos_y", s_py);
		write_float(ss, "settings_size_w", s_sx);
		write_float(ss, "settings_size_h", s_sy);

		write_float(ss, "luau_pos_x", l_px);
		write_float(ss, "luau_pos_y", l_py);
		write_float(ss, "luau_size_w", l_sx);
		write_float(ss, "luau_size_h", l_sy);

		write_float(ss, "preview_pos_x", p_px);
		write_float(ss, "preview_pos_y", p_py);
		write_float(ss, "preview_size_w", p_sx);
		write_float(ss, "preview_size_h", p_sy);

		write_float(ss, "perf_pos_x", pf_px);
		write_float(ss, "perf_pos_y", pf_py);
		write_float(ss, "perf_size_w", pf_sx);
		write_float(ss, "perf_size_h", pf_sy);

		write_float(ss, "spotify_pos_x", sp_px);
		write_float(ss, "spotify_pos_y", sp_py);
		write_float(ss, "spotify_size_w", sp_sx);
		write_float(ss, "spotify_size_h", sp_sy);

		write_float(ss, "explorer_pos_x", ex_px);
		write_float(ss, "explorer_pos_y", ex_py);
		write_float(ss, "explorer_size_w", ex_sx);
		write_float(ss, "explorer_size_h", ex_sy);
		ss << "  }\n";

		ss << "}\n";

		if (g_serialize_mode)
		{
			g_serialize_capture = ss.str();
			return true;
		}
		return write_file_atomic(path, ss.str());
	}

	// serialize live settings to a JSON blob - memory-config path, never touches disk
	std::string serialize_settings()
	{
		g_serialize_mode = true;
		g_serialize_capture.clear();
		const bool ok = save_config("__serialize__");
		g_serialize_mode = false;
		return ok ? g_serialize_capture : std::string();
	}

	void reset_to_defaults()
	{
		settings::teamcheck = false;
		settings::aimbot::enabled = false;
		settings::aimbot::keybind = 0;
		settings::aimbot::activation_mode = 1;
		settings::aimbot::mode = 0;
		settings::aimbot::mouse_sensitivity = 1.0f;
		settings::aimbot::target_parts = 0x01;
		settings::aimbot::air_parts = 0x01;
		settings::aimbot::fov = 100.f;
		settings::aimbot::use_fov = true;
		settings::aimbot::ring_fov_enabled = false;
		settings::aimbot::ring_fov_inner = 20.f;
		settings::aimbot::ring_fov_outer = 100.f;
		settings::aimbot::enable_prediction = false;
		settings::aimbot::prediction_x = 10.f;
		settings::aimbot::prediction_y = 10.f;
		settings::aimbot::jump_prediction = false;
		settings::aimbot::jump_prediction_value = 10.f;
		settings::aimbot::fall_prediction = false;
		settings::aimbot::fall_prediction_value = 10.f;
		settings::aimbot::velocity_multiplier = 1.0f;
		settings::aimbot::air_prediction_enabled = false;
		settings::aimbot::air_prediction_x = 10.f;
		settings::aimbot::air_prediction_y = 10.f;
		settings::aimbot::prediction_range = 250.f;
		settings::aimbot::adaptive_prediction = false;
		settings::aimbot::adapt_strength = 1.0f;
		settings::aimbot::adapt_speed = 20.f;
		settings::aimbot::point_amount = 1;
		settings::aimbot::fatigue = false;
		settings::aimbot::fatigue_rate = 0.2f;
		settings::aimbot::fatigue_decay = 0.75f;
		settings::aimbot::fatigue_move = 3.0f;
		settings::aimbot::fatigue_offset = 1.2f;
		settings::aimbot::prediction_dot = false;
		settings::aimbot::prediction_color[0] = 1.f; settings::aimbot::prediction_color[1] = 0.9f; settings::aimbot::prediction_color[2] = 0.2f; settings::aimbot::prediction_color[3] = 1.f;
		settings::aimbot::prediction_size = 6.f;
		settings::aimbot::prediction_outline = false;
		settings::aimbot::mouse_look = 1.0f;
		settings::aimbot::offset_enabled = false;
		settings::aimbot::offset_x = 0.f;
		settings::aimbot::offset_y = 0.f;
		settings::aimbot::shake_z = 0.f;
		settings::aimbot::stick_assist = true;
		settings::aimbot::stick_strength = 0.4f;
		settings::aimbot::assist_slowdown = true;
		settings::aimbot::assist_slowdown_speed = 250.f;
		settings::aimbot::assist_slowdown_strength = 0.5f;
		settings::aimbot::speed_cap = false;
		settings::aimbot::max_speed_px = 120.f;
		settings::aimbot::min_speed_px = 1.5f;
		settings::aimbot::teamcheck = false;
		settings::aimbot::knock_check = false;
		settings::aimbot::priorities = 0;
		settings::aimbot::health_check_enabled = false;
		settings::aimbot::min_health = 0.f;
		settings::aimbot::sticky_aim = false;
		settings::aimbot::use_fov = true;
		settings::aimbot::draw_fov = false;
		settings::aimbot::fill_fov = false;
		settings::aimbot::fov_circle_colour[0] = 1.f; settings::aimbot::fov_circle_colour[1] = 1.f; settings::aimbot::fov_circle_colour[2] = 1.f; settings::aimbot::fov_circle_colour[3] = 1.f;
		settings::aimbot::fov_outline_colour[0] = 0.f; settings::aimbot::fov_outline_colour[1] = 0.f; settings::aimbot::fov_outline_colour[2] = 0.f; settings::aimbot::fov_outline_colour[3] = 1.f;
		settings::aimbot::fov_fill_colour[0] = 1.f; settings::aimbot::fov_fill_colour[1] = 1.f; settings::aimbot::fov_fill_colour[2] = 1.f; settings::aimbot::fov_fill_colour[3] = 0.15f;

		settings::triggerbot::enabled = false;
		settings::triggerbot::method = 0;
		settings::triggerbot::keybind = 0;
		settings::triggerbot::activation_mode = 1;
		settings::triggerbot::threshold = 10.0f;
		settings::triggerbot::delay = 1;
		settings::triggerbot::cooldown_ms = 35.0f;
		settings::triggerbot::teamcheck = false;
		settings::triggerbot::knock_check = false;
		settings::triggerbot::guncheck = false;
		settings::triggerbot::draw_fov = false;
		settings::triggerbot::fill_fov = false;
		settings::triggerbot::fov_circle_colour[0] = 1.f; settings::triggerbot::fov_circle_colour[1] = 1.f; settings::triggerbot::fov_circle_colour[2] = 1.f; settings::triggerbot::fov_circle_colour[3] = 1.f;
		settings::triggerbot::fov_outline_colour[0] = 0.f; settings::triggerbot::fov_outline_colour[1] = 0.f; settings::triggerbot::fov_outline_colour[2] = 0.f; settings::triggerbot::fov_outline_colour[3] = 1.f;
		settings::triggerbot::fov_fill_colour[0] = 1.f; settings::triggerbot::fov_fill_colour[1] = 1.f; settings::triggerbot::fov_fill_colour[2] = 1.f; settings::triggerbot::fov_fill_colour[3] = 0.15f;









		settings::visuals::neutral.box = false;
		settings::visuals::neutral.box_inline = false;
		settings::visuals::neutral.box_gradient = false;
		settings::visuals::neutral.box_type = 0;
		settings::visuals::neutral.box_fill = false;
		settings::visuals::neutral.box_fill_style = 0;
		settings::visuals::neutral.box_fill_gradient_mask = 1;
		settings::visuals::neutral.colour[0] = 1.f; settings::visuals::neutral.colour[1] = 1.f; settings::visuals::neutral.colour[2] = 1.f; settings::visuals::neutral.colour[3] = 1.f;
		settings::visuals::neutral.box_fill_colour[0] = 1.f; settings::visuals::neutral.box_fill_colour[1] = 1.f; settings::visuals::neutral.box_fill_colour[2] = 1.f; settings::visuals::neutral.box_fill_colour[3] = 0.2f;
		settings::visuals::neutral.box_fill_colour_mid[0] = 1.f; settings::visuals::neutral.box_fill_colour_mid[1] = 1.f; settings::visuals::neutral.box_fill_colour_mid[2] = 1.f; settings::visuals::neutral.box_fill_colour_mid[3] = 0.2f;
		settings::visuals::neutral.box_fill_colour_low[0] = 1.f; settings::visuals::neutral.box_fill_colour_low[1] = 1.f; settings::visuals::neutral.box_fill_colour_low[2] = 1.f; settings::visuals::neutral.box_fill_colour_low[3] = 0.2f;
		settings::visuals::neutral.username = false;
		settings::visuals::neutral.name_bracket_style = 0;
		settings::visuals::neutral.username_colour[0] = 1.f; settings::visuals::neutral.username_colour[1] = 1.f; settings::visuals::neutral.username_colour[2] = 1.f; settings::visuals::neutral.username_colour[3] = 1.f;
		settings::visuals::neutral.distance = false;
		settings::visuals::neutral.distance_colour[0] = 1.f; settings::visuals::neutral.distance_colour[1] = 1.f; settings::visuals::neutral.distance_colour[2] = 1.f; settings::visuals::neutral.distance_colour[3] = 1.f;
		settings::visuals::neutral.tool = false;
		settings::visuals::neutral.tool_colour[0] = 1.f; settings::visuals::neutral.tool_colour[1] = 1.f; settings::visuals::neutral.tool_colour[2] = 1.f; settings::visuals::neutral.tool_colour[3] = 1.f;
		settings::visuals::neutral.healthbar = false;
		settings::visuals::neutral.healthbar_style = 0;
		settings::visuals::neutral.healthbar_padding = 4.f;
		settings::visuals::neutral.healthbar_colour[0] = 0.f; settings::visuals::neutral.healthbar_colour[1] = 1.f; settings::visuals::neutral.healthbar_colour[2] = 0.f; settings::visuals::neutral.healthbar_colour[3] = 1.f;
		settings::visuals::neutral.healthbar_colour_mid[0] = 1.f; settings::visuals::neutral.healthbar_colour_mid[1] = 1.f; settings::visuals::neutral.healthbar_colour_mid[2] = 0.f; settings::visuals::neutral.healthbar_colour_mid[3] = 1.f;
		settings::visuals::neutral.healthbar_colour_low[0] = 1.f; settings::visuals::neutral.healthbar_colour_low[1] = 0.f; settings::visuals::neutral.healthbar_colour_low[2] = 0.f; settings::visuals::neutral.healthbar_colour_low[3] = 1.f;
		settings::visuals::neutral.tracers = false;
		settings::visuals::neutral.tracers_colour[0] = 1.f; settings::visuals::neutral.tracers_colour[1] = 1.f; settings::visuals::neutral.tracers_colour[2] = 1.f; settings::visuals::neutral.tracers_colour[3] = 1.f;
		settings::visuals::neutral.chams = false;
		settings::visuals::neutral.chams_type = 0;
		settings::visuals::neutral.chams_colour[0] = 0.855f; settings::visuals::neutral.chams_colour[1] = 0.278f; settings::visuals::neutral.chams_colour[2] = 0.478f; settings::visuals::neutral.chams_colour[3] = 0.5f;
		settings::visuals::neutral.chams_outline_colour[0] = 0.f; settings::visuals::neutral.chams_outline_colour[1] = 0.f; settings::visuals::neutral.chams_outline_colour[2] = 0.f; settings::visuals::neutral.chams_outline_colour[3] = 1.f;
		settings::visuals::neutral.skeleton = false;
		settings::visuals::neutral.skeleton_colour[0] = 1.f; settings::visuals::neutral.skeleton_colour[1] = 1.f; settings::visuals::neutral.skeleton_colour[2] = 1.f; settings::visuals::neutral.skeleton_colour[3] = 1.f;
		settings::visuals::neutral.skeleton_outline_colour[0] = 0.f; settings::visuals::neutral.skeleton_outline_colour[1] = 0.f; settings::visuals::neutral.skeleton_outline_colour[2] = 0.f; settings::visuals::neutral.skeleton_outline_colour[3] = 1.f;
		settings::visuals::neutral.corpse = false;
		settings::visuals::neutral.corpse_colour[0] = 1.f; settings::visuals::neutral.corpse_colour[1] = 1.f; settings::visuals::neutral.corpse_colour[2] = 1.f; settings::visuals::neutral.corpse_colour[3] = 0.25f;
		settings::visuals::neutral.corpse_outline_colour[0] = 0.f; settings::visuals::neutral.corpse_outline_colour[1] = 0.f; settings::visuals::neutral.corpse_outline_colour[2] = 0.f; settings::visuals::neutral.corpse_outline_colour[3] = 1.f;
		settings::visuals::neutral.corpse_names = false;
		settings::visuals::neutral.corpse_names_colour[0] = 1.f; settings::visuals::neutral.corpse_names_colour[1] = 1.f; settings::visuals::neutral.corpse_names_colour[2] = 1.f; settings::visuals::neutral.corpse_names_colour[3] = 1.f;

		settings::visuals::friendly = settings::visuals::neutral;
		settings::visuals::hostile = settings::visuals::neutral;
		settings::visuals::enabled = false;
		settings::visuals::keybind = 0;
		settings::visuals::activation_mode = 1;
		settings::visuals::teamcheck = false;
		settings::visuals::esp_font_index = 0;
		settings::visuals::esp_font_size = 12.f;
		settings::visuals::bounding_type = 0;
		settings::visuals::render_outlines = { 1, 1, 1, 1, 1, 1, 1, 1 };
		settings::visuals::skeleton_thickness = 1.5f;
		settings::visuals::box_thickness = 1.0f;
		settings::visuals::healthbar_thickness = 2.0f;
		settings::visuals::distance_unit = 0;
		settings::visuals::distance_check = false;
		settings::visuals::sort_by_status = false;
		settings::visuals::knock_check = false;
		settings::visuals::local_player = false;
		settings::visuals::tool_check = false;
		settings::visuals::godded_check = false;
		settings::visuals::forcefield_check = false;
		settings::visuals::ignore_full_health = false;
		settings::visuals::corpse_shift = 0.0f;
		settings::visuals::enemy_highlight = false;
		settings::visuals::enemy_highlight_colour[0] = 1.f; settings::visuals::enemy_highlight_colour[1] = 0.2f; settings::visuals::enemy_highlight_colour[2] = 0.2f; settings::visuals::enemy_highlight_colour[3] = 0.6f;
		settings::visuals::friendly_highlight = false;
		settings::visuals::friendly_highlight_colour[0] = 0.2f; settings::visuals::friendly_highlight_colour[1] = 1.f; settings::visuals::friendly_highlight_colour[2] = 0.2f; settings::visuals::friendly_highlight_colour[3] = 0.6f;
		settings::visuals::bar_fill = false;
		settings::visuals::bar_fill_colour[0] = 0.1f; settings::visuals::bar_fill_colour[1] = 0.1f; settings::visuals::bar_fill_colour[2] = 0.1f; settings::visuals::bar_fill_colour[3] = 0.5f;
		settings::visuals::health_check_enabled = false;
		settings::visuals::flags = false;
		settings::visuals::flags_mask = 0;
		settings::visuals::flags_state_colour[0] = 1.f; settings::visuals::flags_state_colour[1] = 1.f; settings::visuals::flags_state_colour[2] = 1.f; settings::visuals::flags_state_colour[3] = 1.f;
		settings::visuals::flags_colour[0] = 1.f; settings::visuals::flags_colour[1] = 1.f; settings::visuals::flags_colour[2] = 1.f; settings::visuals::flags_colour[3] = 1.f;
		settings::visuals::target_recolor = false;
		settings::visuals::target_recolor_colour[0] = 1.f; settings::visuals::target_recolor_colour[1] = 0.f; settings::visuals::target_recolor_colour[2] = 0.f; settings::visuals::target_recolor_colour[3] = 1.f;
		for (int i = 0; i < 4; ++i) {
		}

		for (int i = 0; i < 4; i++) {
		}





		settings::streamproof = false;
		settings::teamcheck = false;
		settings::vsync = false;
		settings::performance_mode = false;
		settings::misc::unlock_fps = false;
		settings::misc::fps_cap = 180;
		settings::misc::auto_rescan = true;
		settings::misc::keybind_list = true;
		settings::misc::keybind_indicator = false;
		for (int i = 0; i < 4; ++i) settings::misc::keybind_indicator_feature_colour[i] = (i < 3) ? 1.0f : 1.0f;
		for (int i = 0; i < 4; ++i) settings::misc::keybind_indicator_mode_colour[i]    = (i < 3) ? 0.65f : 1.0f;
		settings::misc::watermark = true;
		settings::misc::watermark_pos_x = 10.0f;
		settings::misc::watermark_pos_y = 10.0f;
		settings::misc::keybind_list_pos_x = 10.0f;
		settings::misc::keybind_list_pos_y = 45.0f;
		settings::misc::menu_background_style = 2;
		settings::misc::explorer_window = false;
		settings::misc::performance_window = false;
		settings::misc::model_viewer_window = false;
		settings::misc::preview_model = 2; // Default to Tung Tung Tung Sahur
		settings::misc::custom_model_path[0] = '\0';
		settings::misc::sticky_preview = false;
		settings::misc::hide_console = false;
		settings::misc::bot_support = false;
		settings::stealth::module_mode = 1;
		settings::stealth::hide_console = true;
		settings::stealth::name_threads = true;
		settings::stealth::panic_key = 0;

		Theme::ResetDefaults();

		for (int i = 0; i < 4; ++i) {
		}
		for (int i = 0; i < 4; ++i) {
		}
		for (int i = 0; i < 4; ++i) {
		}

		settings::typingcheck::enabled = true;

		

		{
			std::lock_guard<std::mutex> lock(cache::mtx);
		}




	}

	bool load_config(const std::string& name)
	{
		std::string path = get_config_path(name);
		if (path.empty() || !std::filesystem::exists(path))
			return false;

		std::ifstream file(path);
		if (!file.is_open())
			return false;

		std::string json((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
		file.close();
		return deserialize_settings(json);
	}

	// parse a config JSON blob into live settings (shared by disk + memory paths)
	bool deserialize_settings(const std::string& json)
	{
		if (json.empty() || json.find('{') == std::string::npos)
			return false;

		reset_to_defaults();
		settings::teamcheck = read_bool(json, "teamcheck", settings::teamcheck);

		std::string aimbot_section = extract_section(json, "aimbot");
		std::string triggerbot_section = extract_section(json, "triggerbot");
		std::string silentaim_section = extract_section(json, "silentaim");
		std::string raycast_silentaim_section = extract_section(json, "raycast_silentaim");
		std::string crosshair_section = extract_section(json, "crosshair");
		std::string animationchanger_section = extract_section(json, "animationchanger");
		std::string materialchanger_section = extract_section(json, "materialchanger");
		std::string visuals_section = extract_section(json, "visuals");
		std::string hitboxexpander_section = extract_section(json, "hitboxexpander");
		std::string hit_tracers_section = extract_section(json, "hit_tracers");
		std::string hit_chams_section = extract_section(json, "hit_chams");
		std::string hit_skeleton_section = extract_section(json, "hit_skeleton");
		std::string theme_section = extract_section(json, "theme");
		std::string misc_section = extract_section(json, "misc");
		std::string lighting_section = extract_section(json, "lighting");
		std::string freezeplayer_section = extract_section(json, "freezeplayer");
		std::string typingcheck_section = extract_section(json, "typingcheck");
		std::string config_system_section = extract_section(json, "config_system");
		
		std::string hitsounds_section = extract_section(json, "hitsounds");
		std::string killsounds_section = extract_section(json, "killsounds");
		std::string godmode_section = extract_section(json, "godmode");
		std::string dh_skinchanger_section = extract_section(json, "dh_skinchanger");
		std::string skinchanger_section = extract_section(json, "skinchanger");



		std::string ambient_section = extract_section(lighting_section, "ambient");
		std::string shadows_section = extract_section(lighting_section, "shadows");
		std::string fog_section = extract_section(lighting_section, "fog");
		std::string clocktime_section = extract_section(lighting_section, "clocktime");
		std::string atmosphere_section = extract_section(lighting_section, "atmosphere");
		std::string colorshift_section = extract_section(lighting_section, "colorshift");
		std::string exposure_section = extract_section(lighting_section, "exposure");
		std::string starcount_section = extract_section(lighting_section, "starcount");
		std::string sunmoon_section = extract_section(lighting_section, "sunmoon");
		std::string brightness_section = extract_section(lighting_section, "brightness");
		std::string environment_section = extract_section(lighting_section, "environment");
		std::string latitude_section = extract_section(lighting_section, "latitude");
		std::string terrain_section = extract_section(lighting_section, "terrain");
		std::string bloom_section = extract_section(lighting_section, "bloom");
		std::string sunrays_section = extract_section(lighting_section, "sunrays");
		std::string color_correction_section = extract_section(lighting_section, "color_correction");
		std::string depth_of_field_section = extract_section(lighting_section, "depth_of_field");

		if (!aimbot_section.empty())
		{
			settings::aimbot::enabled = read_bool(aimbot_section, "enabled", settings::aimbot::enabled);
			settings::aimbot::keybind = read_int(aimbot_section, "keybind", settings::aimbot::keybind);
			settings::aimbot::activation_mode = read_int(aimbot_section, "activation_mode", settings::aimbot::activation_mode);
			settings::aimbot::mode = read_int(aimbot_section, "mode", settings::aimbot::mode);
			settings::aimbot::mouse_sensitivity = read_float(aimbot_section, "mouse_sensitivity", settings::aimbot::mouse_sensitivity);
			settings::aimbot::target_parts = read_int(aimbot_section, "target_parts", 0x01);
			settings::aimbot::air_parts = read_int(aimbot_section, "air_parts", 0x01);
			if (settings::aimbot::target_parts == 0) settings::aimbot::target_parts = 0x01;
			if (settings::aimbot::air_parts == 0) settings::aimbot::air_parts = 0x01;
			settings::aimbot::fov = read_float(aimbot_section, "fov", settings::aimbot::fov);
			settings::aimbot::use_fov = read_bool(aimbot_section, "use_fov", settings::aimbot::use_fov);
			settings::aimbot::dynamic_fov = read_bool(aimbot_section, "dynamic_fov", settings::aimbot::dynamic_fov);
			settings::aimbot::ring_fov_enabled = read_bool(aimbot_section, "ring_fov_enabled", settings::aimbot::ring_fov_enabled);
			settings::aimbot::ring_fov_inner = read_float(aimbot_section, "ring_fov_inner", settings::aimbot::ring_fov_inner);
			settings::aimbot::ring_fov_outer = read_float(aimbot_section, "ring_fov_outer", settings::aimbot::ring_fov_outer);
			settings::aimbot::shake = read_bool(aimbot_section, "shake", settings::aimbot::shake);
			settings::aimbot::shake_value = read_float(aimbot_section, "shake_value", settings::aimbot::shake_value);
			settings::aimbot::humanize = read_bool(aimbot_section, "humanize", settings::aimbot::humanize);
			settings::aimbot::lock_delay = read_int(aimbot_section, "lock_delay", settings::aimbot::lock_delay);
			settings::aimbot::aim_motion = read_bool(aimbot_section, "aim_motion", settings::aimbot::aim_motion);
			settings::aimbot::aim_motion_speed = read_float(aimbot_section, "aim_motion_speed", settings::aimbot::aim_motion_speed);
			settings::aimbot::aim_motion_intensity = read_float(aimbot_section, "aim_motion_intensity", settings::aimbot::aim_motion_intensity);
			settings::aimbot::ping_interpolation = read_bool(aimbot_section, "ping_interpolation", settings::aimbot::ping_interpolation);
			settings::aimbot::aim_deadzone = read_float(aimbot_section, "aim_deadzone", settings::aimbot::aim_deadzone);
			
			settings::aimbot::human_jitter = read_float(aimbot_section, "human_jitter", settings::aimbot::human_jitter);
			settings::aimbot::overshoot = read_float(aimbot_section, "overshoot", settings::aimbot::overshoot);
			settings::aimbot::path_wobble = read_float(aimbot_section, "path_wobble", settings::aimbot::path_wobble);
			settings::aimbot::miss_chance = read_float(aimbot_section, "miss_chance", settings::aimbot::miss_chance);
			settings::aimbot::use_fov = true;
			settings::aimbot::enable_prediction = read_bool(aimbot_section, "enable_prediction", settings::aimbot::enable_prediction);
			settings::aimbot::prediction_x = read_float(aimbot_section, "prediction_x", settings::aimbot::prediction_x);
			settings::aimbot::prediction_y = read_float(aimbot_section, "prediction_y", settings::aimbot::prediction_y);
			settings::aimbot::jump_prediction = read_bool(aimbot_section, "jump_prediction", settings::aimbot::jump_prediction);
			settings::aimbot::jump_prediction_value = read_float(aimbot_section, "jump_prediction_value", settings::aimbot::jump_prediction_value);
			settings::aimbot::fall_prediction = read_bool(aimbot_section, "fall_prediction", settings::aimbot::fall_prediction);
			settings::aimbot::fall_prediction_value = read_float(aimbot_section, "fall_prediction_value", settings::aimbot::fall_prediction_value);
			settings::aimbot::velocity_multiplier = read_float(aimbot_section, "velocity_multiplier", settings::aimbot::velocity_multiplier);
			settings::aimbot::air_prediction_enabled = read_bool(aimbot_section, "air_prediction_enabled", settings::aimbot::air_prediction_enabled);
			settings::aimbot::air_prediction_x = read_float(aimbot_section, "air_prediction_x", settings::aimbot::air_prediction_x);
			settings::aimbot::air_prediction_y = read_float(aimbot_section, "air_prediction_y", settings::aimbot::air_prediction_y);
			settings::aimbot::prediction_range = read_float(aimbot_section, "prediction_range", settings::aimbot::prediction_range);
			settings::aimbot::adaptive_prediction = read_bool(aimbot_section, "adaptive_prediction", settings::aimbot::adaptive_prediction);
			settings::aimbot::adapt_strength = read_float(aimbot_section, "adapt_strength", settings::aimbot::adapt_strength);
			settings::aimbot::adapt_speed = read_float(aimbot_section, "adapt_speed", settings::aimbot::adapt_speed);
			settings::aimbot::point_amount = read_int(aimbot_section, "point_amount", settings::aimbot::point_amount);
			settings::aimbot::fatigue = read_bool(aimbot_section, "fatigue", settings::aimbot::fatigue);
			settings::aimbot::fatigue_rate = read_float(aimbot_section, "fatigue_rate", settings::aimbot::fatigue_rate);
			settings::aimbot::fatigue_decay = read_float(aimbot_section, "fatigue_decay", settings::aimbot::fatigue_decay);
			settings::aimbot::fatigue_move = read_float(aimbot_section, "fatigue_move", settings::aimbot::fatigue_move);
			settings::aimbot::fatigue_offset = read_float(aimbot_section, "fatigue_offset", settings::aimbot::fatigue_offset);
			settings::aimbot::prediction_dot = read_bool(aimbot_section, "prediction_dot", settings::aimbot::prediction_dot);
			read_color(aimbot_section, "prediction_color", settings::aimbot::prediction_color);
			settings::aimbot::prediction_size = read_float(aimbot_section, "prediction_size", settings::aimbot::prediction_size);
			settings::aimbot::prediction_outline = read_bool(aimbot_section, "prediction_outline", settings::aimbot::prediction_outline);
			settings::aimbot::mouse_look = read_float(aimbot_section, "mouse_look", settings::aimbot::mouse_look);
			settings::aimbot::offset_enabled = read_bool(aimbot_section, "offset_enabled", settings::aimbot::offset_enabled);
			settings::aimbot::offset_x = read_float(aimbot_section, "offset_x", settings::aimbot::offset_x);
			settings::aimbot::offset_y = read_float(aimbot_section, "offset_y", settings::aimbot::offset_y);
			settings::aimbot::shake_z = read_float(aimbot_section, "shake_z", settings::aimbot::shake_z);
			settings::aimbot::stick_assist = read_bool(aimbot_section, "stick_assist", settings::aimbot::stick_assist);
			settings::aimbot::stick_strength = read_float(aimbot_section, "stick_strength", settings::aimbot::stick_strength);
			settings::aimbot::assist_slowdown = read_bool(aimbot_section, "assist_slowdown", settings::aimbot::assist_slowdown);
			settings::aimbot::assist_slowdown_speed = read_float(aimbot_section, "assist_slowdown_speed", settings::aimbot::assist_slowdown_speed);
			settings::aimbot::assist_slowdown_strength = read_float(aimbot_section, "assist_slowdown_strength", settings::aimbot::assist_slowdown_strength);
			settings::aimbot::speed_cap = read_bool(aimbot_section, "speed_cap", settings::aimbot::speed_cap);
			settings::aimbot::max_speed_px = read_float(aimbot_section, "max_speed_px", settings::aimbot::max_speed_px);
			settings::aimbot::min_speed_px = read_float(aimbot_section, "min_speed_px", settings::aimbot::min_speed_px);
			settings::aimbot::teamcheck = read_bool(aimbot_section, "teamcheck", settings::aimbot::teamcheck);
			settings::aimbot::knock_check = read_bool(aimbot_section, "knock_check", settings::aimbot::knock_check);
			settings::aimbot::disable_on_kill = read_bool(aimbot_section, "disable_on_kill", settings::aimbot::disable_on_kill);
			settings::aimbot::priorities = read_int(aimbot_section, "priorities", settings::aimbot::priorities);
			settings::aimbot::health_check_enabled = read_bool(aimbot_section, "health_check_enabled", settings::aimbot::health_check_enabled);
			settings::aimbot::min_health = read_float(aimbot_section, "min_health", settings::aimbot::min_health);
			settings::aimbot::sticky_aim = read_bool(aimbot_section, "sticky_aim", settings::aimbot::sticky_aim);
			settings::aimbot::draw_fov = read_bool(aimbot_section, "draw_fov", settings::aimbot::draw_fov);
			settings::aimbot::fill_fov = read_bool(aimbot_section, "fill_fov", settings::aimbot::fill_fov);
			read_color(aimbot_section, "fov_circle_colour", settings::aimbot::fov_circle_colour);
			read_color(aimbot_section, "fov_outline_colour", settings::aimbot::fov_outline_colour);
			read_color(aimbot_section, "fov_fill_colour", settings::aimbot::fov_fill_colour);
		}

		if (!triggerbot_section.empty())
		{
			settings::triggerbot::enabled = read_bool(triggerbot_section, "enabled", settings::triggerbot::enabled);
			settings::triggerbot::method = read_int(triggerbot_section, "method", settings::triggerbot::method);
			settings::triggerbot::keybind = read_int(triggerbot_section, "keybind", settings::triggerbot::keybind);
			settings::triggerbot::activation_mode = read_int(triggerbot_section, "activation_mode", settings::triggerbot::activation_mode);
			settings::triggerbot::threshold = read_float(triggerbot_section, "threshold", settings::triggerbot::threshold);
			settings::triggerbot::delay = read_int(triggerbot_section, "delay", settings::triggerbot::delay);
			settings::triggerbot::cooldown_ms = read_float(triggerbot_section, "cooldown_ms", settings::triggerbot::cooldown_ms);
			settings::triggerbot::teamcheck = read_bool(triggerbot_section, "teamcheck", settings::triggerbot::teamcheck);
			settings::triggerbot::knock_check = read_bool(triggerbot_section, "knock_check", settings::triggerbot::knock_check);
			settings::triggerbot::guncheck = read_bool(triggerbot_section, "guncheck", settings::triggerbot::guncheck);
			settings::triggerbot::draw_fov = read_bool(triggerbot_section, "draw_fov", settings::triggerbot::draw_fov);
			settings::triggerbot::fill_fov = read_bool(triggerbot_section, "fill_fov", settings::triggerbot::fill_fov);
			read_color(triggerbot_section, "fov_circle_colour", settings::triggerbot::fov_circle_colour);
			read_color(triggerbot_section, "fov_outline_colour", settings::triggerbot::fov_outline_colour);
			read_color(triggerbot_section, "fov_fill_colour", settings::triggerbot::fov_fill_colour);
		}

		if (!silentaim_section.empty())
		{
		}

		if (!raycast_silentaim_section.empty())
		{
		}

		if (!crosshair_section.empty())
		{
		}

		if (!hitsounds_section.empty())
		{
		}

		if (!killsounds_section.empty())
		{
		}

		if (!animationchanger_section.empty())
		{
		}

		if (!materialchanger_section.empty())
		{
		}

		if (!godmode_section.empty())
		{
		}

		if (!visuals_section.empty())
		{
			settings::visuals::enabled = read_bool(visuals_section, "enabled", settings::visuals::enabled);
			settings::visuals::keybind = read_int(visuals_section, "keybind", settings::visuals::keybind);
			settings::visuals::activation_mode = read_int(visuals_section, "activation_mode", settings::visuals::activation_mode);
			read_priority_settings(visuals_section, "neutral", settings::visuals::neutral);
			read_priority_settings(visuals_section, "friendly", settings::visuals::friendly);
			read_priority_settings(visuals_section, "hostile", settings::visuals::hostile);
			settings::visuals::teamcheck = read_bool(visuals_section, "teamcheck", settings::visuals::teamcheck);
			settings::visuals::esp_font_index = read_int(visuals_section, "esp_font_index", settings::visuals::esp_font_index);
			settings::visuals::esp_font_size = read_float(visuals_section, "esp_font_size", settings::visuals::esp_font_size);
			settings::visuals::bounding_type = read_int(visuals_section, "bounding_type", settings::visuals::bounding_type);
			for (size_t i = 0; i < settings::visuals::render_outlines.size(); i++) {
				std::string name = "render_outline_" + std::to_string(i);
				settings::visuals::render_outlines[i] = read_int(visuals_section, name.c_str(), settings::visuals::render_outlines[i]);
			}
			settings::visuals::skeleton_thickness = read_float(visuals_section, "skeleton_thickness", settings::visuals::skeleton_thickness);
			settings::visuals::box_thickness = read_float(visuals_section, "box_thickness", settings::visuals::box_thickness);
			settings::visuals::healthbar_thickness = read_float(visuals_section, "healthbar_thickness", settings::visuals::healthbar_thickness);
			settings::visuals::distance_unit = read_int(visuals_section, "distance_unit", settings::visuals::distance_unit);
			settings::visuals::distance_check = read_bool(visuals_section, "distance_check", settings::visuals::distance_check);
			settings::visuals::max_distance = read_float(visuals_section, "max_distance", settings::visuals::max_distance);
			settings::visuals::sort_by_status = read_bool(visuals_section, "sort_by_status", settings::visuals::sort_by_status);
			settings::visuals::knock_check = read_bool(visuals_section, "knock_check", settings::visuals::knock_check);
			settings::visuals::local_player = read_bool(visuals_section, "local_player", settings::visuals::local_player);
			settings::visuals::tool_check = read_bool(visuals_section, "tool_check", settings::visuals::tool_check);
			settings::visuals::godded_check = read_bool(visuals_section, "godded_check", settings::visuals::godded_check);
			settings::visuals::forcefield_check = read_bool(visuals_section, "forcefield_check", settings::visuals::forcefield_check);
			settings::visuals::alive_check = read_bool(visuals_section, "alive_check", settings::visuals::alive_check);
			settings::visuals::ragdoll_check = read_bool(visuals_section, "ragdoll_check", settings::visuals::ragdoll_check);
			settings::visuals::visible_check = read_bool(visuals_section, "visible_check", settings::visuals::visible_check);
			settings::visuals::ignore_full_health = read_bool(visuals_section, "ignore_full_health", settings::visuals::ignore_full_health);
			settings::visuals::corpse_shift = read_float(visuals_section, "corpse_shift", settings::visuals::corpse_shift);
			settings::visuals::enemy_highlight = read_bool(visuals_section, "enemy_highlight", settings::visuals::enemy_highlight);
			read_color(visuals_section, "enemy_highlight_colour", settings::visuals::enemy_highlight_colour);
			settings::visuals::friendly_highlight = read_bool(visuals_section, "friendly_highlight", settings::visuals::friendly_highlight);
			read_color(visuals_section, "friendly_highlight_colour", settings::visuals::friendly_highlight_colour);
			settings::visuals::bar_fill = read_bool(visuals_section, "bar_fill", settings::visuals::bar_fill);
			read_color(visuals_section, "bar_fill_colour", settings::visuals::bar_fill_colour);
			settings::visuals::health_check_enabled = read_bool(visuals_section, "health_check_enabled", settings::visuals::health_check_enabled);
			settings::visuals::flags = read_bool(visuals_section, "flags", settings::visuals::flags);
			settings::visuals::flags_mask = read_int(visuals_section, "flags_mask", settings::visuals::flags_mask);
			read_color(visuals_section, "flags_state_colour", settings::visuals::flags_state_colour);
			read_color(visuals_section, "flags_colour", settings::visuals::flags_colour);
			settings::visuals::target_recolor = read_bool(visuals_section, "target_recolor", settings::visuals::target_recolor);
			read_color(visuals_section, "target_recolor_colour", settings::visuals::target_recolor_colour);
		}

		if (!hitboxexpander_section.empty())
		{
		}

		if (!hit_tracers_section.empty())
		{
		}

		if (!hit_chams_section.empty())
		{
		}

		if (!hit_skeleton_section.empty())
		{
		}

		if (!theme_section.empty())
		{
			ImGuiStyle& style = ImGui::GetStyle();
			read_color(theme_section, "WindowBg", &style.Colors[ImGuiCol_WindowBg].x);
			read_color(theme_section, "ChildBg", &style.Colors[ImGuiCol_ChildBg].x);
			read_color(theme_section, "Header", &style.Colors[ImGuiCol_Header].x);
			read_color(theme_section, "PopupBg", &style.Colors[ImGuiCol_PopupBg].x);
			read_color(theme_section, "Text", &style.Colors[ImGuiCol_Text].x);
			read_color(theme_section, "TextDisabled", &style.Colors[ImGuiCol_TextDisabled].x);
			read_color(theme_section, "SliderGrab", &style.Colors[ImGuiCol_SliderGrab].x);
			read_color(theme_section, "SliderGrabActive", &style.Colors[ImGuiCol_SliderGrabActive].x);
			read_color(theme_section, "Border", &style.Colors[ImGuiCol_Border].x);
			read_color(theme_section, "Button", &style.Colors[ImGuiCol_Button].x);
			read_color(theme_section, "ButtonHovered", &style.Colors[ImGuiCol_ButtonHovered].x);
			read_color(theme_section, "ButtonActive", &style.Colors[ImGuiCol_ButtonActive].x);
			read_color(theme_section, "FrameBg", &style.Colors[ImGuiCol_FrameBg].x);
			read_color(theme_section, "FrameBgHovered", &style.Colors[ImGuiCol_FrameBgHovered].x);
			read_color(theme_section, "FrameBgActive", &style.Colors[ImGuiCol_FrameBgActive].x);
			read_color(theme_section, "ScrollbarBg", &style.Colors[ImGuiCol_ScrollbarBg].x);
			read_color(theme_section, "ScrollbarGrab", &style.Colors[ImGuiCol_ScrollbarGrab].x);
			read_color(theme_section, "menu_color", orok_config.menu_color);
			read_color(theme_section, "menu_color_secondary", orok_config.menu_color_secondary);
			read_color(theme_section, "menu_color_text", orok_config.menu_color_text);
			read_color(theme_section, "menu_color_border", orok_config.menu_color_border);
			read_color(theme_section, "menu_color_child", orok_config.menu_color_child);
			read_color(theme_section, "menu_color_button", orok_config.menu_color_button);
			read_color(theme_section, "menu_color_button_hover", orok_config.menu_color_button_hover);
			read_color(theme_section, "menu_color_button_active", orok_config.menu_color_button_active);
			read_color(theme_section, "menu_color_frame", orok_config.menu_color_frame);
			read_color(theme_section, "menu_color_frame_hover", orok_config.menu_color_frame_hover);
			read_color(theme_section, "menu_color_frame_active", orok_config.menu_color_frame_active);
			read_color(theme_section, "menu_color_popup", orok_config.menu_color_popup);
			read_color(theme_section, "menu_color_header", orok_config.menu_color_header);
			read_color(theme_section, "menu_color_header_hover", orok_config.menu_color_header_hover);
			read_color(theme_section, "menu_color_header_active", orok_config.menu_color_header_active);
			read_color(theme_section, "menu_color_checkmark", orok_config.menu_color_checkmark);
			read_color(theme_section, "menu_color_slider_grab", orok_config.menu_color_slider_grab);
			read_color(theme_section, "menu_color_slider_grab_active", orok_config.menu_color_slider_grab_active);
			read_color(theme_section, "menu_color_scrollbar_bg", orok_config.menu_color_scrollbar_bg);
			read_color(theme_section, "menu_color_scrollbar_grab", orok_config.menu_color_scrollbar_grab);
		}

		if (!misc_section.empty())
		{
			settings::streamproof = read_bool(misc_section, "streamproof", settings::streamproof);
			settings::stealth::cursor_in_capture = read_bool(misc_section, "cursor_in_capture", settings::stealth::cursor_in_capture);
			settings::teamcheck = read_bool(misc_section, "teamcheck", settings::teamcheck);
			settings::vsync = read_bool(misc_section, "vsync", settings::vsync);
			settings::performance_mode = read_bool(misc_section, "performance_mode", settings::performance_mode);
			settings::misc::unlock_fps = read_bool(misc_section, "unlock_fps", settings::misc::unlock_fps);
			settings::misc::fps_cap = read_int(misc_section, "fps_cap", settings::misc::fps_cap);
			settings::misc::auto_rescan = read_bool(misc_section, "auto_rescan", settings::misc::auto_rescan);
			settings::misc::keybind_list = read_bool(misc_section, "keybind_list", settings::misc::keybind_list);
			settings::misc::keybind_indicator = read_bool(misc_section, "keybind_indicator", settings::misc::keybind_indicator);
			read_color(misc_section, "keybind_indicator_feature_colour", settings::misc::keybind_indicator_feature_colour);
			read_color(misc_section, "keybind_indicator_mode_colour", settings::misc::keybind_indicator_mode_colour);
			settings::misc::keybind_list_pos_x = read_float(misc_section, "keybind_list_pos_x", settings::misc::keybind_list_pos_x);
			settings::misc::keybind_list_pos_y = read_float(misc_section, "keybind_list_pos_y", settings::misc::keybind_list_pos_y);
			settings::misc::menu_background_style = read_int(misc_section, "menu_background_style", settings::misc::menu_background_style);
			settings::misc::watermark_position = read_int(misc_section, "watermark_position", settings::misc::watermark_position);
			settings::misc::explorer_window = read_bool(misc_section, "explorer_window", settings::misc::explorer_window);
			settings::misc::performance_window = read_bool(misc_section, "performance_window", settings::misc::performance_window);
			settings::performance::target_thread_sleep = read_int(misc_section, "target_thread_sleep", settings::performance::target_thread_sleep);
			settings::performance::aim_thread_sleep = read_int(misc_section, "aim_thread_sleep", settings::performance::aim_thread_sleep);
			settings::performance::cache_refresh_delay = read_int(misc_section, "cache_refresh_delay", settings::performance::cache_refresh_delay);
			settings::performance::main_loop_delay = read_int(misc_section, "main_loop_delay", settings::performance::main_loop_delay);
			settings::performance::thread_throttling = read_bool(misc_section, "thread_throttling", settings::performance::thread_throttling);
			settings::misc::hide_console = read_bool(misc_section, "hide_console", settings::misc::hide_console);
			settings::stealth::module_mode = read_int(misc_section, "stealth_module_mode", settings::stealth::module_mode);
			settings::stealth::hide_console = read_bool(misc_section, "stealth_hide_console", settings::stealth::hide_console);
			settings::stealth::name_threads = read_bool(misc_section, "stealth_name_threads", settings::stealth::name_threads);
			settings::stealth::panic_key = read_int(misc_section, "stealth_panic_key", settings::stealth::panic_key);
			settings::misc::bot_support = read_bool(misc_section, "bot_support", settings::misc::bot_support);
			settings::misc::only_workspace_bots = read_bool(misc_section, "only_workspace_bots", settings::misc::only_workspace_bots);
			settings::misc::disable_scrollbars = read_bool(misc_section, "disable_scrollbars", settings::misc::disable_scrollbars);
			settings::misc::preview_model = read_int(misc_section, "preview_model", settings::misc::preview_model);
			std::string loaded_cmp = read_string(misc_section, "custom_model_path", settings::misc::custom_model_path);
			strncpy_s(settings::misc::custom_model_path, loaded_cmp.c_str(), sizeof(settings::misc::custom_model_path) - 1);
			settings::misc::sticky_preview = read_bool(misc_section, "sticky_preview", settings::misc::sticky_preview);
			settings::general::confirm_unload = read_bool(misc_section, "confirm_unload", settings::general::confirm_unload);
			settings::misc::theme_index = read_int(misc_section, "theme_index", settings::misc::theme_index);
			settings::misc::custom_colors_enabled = read_bool(misc_section, "custom_colors_enabled", settings::misc::custom_colors_enabled);
			for (int i = 0; i < 14; i++)
				read_color(misc_section, ("custom_color_" + std::to_string(i)).c_str(), settings::misc::custom_colors[i]);

			if (misc_section.find("theme_preset") != std::string::npos)
			{
			}
			if (misc_section.find("theme_menu_accent") != std::string::npos)
			{
			}
			if (misc_section.find("theme_background_one") != std::string::npos)
			{
			}
			if (misc_section.find("theme_background_two") != std::string::npos)
			{
			}
			if (misc_section.find("theme_stroke") != std::string::npos)
			{
			}
			if (misc_section.find("theme_stroke_two") != std::string::npos)
			{
			}
			if (misc_section.find("theme_text") != std::string::npos)
			{
			}
			if (misc_section.find("theme_text_inactive") != std::string::npos)
			{
			}
		}

		std::string notifications_section = extract_section(json, "notifications");
		if (!notifications_section.empty())
		{








		}

		std::string btools_section = extract_section(json, "btools");
		if (!btools_section.empty())
		{
		}

		if (!lighting_section.empty())
		{
			if (!ambient_section.empty())
			{
			}
			if (!shadows_section.empty())
			{
			}
			if (!fog_section.empty())
			{
			}
			if (!clocktime_section.empty())
			{
			}
			if (!atmosphere_section.empty())
			{
			}
			if (!colorshift_section.empty())
			{
			}
			if (!exposure_section.empty())
			{
			}
			if (!starcount_section.empty())
			{
			}
			if (!sunmoon_section.empty())
			{
			}
			if (!brightness_section.empty())
			{
			}
			if (!environment_section.empty())
			{
			}
			if (!latitude_section.empty())
			{
			}
			if (!terrain_section.empty())
			{
			}
			if (!bloom_section.empty())
			{
			}
			if (!sunrays_section.empty())
			{
			}
			if (!color_correction_section.empty())
			{
			}
			if (!depth_of_field_section.empty())
			{
			}
		}

		if (!freezeplayer_section.empty())
		{
		}

		if (!typingcheck_section.empty())
		{
			settings::typingcheck::enabled = read_bool(typingcheck_section, "enabled", settings::typingcheck::enabled);
		}

		if (!config_system_section.empty())
		{
		}

		

		if (!skinchanger_section.empty())
		{

			std::vector<std::string> loaded_favs = read_string_vector(skinchanger_section, "favorites");
			{
				std::lock_guard<std::mutex> lock(cache::mtx);
			}
		}

		if (!dh_skinchanger_section.empty())
		{
		}

		std::string skyboxchanger_section = extract_section(json, "skyboxchanger");
		if (!skyboxchanger_section.empty())
		{
		}

		std::string win_section = extract_section(json, "windows");
		if (!win_section.empty())
		{
			float mx = read_float(win_section, "menu_pos_x", -1.f);
			float my = read_float(win_section, "menu_pos_y", -1.f);
			float mw = read_float(win_section, "menu_size_w", -1.f);
			float mh = read_float(win_section, "menu_size_h", -1.f);
			if (mx >= 0.f && my >= 0.f) g_saved_main_menu_pos = ImVec2(mx, my);
			if (mw > 0.f && mh > 0.f) {
				g_saved_main_menu_size = ImVec2(mw, mh);
				g_main_menu_size = ImVec2(mw, mh);
			}

			float sx = read_float(win_section, "settings_pos_x", -1.f);
			float sy = read_float(win_section, "settings_pos_y", -1.f);
			float sw = read_float(win_section, "settings_size_w", -1.f);
			float sh = read_float(win_section, "settings_size_h", -1.f);
			if (sx >= 0.f && sy >= 0.f) g_saved_settings_pos = ImVec2(sx, sy);
			if (sw > 0.f && sh > 0.f) g_saved_settings_size = ImVec2(sw, sh);

			float lx = read_float(win_section, "luau_pos_x", -1.f);
			float ly = read_float(win_section, "luau_pos_y", -1.f);
			float lw = read_float(win_section, "luau_size_w", -1.f);
			float lh = read_float(win_section, "luau_size_h", -1.f);
			if (lx >= 0.f && ly >= 0.f) g_saved_luau_pos = ImVec2(lx, ly);
			if (lw > 0.f && lh > 0.f) g_saved_luau_size = ImVec2(lw, lh);

			float px = read_float(win_section, "preview_pos_x", -1.f);
			float py = read_float(win_section, "preview_pos_y", -1.f);
			float pw = read_float(win_section, "preview_size_w", -1.f);
			float ph = read_float(win_section, "preview_size_h", -1.f);
			if (px >= 0.f && py >= 0.f) g_saved_preview_pos = ImVec2(px, py);
			if (pw > 0.f && ph > 0.f) g_saved_preview_size = ImVec2(pw, ph);

			float pfx = read_float(win_section, "perf_pos_x", -1.f);
			float pfy = read_float(win_section, "perf_pos_y", -1.f);
			float pfw = read_float(win_section, "perf_size_w", -1.f);
			float pfh = read_float(win_section, "perf_size_h", -1.f);
			if (pfx >= 0.f && pfy >= 0.f) g_saved_perf_pos = ImVec2(pfx, pfy);
			if (pfw > 0.f && pfh > 0.f) g_saved_perf_size = ImVec2(pfw, pfh);

			float spx = read_float(win_section, "spotify_pos_x", -1.f);
			float spy = read_float(win_section, "spotify_pos_y", -1.f);
			float spw = read_float(win_section, "spotify_size_w", -1.f);
			float sph = read_float(win_section, "spotify_size_h", -1.f);
			if (spx >= 0.f && spy >= 0.f) g_saved_spotify_pos = ImVec2(spx, spy);
			if (spw > 0.f && sph > 0.f) g_saved_spotify_size = ImVec2(spw, sph);

			float exx = read_float(win_section, "explorer_pos_x", -1.f);
			float exy = read_float(win_section, "explorer_pos_y", -1.f);
			float exw = read_float(win_section, "explorer_size_w", -1.f);
			float exh = read_float(win_section, "explorer_size_h", -1.f);
			if (exx >= 0.f && exy >= 0.f) g_saved_explorer_pos = ImVec2(exx, exy);
			if (exw > 0.f && exh > 0.f) g_saved_explorer_size = ImVec2(exw, exh);
		}


		return true;
	}

	bool delete_config(const std::string& name)
	{
		std::string path = get_config_path(name);
		if (path.empty() || !std::filesystem::exists(path))
			return false;

		try
		{
			return std::filesystem::remove(path);
		}
		catch (...)
		{
			return false;
		}
	}

	bool config_exists(const std::string& name)
	{
		std::string path = get_config_path(name);
		return !path.empty() && std::filesystem::exists(path);
	}

	// =========================================================================
	// memory-only config store
	//
	// Configs live in RAM under randomized names - the scanner's #1 catch is
	// a cheat config ever existing as a file on an NTFS volume (USN name
	// rules, Amcache, prefetch, OpenSave MRU), and every cleanup primitive
	// has its own detector. So: nothing is ever written, nothing is ever
	// deleted - there is no disk artifact to find or to wipe.
	// =========================================================================
		// wipe a string's bytes in place (volatile writes the optimizer can't skip)
	static void zero_string(std::string& s)
	{
		volatile char* p = s.data();
		for (size_t i = 0; i < s.size(); i++)
			p[i] = 0;
	}
	namespace memstore
	{
		static std::mutex mu;
		static std::unordered_map<std::string, std::string> configs;

		// registry-backed persistence: configs survive reruns without ever
		// touching the filesystem - registry values are invisible to USN /
		// Amcache / prefetch file scanning, and the key name matches the
		// host exe so it reads as legitimate application state
		inline const char* reg_path = "Software\\RiotClientCrashHandler";
		inline bool reg_loaded = false;

		static std::string name_to_hex(const std::string& s)
		{
			static const char* hd = "0123456789ABCDEF";
			std::string o;
			for (unsigned char c : s) { o += hd[c >> 4]; o += hd[c & 15]; }
			return o;
		}
		static std::string hex_to_name(const std::string& h)
		{
			auto d = [](char c) -> int { if (c >= '0' && c <= '9') return c - '0'; if (c >= 'A' && c <= 'F') return c - 'A' + 10; if (c >= 'a' && c <= 'f') return c - 'a' + 10; return 0; };
			std::string o;
			for (size_t i = 0; i + 1 < h.size(); i += 2)
				o += (char)((d(h[i]) << 4) | d(h[i + 1]));
			return o;
		}

		static void reg_load()
		{
			if (reg_loaded)
				return;
			reg_loaded = true;
		HKEY h;
		if (RegOpenKeyExA(HKEY_CURRENT_USER, reg_path, 0, KEY_READ, &h) != ERROR_SUCCESS)
			return;
		// 64KB value buffer: real configs run ~12KB+, and a value that still
		// doesn't fit is skipped without aborting the rest of the scan
		// (the old 16KB buffer silently dropped those configs forever)
		DWORD idx = 0;
		char name[256];
		DWORD name_len = 0;
		std::vector<BYTE> data;
		data.resize(65536);
		DWORD data_len = 0;
		DWORD type = 0;
		for (;;)
		{
			name_len = 256;
			data_len = (DWORD)data.size();
			type = 0;
			LONG rc = RegEnumValueA(h, idx, name, &name_len, nullptr, &type, data.data(), &data_len);
			if (rc == ERROR_NO_MORE_ITEMS)
				break;
			if (rc == ERROR_MORE_DATA)
			{
				if (data.size() < 262144) { data.resize(data.size() * 2); continue; }
				idx++;
				continue;
			}
			if (rc != ERROR_SUCCESS)
				break;
			idx++;
			if (type == REG_SZ && data_len > 0)
			{
				std::string blob((char*)data.data(), data_len);
				while (!blob.empty() && blob.back() == '\0')
					blob.pop_back();
				std::string key = hex_to_name(name);
				if (!blob.empty() && !key.empty())
					configs[key] = blob;
			}
		}
		RegCloseKey(h);
		}

		// registry value names must be conservative chars - config keys contain
		// specials (!@#$ etc.) which RegSetValueExA can silently reject, so
		// keys are stored hex-encoded (reversible, [0-9A-F] only)
		static void reg_write(const std::string& name, const std::string& blob)
		{
			HKEY h;
			if (RegCreateKeyExA(HKEY_CURRENT_USER, reg_path, 0, nullptr, 0, KEY_WRITE, nullptr, &h, nullptr) != ERROR_SUCCESS)
				return;
			const std::string vn = name_to_hex(name);
			RegSetValueExA(h, vn.c_str(), 0, REG_SZ, (const BYTE*)blob.data(), (DWORD)(blob.size() + 1));
			RegCloseKey(h);
		}

		static void reg_delete(const std::string& name)
		{
			HKEY h;
			if (RegOpenKeyExA(HKEY_CURRENT_USER, reg_path, 0, KEY_WRITE, &h) != ERROR_SUCCESS)
				return;
			const std::string vn = name_to_hex(name);
			RegDeleteValueA(h, vn.c_str());
			RegCloseKey(h);
		}

		static void reg_wipe()
		{
			HKEY h;
			if (RegOpenKeyExA(HKEY_CURRENT_USER, reg_path, 0, KEY_READ | KEY_WRITE, &h) != ERROR_SUCCESS)
				return;
			char name[128];
			DWORD name_len = 128;
			// values shift left after each delete - always re-enumerate at 0
			while (RegEnumValueA(h, 0, name, &name_len, nullptr, nullptr, nullptr, nullptr) == ERROR_SUCCESS)
			{
				RegDeleteValueA(h, name);
				name_len = 128;
			}
			RegCloseKey(h);
		}
	}

	std::vector<std::string> get_config_list_mem()
	{
		memstore::reg_load();
		std::lock_guard<std::mutex> lock(memstore::mu);
		std::vector<std::string> out;
		out.reserve(memstore::configs.size());
		for (const auto& [k, v] : memstore::configs)
			out.push_back(k);
		return out;
	}

	bool config_exists_mem(const std::string& name)
	{
		memstore::reg_load();
		std::lock_guard<std::mutex> lock(memstore::mu);
		return memstore::configs.count(name) != 0;
	}

	bool save_config_mem(const std::string& name)
	{
		const std::string blob = serialize_settings();
		if (blob.empty())
			return false;
		{
			std::lock_guard<std::mutex> lock(memstore::mu);
			memstore::configs[name] = blob;
		}
		memstore::reg_write(name, blob);
		return true;
	}

	bool load_config_mem(const std::string& name)
	{
		memstore::reg_load();
		std::string blob;
		{
			std::lock_guard<std::mutex> lock(memstore::mu);
			auto it = memstore::configs.find(name);
			if (it == memstore::configs.end())
				return false;
			blob = it->second;
		}
		return deserialize_settings(blob);
	}

	bool delete_config_mem(const std::string& name)
	{
		memstore::reg_load();
		std::lock_guard<std::mutex> lock(memstore::mu);
		auto it = memstore::configs.find(name);
		if (it == memstore::configs.end())
			return false;
		// wipe the blob contents before unlinking so freed heap doesn't
		// carry the config JSON
		std::string& blob = it->second;
		zero_string(blob);
		memstore::configs.erase(it);
		memstore::reg_delete(name);
		return true;
	}

	// randomized config key: letters, digits and specials (never a name that
	// means anything to a scanner)
	std::string generate_config_name()
	{
		static const char alphabet[] =
			"abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789!@#$%^&*()-_=+[]{};:,.?/~";
		std::random_device rd;
		std::mt19937 gen(rd());
		std::uniform_int_distribution<int> dist(0, (int)(sizeof(alphabet) - 2));
		std::uniform_int_distribution<int> len_dist(12, 20);
		const int len = len_dist(gen);
		std::string out;
		out.reserve(len);
		for (int i = 0; i < len; i++)
			out += alphabet[dist(gen)];
		return out;
	}

	void wipe_config_mem()
	{
		memstore::reg_wipe();
		std::lock_guard<std::mutex> lock(memstore::mu);
		for (auto& [k, v] : memstore::configs)
		{
			zero_string(const_cast<std::string&>(k));
			zero_string(v);
		}
		memstore::configs.clear();
	}

	static std::string get_autoload_path()
	{
		std::string dir = get_config_directory();
		if (dir.empty()) return "";
		return dir + "\\autoload.txt";
	}

	bool set_autoload(const std::string& name)
	{
		if (!ensure_config_directory()) return false;
		std::string path = get_autoload_path();
		if (path.empty()) return false;
		std::ofstream f(path, std::ios::trunc);
		if (!f.is_open()) return false;
		f << name;
		return true;
	}

	std::string get_autoload()
	{
		// Read once per second max â€” the settings tab queries this every frame.
		static std::mutex cache_mu;
		static std::string cached;
		static std::chrono::steady_clock::time_point last_scan{};
		static bool has_scanned = false;
		{
			std::lock_guard<std::mutex> lk(cache_mu);
			auto now = std::chrono::steady_clock::now();
			if (has_scanned && (now - last_scan) < std::chrono::milliseconds(1000))
				return cached;
			last_scan = now;
		}

		std::string result;
		std::string path = get_autoload_path();
		if (!path.empty() && std::filesystem::exists(path))
		{
			std::ifstream f(path);
			if (f.is_open())
				std::getline(f, result);
		}

		std::lock_guard<std::mutex> lk(cache_mu);
		cached = result;
		has_scanned = true;
		return cached;
	}

	void clear_autoload()
	{
		std::string path = get_autoload_path();
		if (!path.empty() && std::filesystem::exists(path))
			std::filesystem::remove(path);
	}

	bool load_autoload()
	{
		std::string name = get_autoload();
		if (name.empty()) return false;
		if (!config_exists(name)) return false;
		return load_config(name);
	}

	// ---- registry-backed autoload (no disk) ----
	// The disk autoload.txt path is deliberately unused: no files may persist.
	// The value name is hex-encoded "autoload" so it blends with the memstore
	// config value names.
	static const char* k_autoload_value = "6175746f6c6f6164";

	bool set_autoload_mem(const std::string& name)
	{
		if (name.empty()) return false;
		HKEY h;
		if (RegCreateKeyExA(HKEY_CURRENT_USER, memstore::reg_path, 0, nullptr, 0, KEY_WRITE, nullptr, &h, nullptr) != ERROR_SUCCESS)
			return false;
		const bool ok = RegSetValueExA(h, k_autoload_value, 0, REG_SZ, (const BYTE*)name.c_str(), (DWORD)name.size() + 1) == ERROR_SUCCESS;
		RegCloseKey(h);
		return ok;
	}

	std::string get_autoload_mem()
	{
		HKEY h;
		if (RegOpenKeyExA(HKEY_CURRENT_USER, memstore::reg_path, 0, KEY_READ, &h) != ERROR_SUCCESS)
			return {};
		char buf[128] = {};
		DWORD sz = sizeof(buf) - 1, t = 0;
		const bool ok = RegQueryValueExA(h, k_autoload_value, nullptr, &t, (BYTE*)buf, &sz) == ERROR_SUCCESS && t == REG_SZ;
		RegCloseKey(h);
		return ok ? std::string(buf) : std::string();
	}

	void clear_autoload_mem()
	{
		HKEY h;
		if (RegOpenKeyExA(HKEY_CURRENT_USER, memstore::reg_path, 0, KEY_WRITE, &h) == ERROR_SUCCESS)
		{
			RegDeleteValueA(h, k_autoload_value);
			RegCloseKey(h);
		}
	}

	bool load_autoload_mem()
	{
		const std::string name = get_autoload_mem();
		if (name.empty()) return false;
		if (!config_exists_mem(name)) return false;
		return load_config_mem(name);
	}

	void wipe_traces()
	{
		// zero the in-memory blobs too so freed heap doesn't carry config json
		wipe_config_mem();

		HKEY h;
		if (RegOpenKeyExA(HKEY_CURRENT_USER, memstore::reg_path, 0, KEY_READ | KEY_WRITE, &h) != ERROR_SUCCESS)
			return;
		for (DWORD i = 0;;)
		{
			char name[256];
			DWORD name_len = sizeof(name);
			const LSTATUS s = RegEnumValueA(h, i, name, &name_len, nullptr, nullptr, nullptr, nullptr);
			if (s == ERROR_NO_MORE_ITEMS) break;
			if (s != ERROR_SUCCESS) { ++i; continue; }
			// overwriting with zeros before deleting: a deleted registry cell
			// stays recoverable from hive slack by forensic tools otherwise
			DWORD size = 0, type = 0;
			if (RegQueryValueExA(h, name, nullptr, &type, nullptr, &size) == ERROR_SUCCESS && size > 0 && size < 4096)
			{
				std::vector<BYTE> zeros(size, 0);
				RegSetValueExA(h, name, 0, type, zeros.data(), size);
			}
			RegDeleteValueA(h, name);
			// the next value shifted into this index - don't advance i
		}
		RegCloseKey(h);
		RegDeleteKeyA(HKEY_CURRENT_USER, memstore::reg_path);
	}

	bool save_theme()
	{
		if (!ensure_config_directory()) return false;
		std::string path = get_config_directory() + "\\theme.json";
		if (path.empty()) return false;

		ImGuiStyle& style = ImGui::GetStyle();
		std::ostringstream ss;
		ss << "{\n";
		write_color(ss, "WindowBg",          &style.Colors[ImGuiCol_WindowBg].x);
		write_color(ss, "ChildBg",           &style.Colors[ImGuiCol_ChildBg].x);
		write_color(ss, "Header",            &style.Colors[ImGuiCol_Header].x);
		write_color(ss, "PopupBg",           &style.Colors[ImGuiCol_PopupBg].x);
		write_color(ss, "Text",              &style.Colors[ImGuiCol_Text].x);
		write_color(ss, "TextDisabled",      &style.Colors[ImGuiCol_TextDisabled].x);
		write_color(ss, "SliderGrab",        &style.Colors[ImGuiCol_SliderGrab].x);
		write_color(ss, "SliderGrabActive",  &style.Colors[ImGuiCol_SliderGrabActive].x);
		write_color(ss, "Border",            &style.Colors[ImGuiCol_Border].x);
		write_color(ss, "Button",            &style.Colors[ImGuiCol_Button].x);
		write_color(ss, "ButtonHovered",     &style.Colors[ImGuiCol_ButtonHovered].x);
		write_color(ss, "ButtonActive",      &style.Colors[ImGuiCol_ButtonActive].x);
		write_color(ss, "FrameBg",           &style.Colors[ImGuiCol_FrameBg].x);
		write_color(ss, "FrameBgHovered",    &style.Colors[ImGuiCol_FrameBgHovered].x);
		write_color(ss, "FrameBgActive",     &style.Colors[ImGuiCol_FrameBgActive].x);
		write_color(ss, "ScrollbarBg",       &style.Colors[ImGuiCol_ScrollbarBg].x);
		write_color(ss, "ScrollbarGrab",     &style.Colors[ImGuiCol_ScrollbarGrab].x);
		ss << "}\n";

		return write_file_atomic(path, ss.str());
	}

	bool load_theme()
	{
		std::string path = get_config_directory() + "\\theme.json";
		if (path.empty() || !std::filesystem::exists(path)) return false;

		std::ifstream file(path);
		if (!file.is_open()) return false;
		std::string json((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
		file.close();

		ImGuiStyle& style = ImGui::GetStyle();
		read_color(json, "WindowBg",         &style.Colors[ImGuiCol_WindowBg].x);
		read_color(json, "ChildBg",          &style.Colors[ImGuiCol_ChildBg].x);
		read_color(json, "Header",           &style.Colors[ImGuiCol_Header].x);
		read_color(json, "PopupBg",          &style.Colors[ImGuiCol_PopupBg].x);
		read_color(json, "Text",             &style.Colors[ImGuiCol_Text].x);
		read_color(json, "TextDisabled",     &style.Colors[ImGuiCol_TextDisabled].x);
		read_color(json, "SliderGrab",       &style.Colors[ImGuiCol_SliderGrab].x);
		read_color(json, "SliderGrabActive", &style.Colors[ImGuiCol_SliderGrabActive].x);
		read_color(json, "Border",           &style.Colors[ImGuiCol_Border].x);
		read_color(json, "Button",           &style.Colors[ImGuiCol_Button].x);
		read_color(json, "ButtonHovered",    &style.Colors[ImGuiCol_ButtonHovered].x);
		read_color(json, "ButtonActive",     &style.Colors[ImGuiCol_ButtonActive].x);
		read_color(json, "FrameBg",          &style.Colors[ImGuiCol_FrameBg].x);
		read_color(json, "FrameBgHovered",   &style.Colors[ImGuiCol_FrameBgHovered].x);
		read_color(json, "FrameBgActive",    &style.Colors[ImGuiCol_FrameBgActive].x);
		read_color(json, "ScrollbarBg",      &style.Colors[ImGuiCol_ScrollbarBg].x);
		read_color(json, "ScrollbarGrab",    &style.Colors[ImGuiCol_ScrollbarGrab].x);

		style.Colors[ImGuiCol_ScrollbarGrabHovered] = style.Colors[ImGuiCol_ScrollbarGrab];
		style.Colors[ImGuiCol_ScrollbarGrabActive]  = style.Colors[ImGuiCol_ScrollbarGrab];
		style.Colors[ImGuiCol_Separator]             = style.Colors[ImGuiCol_Border];
		style.Colors[ImGuiCol_CheckMark]             = style.Colors[ImGuiCol_ChildBg];
		style.Colors[ImGuiCol_FrameBgHovered]        = style.Colors[ImGuiCol_ButtonHovered];
		style.Colors[ImGuiCol_FrameBgActive]         = style.Colors[ImGuiCol_ButtonActive];
		return true;
	}

	std::string get_theme_directory()
	{
		return "C:\\Ivory\\Themes";
	}

	bool ensure_theme_directory()
	{
		std::string dir = get_theme_directory();
		try
		{
			std::filesystem::create_directories(dir);
			return true;
		}
		catch (...)
		{
			return false;
		}
	}

	std::vector<std::string> get_theme_list()
	{
		// Cached with a short TTL â€” the theme tab iterates this every frame.
		static std::mutex cache_mu;
		static std::vector<std::string> cached;
		static std::chrono::steady_clock::time_point last_scan{};
		static bool has_scanned = false;
		{
			std::lock_guard<std::mutex> lk(cache_mu);
			auto now = std::chrono::steady_clock::now();
			if (has_scanned && (now - last_scan) < std::chrono::milliseconds(1000))
				return cached;
			last_scan = now;
		}

		std::vector<std::string> themes;
		std::string dir = get_theme_directory();
		if (!dir.empty() && std::filesystem::exists(dir))
		{
			try
			{
				for (const auto& entry : std::filesystem::directory_iterator(dir))
				{
					if (entry.is_regular_file() && entry.path().extension() == ".json")
					{
						std::string stem = entry.path().stem().string();
						if (stem != "autoload_theme")
						{
							themes.push_back(stem);
						}
					}
				}
			}
			catch (...)
			{
			}
		}

		std::lock_guard<std::mutex> lk(cache_mu);
		cached = themes;
		has_scanned = true;
		return cached;
	}

	bool save_theme_custom(const std::string& name)
	{
		if (name.empty()) return false;
		ensure_theme_directory();
		std::string filename = name;
		if (filename.find(".json") == std::string::npos)
			filename += ".json";

		std::string path = get_theme_directory() + "\\" + filename;
		std::ostringstream ss;
		ss << "{\n";

		write_color(ss, "menu_color", orok_config.menu_color);
		write_color(ss, "menu_color_secondary", orok_config.menu_color_secondary);
		write_color(ss, "menu_color_text", orok_config.menu_color_text);
		write_color(ss, "menu_color_border", orok_config.menu_color_border);
		write_float(ss, "menu_window_rounding", orok_config.menu_window_rounding);
		write_float(ss, "menu_frame_rounding", orok_config.menu_frame_rounding);
		write_float(ss, "menu_scrollbar_size", orok_config.menu_scrollbar_size);
		write_float(ss, "menu_size_width", orok_config.menu_size[0]);
		write_float(ss, "menu_size_height", orok_config.menu_size[1]);
		write_bool(ss, "watermark", settings::misc::watermark);
		write_bool(ss, "keybind_list", settings::misc::keybind_list);
		write_float(ss, "watermark_pos_x", settings::misc::watermark_pos_x);
		write_float(ss, "watermark_pos_y", settings::misc::watermark_pos_y);
		write_float(ss, "keybind_list_pos_x", settings::misc::keybind_list_pos_x);
		write_float(ss, "keybind_list_pos_y", settings::misc::keybind_list_pos_y);
		write_int(ss, "theme_index", settings::misc::theme_index);
		write_bool(ss, "custom_colors_enabled", settings::misc::custom_colors_enabled);
		for (int i = 0; i < 14; i++)
		{
			std::string key = "custom_color_" + std::to_string(i);
			write_color(ss, key.c_str(), settings::misc::custom_colors[i]);
		}
		ss << "}\n";

		return write_file_atomic(path, ss.str());
	}

	bool load_theme_custom(const std::string& name)
	{
		if (name.empty()) return false;
		ensure_theme_directory();
		std::string filename = name;
		if (filename.find(".json") == std::string::npos)
			filename += ".json";

		std::string path = get_theme_directory() + "\\" + filename;
		if (!std::filesystem::exists(path)) return false;

		std::ifstream file(path);
		if (!file.is_open()) return false;
		std::string json((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
		file.close();



		read_color(json, "menu_color", orok_config.menu_color);
		read_color(json, "menu_color_secondary", orok_config.menu_color_secondary);
		read_color(json, "menu_color_text", orok_config.menu_color_text);
		read_color(json, "menu_color_border", orok_config.menu_color_border);
		orok_config.menu_window_rounding = read_float(json, "menu_window_rounding", 0.0f);
		orok_config.menu_frame_rounding = read_float(json, "menu_frame_rounding", 0.0f);
		orok_config.menu_scrollbar_size = read_float(json, "menu_scrollbar_size", 6.0f);
		orok_config.menu_size[0] = read_float(json, "menu_size_width", 516.0f);
		orok_config.menu_size[1] = read_float(json, "menu_size_height", 563.0f);
		settings::misc::watermark = read_bool(json, "watermark", settings::misc::watermark);
		settings::misc::keybind_list = read_bool(json, "keybind_list", settings::misc::keybind_list);
		settings::misc::watermark_pos_x = read_float(json, "watermark_pos_x", settings::misc::watermark_pos_x);
		settings::misc::watermark_pos_y = read_float(json, "watermark_pos_y", settings::misc::watermark_pos_y);
		settings::misc::keybind_list_pos_x = read_float(json, "keybind_list_pos_x", settings::misc::keybind_list_pos_x);
		settings::misc::keybind_list_pos_y = read_float(json, "keybind_list_pos_y", settings::misc::keybind_list_pos_y);
		settings::misc::theme_index = read_int(json, "theme_index", settings::misc::theme_index);
		settings::misc::custom_colors_enabled = read_bool(json, "custom_colors_enabled", settings::misc::custom_colors_enabled);
		for (int i = 0; i < 14; i++)
		{
			std::string key = "custom_color_" + std::to_string(i);
			read_color(json, key.c_str(), settings::misc::custom_colors[i]);
		}

		return true;
	}

	bool delete_theme_custom(const std::string& name)
	{
		if (name.empty()) return false;
		std::string filename = name;
		if (filename.find(".json") == std::string::npos)
			filename += ".json";

		std::string path = get_theme_directory() + "\\" + filename;
		if (std::filesystem::exists(path))
		{
			return std::filesystem::remove(path);
		}
		return false;
	}

	bool set_theme_autoload(const std::string& name)
	{
		ensure_theme_directory();
		std::string path = get_theme_directory() + "\\autoload_theme.txt";
		return write_file_atomic(path, name);
	}

	std::string get_theme_autoload()
	{
		std::string path = get_theme_directory() + "\\autoload_theme.txt";
		if (!std::filesystem::exists(path)) return "";
		std::ifstream file(path);
		if (!file.is_open()) return "";
		std::string name;
		file >> name;
		file.close();
		return name;
	}

	void clear_theme_autoload()
	{
		std::string path = get_theme_directory() + "\\autoload_theme.txt";
		if (std::filesystem::exists(path))
		{
			std::filesystem::remove(path);
		}
	}

	bool auto_load_theme()
	{
		ensure_theme_directory();
		std::string name = get_theme_autoload();
		if (!name.empty())
		{
			return load_theme_custom(name);
		}
		if (std::filesystem::exists(get_theme_directory() + "\\default.json"))
		{
			return load_theme_custom("default");
		}
		return false;
	}

	static const std::string b64_table = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

	static std::string base64_encode(const std::string& in)
	{
		std::string out;
		int val = 0, valb = -6;
		for (unsigned char c : in)
		{
			val = (val << 8) + c;
			valb += 8;
			while (valb >= 0)
			{
				out.push_back(b64_table[(val >> valb) & 0x3F]);
				valb -= 6;
			}
		}
		if (valb > -6) out.push_back(b64_table[((val << 8) >> (valb + 8)) & 0x3F]);
		while (out.size() % 4) out.push_back('=');
		return out;
	}

	static std::string base64_decode(const std::string& in)
	{
		std::string out;
		std::vector<int> T(256, -1);
		for (int i = 0; i < 64; i++) T[b64_table[i]] = i;
		int val = 0, valb = -8;
		for (unsigned char c : in)
		{
			if (T[c] == -1) break;
			val = (val << 6) + T[c];
			valb += 6;
			if (valb >= 0)
			{
				out.push_back(char((val >> valb) & 0xFF));
				valb -= 8;
			}
		}
		return out;
	}

	static bool copy_to_clipboard(const std::string& text)
	{
		if (!OpenClipboard(nullptr)) return false;
		EmptyClipboard();
		HGLOBAL hGlob = GlobalAlloc(GMEM_MOVEABLE, text.size() + 1);
		if (!hGlob) { CloseClipboard(); return false; }
		char* pBuf = static_cast<char*>(GlobalLock(hGlob));
		if (pBuf)
		{
			memcpy(pBuf, text.c_str(), text.size() + 1);
			GlobalUnlock(hGlob);
			SetClipboardData(CF_TEXT, hGlob);
		}
		CloseClipboard();
		return true;
	}

	static std::string read_from_clipboard()
	{
		if (!OpenClipboard(nullptr)) return "";
		HANDLE hData = GetClipboardData(CF_TEXT);
		if (!hData) { CloseClipboard(); return ""; }
		char* pBuf = static_cast<char*>(GlobalLock(hData));
		std::string text = pBuf ? pBuf : "";
		GlobalUnlock(hData);
		CloseClipboard();
		return text;
	}

	bool export_config_to_clipboard()
	{
		// pure memory -> clipboard: the old path wrote AND deleted a temp
		// file, which is two forensic artifacts for one action
		const std::string json = serialize_settings();
		if (json.empty())
			return false;
		return copy_to_clipboard(base64_encode(json));
	}

	bool import_config_from_clipboard()
	{
		std::string clipboard = read_from_clipboard();
		if (clipboard.empty()) return false;

		std::string encoded = clipboard;
		size_t pos = clipboard.find("Ivory_CFG_CODE:");
		if (pos != std::string::npos)
		{
			encoded = clipboard.substr(pos + 20);
		}

		while (!encoded.empty() && (encoded.back() == '\r' || encoded.back() == '\n' || encoded.back() == ' ' || encoded.back() == '\t'))
			encoded.pop_back();

		std::string json = base64_decode(encoded);
		if (json.empty() || json.find("{") == std::string::npos) return false;

		// straight into the memory store under a random key - no file, ever
		std::string name = generate_config_name();
		{
			std::lock_guard<std::mutex> lock(memstore::mu);
			memstore::configs[name] = json;
		}
		return load_config_mem(name);
	}

	// cloud configs (paste.rs - no accounts, no secrets, plain https).
	// share uploads the base64 blob and returns the paste url; fetch pulls a
	// paste id/url back down. network always runs on a worker thread - the
	// caller drains results on the render thread (see gui cloud slots).
	static bool http_status_ok(HINTERNET req)
	{
		DWORD code = 0, len = sizeof(code);
		if (!HttpQueryInfoA(req, HTTP_QUERY_STATUS_CODE | HTTP_QUERY_FLAG_NUMBER, &code, &len, nullptr))
			return false;
		return code == 200;
	}

	static bool http_read_all(HINTERNET req, std::string& out, size_t cap)
	{
		out.clear();
		char buf[8192];
		DWORD got = 0;
		while (InternetReadFile(req, buf, sizeof(buf), &got) && got > 0)
		{
			if (out.size() + got > cap)
				return false;
			out.append(buf, got);
		}
		return true;
	}

	static bool http_post_paste(const std::string& body, std::string& out_resp)
	{
		out_resp.clear();
		if (body.empty() || body.size() > 1048576)
			return false;
		HINTERNET net = InternetOpenA("Mozilla/5.0 (Windows NT 10.0; Win64; x64)", INTERNET_OPEN_TYPE_PRECONFIG, nullptr, nullptr, 0);
		if (!net) return false;
		bool ok = false;
		HINTERNET conn = InternetConnectA(net, "paste.rs", INTERNET_DEFAULT_HTTPS_PORT, nullptr, nullptr, INTERNET_SERVICE_HTTP, 0, 0);
		if (conn)
		{
			HINTERNET req = HttpOpenRequestA(conn, "POST", "/", nullptr, nullptr, nullptr, INTERNET_FLAG_SECURE | INTERNET_FLAG_RELOAD | INTERNET_FLAG_NO_CACHE_WRITE, 0);
			if (req)
			{
				DWORD to = 10000;
				InternetSetOptionA(req, INTERNET_OPTION_SEND_TIMEOUT, &to, sizeof(to));
				InternetSetOptionA(req, INTERNET_OPTION_RECEIVE_TIMEOUT, &to, sizeof(to));
				if (HttpSendRequestA(req, nullptr, 0, (LPVOID)body.data(), (DWORD)body.size()) && http_status_ok(req))
					ok = http_read_all(req, out_resp, 4096);
				InternetCloseHandle(req);
			}
			InternetCloseHandle(conn);
		}
		InternetCloseHandle(net);
		return ok;
	}

	static bool http_get_paste(const std::string& id, std::string& out_body)
	{
		out_body.clear();
		HINTERNET net = InternetOpenA("Mozilla/5.0 (Windows NT 10.0; Win64; x64)", INTERNET_OPEN_TYPE_PRECONFIG, nullptr, nullptr, 0);
		if (!net) return false;
		bool ok = false;
		HINTERNET conn = InternetConnectA(net, "paste.rs", INTERNET_DEFAULT_HTTPS_PORT, nullptr, nullptr, INTERNET_SERVICE_HTTP, 0, 0);
		if (conn)
		{
			const std::string path = "/" + id;
			HINTERNET req = HttpOpenRequestA(conn, "GET", path.c_str(), nullptr, nullptr, nullptr, INTERNET_FLAG_SECURE | INTERNET_FLAG_RELOAD | INTERNET_FLAG_NO_CACHE_WRITE, 0);
			if (req)
			{
				DWORD to = 10000;
				InternetSetOptionA(req, INTERNET_OPTION_SEND_TIMEOUT, &to, sizeof(to));
				InternetSetOptionA(req, INTERNET_OPTION_RECEIVE_TIMEOUT, &to, sizeof(to));
				if (HttpSendRequestA(req, nullptr, 0, nullptr, 0) && http_status_ok(req))
					ok = http_read_all(req, out_body, 262144);
				InternetCloseHandle(req);
			}
			InternetCloseHandle(conn);
		}
		InternetCloseHandle(net);
		return ok;
	}

	static void trim_ws(std::string& s)
	{
		while (!s.empty() && (s.back() == '\r' || s.back() == '\n' || s.back() == ' ' || s.back() == '\t')) s.pop_back();
		while (!s.empty() && (s.front() == ' ' || s.front() == '\t')) s.erase(s.begin());
	}

	bool share_config_cloud(std::string& out_url)
	{
		out_url.clear();
		const std::string json = serialize_settings();
		if (json.empty()) return false;
		std::string resp;
		if (!http_post_paste(base64_encode(json), resp)) return false;
		trim_ws(resp);
		if (resp.compare(0, 17, "https://paste.rs/") != 0 || resp.size() > 64) return false;
		out_url = resp;
		return true;
	}

	bool fetch_config_cloud_body(const std::string& id_or_url, std::string& out_body)
	{
		out_body.clear();
		std::string id = id_or_url;
		trim_ws(id);
		for (const char* prefix : { "https://paste.rs/", "http://paste.rs/", "paste.rs/" })
		{
			const size_t plen = strlen(prefix);
			if (id.compare(0, plen, prefix) == 0) { id.erase(0, plen); break; }
		}
		while (!id.empty() && id.back() == '/') id.pop_back();
		if (id.empty() || id.size() > 64) return false;
		for (char c : id)
			if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '-' || c == '_'))
				return false;
		return http_get_paste(id, out_body);
	}

	bool apply_fetched_config(const std::string& body)
	{
		std::string b = body;
		trim_ws(b);
		if (b.empty()) return false;
		// base64 blob from share, or raw json - but never apply blindly:
		// require a known section so garbage can only ever be rejected,
		// never wipe live settings (deserialize resets to defaults first)
		std::string json = base64_decode(b);
		if (json.find("\"aimbot\"") == std::string::npos)
			json = b;
		if (json.find("\"aimbot\"") == std::string::npos || json.find('{') == std::string::npos)
			return false;
		std::string name = generate_config_name();
		{
			std::lock_guard<std::mutex> lock(memstore::mu);
			memstore::configs[name] = json;
		}
		return load_config_mem(name);
	}

	bool export_theme_to_clipboard()
	{
		save_theme_custom("temp_theme_export");
		std::string path = get_theme_directory() + "\\temp_theme_export.json";
		if (!std::filesystem::exists(path)) return false;
		std::ifstream file(path);
		if (!file.is_open()) return false;
		std::string json((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
		file.close();
		delete_theme_custom("temp_theme_export");

		std::string encoded = base64_encode(json);
		return copy_to_clipboard(encoded);
	}

	bool import_theme_from_clipboard()
	{
		std::string clipboard = read_from_clipboard();
		if (clipboard.empty()) return false;

		if (clipboard.find("Menu Accent:") != std::string::npos || clipboard.find("Contrast One:") != std::string::npos)
		{
			auto parse_hex = [](const std::string& text, const std::string& key, float* out_rgb) {
				size_t p = text.find(key);
				if (p == std::string::npos) return;
				size_t hash_pos = text.find('#', p);
				if (hash_pos != std::string::npos && hash_pos + 6 < text.length())
				{
					std::string hex = text.substr(hash_pos + 1, 6);
					int r = 0, g = 0, b = 0;
					if (sscanf_s(hex.c_str(), "%02x%02x%02x", &r, &g, &b) == 3 || sscanf_s(hex.c_str(), "%02X%02X%02X", &r, &g, &b) == 3)
					{
						out_rgb[0] = r / 255.0f;
						out_rgb[1] = g / 255.0f;
						out_rgb[2] = b / 255.0f;
					}
				}
			};



			return true;
		}

		std::string encoded = clipboard;
		size_t pos = clipboard.find("Ivory_THEME_CODE:");
		if (pos != std::string::npos)
		{
			encoded = clipboard.substr(pos + 22);
		}

		while (!encoded.empty() && (encoded.back() == '\r' || encoded.back() == '\n' || encoded.back() == ' ' || encoded.back() == '\t'))
			encoded.pop_back();

		std::string json = base64_decode(encoded);
		if (json.empty() || json.find("{") == std::string::npos) return false;

		auto now = std::chrono::system_clock::now().time_since_epoch().count() % 10000;
		std::string name = "ImportedTheme_" + std::to_string(now);

		ensure_theme_directory();
		std::string path = get_theme_directory() + "\\" + name + ".json";
		if (!write_file_atomic(path, json))
			return false;

		return load_theme_custom(name);
	}

	std::string get_scripts_directory()
	{
		return "C:\\Ivory\\Scripts";
	}

	bool ensure_scripts_directory()
	{
		std::string dir = get_scripts_directory();
		try
		{
			std::filesystem::create_directories(dir);
			return true;
		}
		catch (...)
		{
			return false;
		}
	}

	std::vector<std::string> get_script_list()
	{
		ensure_scripts_directory();
		std::vector<std::string> list;
		std::string dir = get_scripts_directory();
		try
		{
			if (std::filesystem::exists(dir))
			{
				for (const auto& entry : std::filesystem::directory_iterator(dir))
				{
					if (entry.is_regular_file())
					{
						auto ext = entry.path().extension().string();
						if (ext == ".lua" || ext == ".luau" || ext == ".txt")
						{
							list.push_back(entry.path().stem().string());
						}
					}
				}
			}
		}
		catch (...) {}
		std::sort(list.begin(), list.end());
		return list;
	}

	bool save_script(const std::string& name, const std::string& content)
	{
		if (name.empty()) return false;
		ensure_scripts_directory();
		std::string path = get_scripts_directory() + "\\" + name + ".lua";
		try
		{
			std::ofstream f(path);
			if (!f.is_open()) return false;
			f << content;
			f.close();
			return true;
		}
		catch (...) { return false; }
	}

	std::string load_script(const std::string& name)
	{
		if (name.empty()) return "";
		ensure_scripts_directory();
		std::string path = get_scripts_directory() + "\\" + name + ".lua";
		if (!std::filesystem::exists(path))
		{
			path = get_scripts_directory() + "\\" + name + ".luau";
			if (!std::filesystem::exists(path))
			{
				path = get_scripts_directory() + "\\" + name + ".txt";
				if (!std::filesystem::exists(path)) return "";
			}
		}
		try
		{
			std::ifstream f(path);
			if (!f.is_open()) return "";
			std::stringstream buffer;
			buffer << f.rdbuf();
			return buffer.str();
		}
		catch (...) { return ""; }
	}

	bool delete_script(const std::string& name)
	{
		if (name.empty()) return false;
		ensure_scripts_directory();
		std::string path = get_scripts_directory() + "\\" + name + ".lua";
		try
		{
			if (std::filesystem::exists(path))
				return std::filesystem::remove(path);
			path = get_scripts_directory() + "\\" + name + ".luau";
			if (std::filesystem::exists(path))
				return std::filesystem::remove(path);
			path = get_scripts_directory() + "\\" + name + ".txt";
			if (std::filesystem::exists(path))
				return std::filesystem::remove(path);
			return false;
		}
		catch (...) { return false; }
	}

	std::string get_debug_directory()
	{
		return "C:\\Ivory\\Debug";
	}

	bool ensure_debug_directories()
	{
		try
		{
			std::string base = get_debug_directory();
			std::filesystem::create_directories(base);
			std::filesystem::create_directories(base + "\\Players");
			std::filesystem::create_directories(base + "\\Explorer");
			std::filesystem::create_directories(base + "\\Memory");
			std::filesystem::create_directories(base + "\\Bones");
			return true;
		}
		catch (...) { return false; }
	}

	static std::string get_timestamp_str()
	{
		auto now = std::chrono::system_clock::now();
		auto time_t = std::chrono::system_clock::to_time_t(now);
		std::tm tm;
		localtime_s(&tm, &time_t);
		char buf[64];
		std::strftime(buf, sizeof(buf), "%Y%m%d_%H%M%S", &tm);
		return std::string(buf);
	}

	bool dump_debug_players(std::string& out_path)
	{
		ensure_debug_directories();
		std::string filename = get_debug_directory() + "\\Players\\players_" + get_timestamp_str() + ".txt";
		out_path = filename;
		try
		{
			std::ofstream f(filename);
			if (!f.is_open()) return false;

			f << "=======================================================\n";
			f << " Ivory DEBUG DUMP - PLAYERS\n";
			f << " Time: " << get_timestamp_str() << "\n";
			f << "=======================================================\n\n";

			std::lock_guard<std::mutex> lock(cache::mtx);
			f << "Cached Players Count: " << cache::players.size() << "\n\n";

			for (size_t i = 0; i < cache::players.size(); i++)
			{
				const auto& p = cache::players[i];
				f << "[" << i << "] Player: " << p.name << " (Display: " << p.display_name << ")\n";
				f << "  Address: 0x" << std::hex << p.instance.address << std::dec << "\n";
				f << "  User ID: " << p.user_id << "\n";
				f << "  Health: " << p.health << " / " << p.max_health << "\n";
				f << "  Team: " << p.team << "\n";
				f << "  Priority: " << (int)p.priority << "\n";
				f << "  Position: (" << p.position.x << ", " << p.position.y << ", " << p.position.z << ")\n";
				f << "  HRP Prim: 0x" << std::hex << p.hrp_prim_addr << std::dec << "\n";
				f << "  Head Prim: 0x" << std::hex << p.head_prim_addr << std::dec << "\n";
				f << "  Torso Prim: 0x" << std::hex << p.torso_prim_addr << std::dec << "\n";
				f << "  Left Arm Prim: 0x" << std::hex << p.left_arm_prim_addr << std::dec << "\n";
				f << "  Right Arm Prim: 0x" << std::hex << p.right_arm_prim_addr << std::dec << "\n";
				f << "  Left Leg Prim: 0x" << std::hex << p.left_leg_prim_addr << std::dec << "\n";
				f << "  Right Leg Prim: 0x" << std::hex << p.right_leg_prim_addr << std::dec << "\n";
				f << "  Tool: " << p.tool_name << "\n";
				f << "  Parts map count: " << p.parts.size() << "\n";
				for (const auto& [part_name, part_inst] : p.parts)
				{
					f << "    - " << part_name << " (Addr: 0x" << std::hex << part_inst.address << std::dec << ")\n";
				}
				f << "-------------------------------------------------------\n";
			}
			f.close();
			return true;
		}
		catch (...) { return false; }
	}

	bool dump_debug_explorer(std::string& out_path)
	{
		ensure_debug_directories();
		std::string filename = get_debug_directory() + "\\Explorer\\datamodel_tree_" + get_timestamp_str() + ".txt";
		out_path = filename;
		try
		{
			std::ofstream f(filename);
			if (!f.is_open()) return false;

			f << "=======================================================\n";
			f << " Ivory DEBUG DUMP - DATAMODEL / EXPLORER HIERARCHY\n";
			f << " Time: " << get_timestamp_str() << "\n";
			f << "=======================================================\n\n";

			if (game::datamodel && game::datamodel->address)
			{
				f << "DataModel Address: 0x" << std::hex << game::datamodel->address << std::dec << "\n";
				f << "Game Place ID: " << game::datamodel->get_place_id() << "\n";
				f << "Game Job ID: " << game::datamodel->get_job_id() << "\n\n";

				std::vector<rbx::c_instance> services = game::datamodel->get_children<rbx::c_instance>();
				f << "Top-Level Services (" << services.size() << "):\n";
				for (auto& s : services)
				{
					if (!s.address) continue;
					f << "+ [" << s.get_class_name() << "] " << s.get_name() << " (0x" << std::hex << s.address << std::dec << ")\n";
					std::vector<rbx::c_instance> children = s.get_children<rbx::c_instance>();
					for (auto& c : children)
					{
						if (!c.address) continue;
						f << "  |- [" << c.get_class_name() << "] " << c.get_name() << " (0x" << std::hex << c.address << std::dec << ")\n";
						std::vector<rbx::c_instance> sub_children = c.get_children<rbx::c_instance>();
						for (auto& sc : sub_children)
						{
							if (!sc.address) continue;
							f << "     |-- [" << sc.get_class_name() << "] " << sc.get_name() << " (0x" << std::hex << sc.address << std::dec << ")\n";
						}
					}
				}
			}
			else
			{
				f << "DataModel is inactive / nullptr.\n";
			}
			f.close();
			return true;
		}
		catch (...) { return false; }
	}

	bool dump_debug_bones(std::string& out_path)
	{
		ensure_debug_directories();
		std::string filename = get_debug_directory() + "\\Bones\\local_player_bones_" + get_timestamp_str() + ".txt";
		out_path = filename;
		try
		{
			std::ofstream f(filename);
			if (!f.is_open()) return false;

			f << "=======================================================\n";
			f << " Ivory DEBUG DUMP - LOCAL PLAYER & BONES\n";
			f << " Time: " << get_timestamp_str() << "\n";
			f << "=======================================================\n\n";

			std::lock_guard<std::mutex> lock(cache::local_player_mtx);
			f << "Local Player Name: " << cache::local_player.name << " (" << cache::local_player.display_name << ")\n";
			f << "Address: 0x" << std::hex << cache::local_player.instance.address << std::dec << "\n";
			f << "Position: (" << cache::local_player.position.x << ", " << cache::local_player.position.y << ", " << cache::local_player.position.z << ")\n";
			f << "Head: (" << cache::local_player.part_positions.head.x << ", " << cache::local_player.part_positions.head.y << ", " << cache::local_player.part_positions.head.z << ")\n";
			f << "Torso: (" << cache::local_player.part_positions.torso.x << ", " << cache::local_player.part_positions.torso.y << ", " << cache::local_player.part_positions.torso.z << ")\n";
			f << "Left Arm: (" << cache::local_player.part_positions.left_arm.x << ", " << cache::local_player.part_positions.left_arm.y << ", " << cache::local_player.part_positions.left_arm.z << ")\n";
			f << "Right Arm: (" << cache::local_player.part_positions.right_arm.x << ", " << cache::local_player.part_positions.right_arm.y << ", " << cache::local_player.part_positions.right_arm.z << ")\n";
			f << "Left Leg: (" << cache::local_player.part_positions.left_leg.x << ", " << cache::local_player.part_positions.left_leg.y << ", " << cache::local_player.part_positions.left_leg.z << ")\n";
			f << "Right Leg: (" << cache::local_player.part_positions.right_leg.x << ", " << cache::local_player.part_positions.right_leg.y << ", " << cache::local_player.part_positions.right_leg.z << ")\n";
			f << "Health: " << cache::local_player.health << " / " << cache::local_player.max_health << "\n";

			if (game::visualengine && game::visualengine->address)
			{
				f << "\nVisualEngine View Dimensions: " << game::visualengine->get_dimensions().x << " x " << game::visualengine->get_dimensions().y << "\n";
				f << "VisualEngine Address: 0x" << std::hex << game::visualengine->address << std::dec << "\n";
			}
			f.close();
			return true;
		}
		catch (...) { return false; }
	}

	bool dump_debug_memory(std::string& out_path)
	{
		ensure_debug_directories();
		std::string filename = get_debug_directory() + "\\Memory\\game_memory_" + get_timestamp_str() + ".txt";
		out_path = filename;
		try
		{
			std::ofstream f(filename);
			if (!f.is_open()) return false;

			f << "=======================================================\n";
			f << " Ivory DEBUG DUMP - GAME & MEMORY STATUS\n";
			f << " Time: " << get_timestamp_str() << "\n";
			f << "=======================================================\n\n";

			if (game::datamodel && game::datamodel->address)
			{
				f << "DataModel: 0x" << std::hex << game::datamodel->address << std::dec << "\n";
				f << "Place ID: " << game::datamodel->get_place_id() << "\n";
				f << "Job ID: " << game::datamodel->get_job_id() << "\n";
			}
			if (game::visualengine && game::visualengine->address)
			{
				f << "VisualEngine: 0x" << std::hex << game::visualengine->address << std::dec << "\n";
				f << "View Dimensions: " << game::visualengine->get_dimensions().x << " x " << game::visualengine->get_dimensions().y << "\n";
			}

			MEMORYSTATUSEX mem_info;
			mem_info.dwLength = sizeof(MEMORYSTATUSEX);
			if (GlobalMemoryStatusEx(&mem_info))
			{
				f << "\nSystem Memory Usage: " << mem_info.dwMemoryLoad << "%\n";
				f << "Total Physical RAM: " << (mem_info.ullTotalPhys / (1024 * 1024)) << " MB\n";
				f << "Available RAM: " << (mem_info.ullAvailPhys / (1024 * 1024)) << " MB\n";
			}

			f.close();
			return true;
		}
		catch (...) { return false; }
	}

	bool clear_debug_files()
	{
		try
		{
			std::string base = get_debug_directory();
			if (std::filesystem::exists(base))
			{
				std::filesystem::remove_all(base);
				ensure_debug_directories();
				return true;
			}
			return false;
		}
		catch (...) { return false; }
	}
}

