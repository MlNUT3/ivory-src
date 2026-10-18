#pragma once
#include <string>
#include <vector>

namespace config
{
	struct config_info_t
	{
		std::string name;
		std::string path;
	};

	std::string get_base_directory();
	std::string get_config_directory();
	std::string get_config_path(const std::string& name);
	bool ensure_config_directory();

	std::string get_dumps_directory();
	bool ensure_dumps_directory();

	std::string get_resources_directory();
	bool ensure_resources_directory();

	std::vector<config_info_t> get_config_list();
	bool save_config(const std::string& name);
	bool load_config(const std::string& name);
	bool delete_config(const std::string& name);
	bool config_exists(const std::string& name);
	void reset_to_defaults();

	// memory-only config store (RAM, randomized keys, never touches disk)
	std::vector<std::string> get_config_list_mem();
	bool config_exists_mem(const std::string& name);
	bool save_config_mem(const std::string& name);
	bool load_config_mem(const std::string& name);
	bool delete_config_mem(const std::string& name);
	std::string generate_config_name();
	void wipe_config_mem();
	std::string serialize_settings();
	bool deserialize_settings(const std::string& json);

	bool set_autoload(const std::string& name);
	std::string get_autoload();
	void clear_autoload();
	bool load_autoload();

	// registry-backed autoload (no disk): the config that auto-loads every run
	bool set_autoload_mem(const std::string& name);
	std::string get_autoload_mem();
	void clear_autoload_mem();
	bool load_autoload_mem();

	// self-destruct: zero + delete every value under the cheat key, then the
	// key itself (the panic key and the panel self destruct both call this)
	void wipe_traces();

	bool save_theme();
	bool load_theme();

	std::string get_theme_directory();
	bool ensure_theme_directory();
	std::vector<std::string> get_theme_list();
	bool save_theme_custom(const std::string& name);
	bool load_theme_custom(const std::string& name);
	bool delete_theme_custom(const std::string& name);
	bool set_theme_autoload(const std::string& name);
	std::string get_theme_autoload();
	void clear_theme_autoload();
	bool auto_load_theme();
	bool save_theme_default();
	bool load_theme_default();

	bool export_config_to_clipboard();
	bool import_config_from_clipboard();

	// cloud configs (paste.rs) - network runs on a worker thread, the caller
	// drains results on the render thread
	bool share_config_cloud(std::string& out_url);
	bool fetch_config_cloud_body(const std::string& id_or_url, std::string& out_body);
	bool apply_fetched_config(const std::string& body);
	bool export_theme_to_clipboard();
	bool import_theme_from_clipboard();

	std::string get_scripts_directory();
	bool ensure_scripts_directory();
	std::vector<std::string> get_script_list();
	bool save_script(const std::string& name, const std::string& content);
	std::string load_script(const std::string& name);
	bool delete_script(const std::string& name);

	std::string get_debug_directory();
	bool ensure_debug_directories();
	bool dump_debug_players(std::string& out_path);
	bool dump_debug_explorer(std::string& out_path);
	bool dump_debug_bones(std::string& out_path);
	bool dump_debug_memory(std::string& out_path);
	bool clear_debug_files();
}
