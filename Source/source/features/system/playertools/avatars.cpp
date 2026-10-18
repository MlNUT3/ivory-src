// player avatars: roblox thumbnail cdn -> stb decode -> d3d11 srv cache.
// fetch happens on background workers; texture upload on the render thread.
// (stb_image implementation lives in the framework's fw_images.cpp)

#include <stb/stb_image.h>

#include "avatars.h"

#include <windows.h>
#include <winhttp.h>
#include <atomic>
#include <cctype>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <string>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <sdk/cache/core/cache.h>
#include <ui/render/render.h>

#pragma comment(lib, "winhttp.lib")

namespace playertools
{
	namespace avatars
	{
		namespace
		{
			struct Tex
			{
				ID3D11ShaderResourceView* srv = nullptr;
			};

			struct Pending
			{
				std::int64_t uid = 0;
				std::vector<std::uint8_t> png;
			};

			std::mutex g_mu;
			// steady-clock seconds for retry bookkeeping
			float now_secs()
			{
				return std::chrono::duration<float>(std::chrono::steady_clock::now().time_since_epoch()).count();
			}
			std::unordered_map<std::int64_t, Tex> g_tex;
			// retry timestamps: value = steady seconds after which the uid may
			// be retried. Roblox thumbnails return "Pending" while generating
			// and transient HTTP failures happen - a permanent blacklist meant
			// avatars for fresh joins could never load.
			std::unordered_map<std::int64_t, float> g_fail_at;
			std::unordered_set<std::int64_t> g_queued;
			std::unordered_set<std::int64_t> g_loading;
			std::vector<std::int64_t> g_queue;
			std::vector<Pending> g_ready;

			std::unordered_map<std::string, std::int64_t> g_name_uid;
			std::unordered_map<std::string, float> g_name_fail_at;

			static std::atomic<bool> s_workers_started{ false };

			std::string lower_copy(std::string s)
			{
				for (char& c : s)
					c = (char)std::tolower((unsigned char)c);
				return s;
			}

			bool http_get(const wchar_t* host, INTERNET_PORT port, const wchar_t* path,
				bool https, std::vector<std::uint8_t>& out)
			{
				out.clear();
				HINTERNET ses = WinHttpOpen(L"ivory/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
					WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
				if (!ses)
					return false;

				HINTERNET con = WinHttpConnect(ses, host, port, 0);
				if (!con)
				{
					WinHttpCloseHandle(ses);
					return false;
				}

				DWORD flags = https ? WINHTTP_FLAG_SECURE : 0;
				HINTERNET req = WinHttpOpenRequest(con, L"GET", path, nullptr,
					WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, flags);
				if (!req)
				{
					WinHttpCloseHandle(con);
					WinHttpCloseHandle(ses);
					return false;
				}

				DWORD redir = WINHTTP_OPTION_REDIRECT_POLICY_ALWAYS;
				WinHttpSetOption(req, WINHTTP_OPTION_REDIRECT_POLICY, &redir, sizeof(redir));

				BOOL ok = WinHttpSendRequest(req, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
					WINHTTP_NO_REQUEST_DATA, 0, 0, 0);
				if (!ok || !WinHttpReceiveResponse(req, nullptr))
				{
					WinHttpCloseHandle(req);
					WinHttpCloseHandle(con);
					WinHttpCloseHandle(ses);
					return false;
				}

				for (;;)
				{
					DWORD avail = 0;
					if (!WinHttpQueryDataAvailable(req, &avail) || avail == 0)
						break;

					std::size_t old = out.size();
					out.resize(old + avail);
					DWORD read = 0;
					if (!WinHttpReadData(req, out.data() + old, avail, &read))
					{
						out.resize(old);
						break;
					}
					out.resize(old + read);
					if (out.size() > (4u << 20))
						break;
				}

				WinHttpCloseHandle(req);
				WinHttpCloseHandle(con);
				WinHttpCloseHandle(ses);
				return !out.empty();
			}

			bool http_post_json(const wchar_t* host, const wchar_t* path, const std::string& body,
				std::vector<std::uint8_t>& out)
			{
				out.clear();
				HINTERNET ses = WinHttpOpen(L"ivory/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
					WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
				if (!ses)
					return false;

				HINTERNET con = WinHttpConnect(ses, host, INTERNET_DEFAULT_HTTPS_PORT, 0);
				if (!con)
				{
					WinHttpCloseHandle(ses);
					return false;
				}

				HINTERNET req = WinHttpOpenRequest(con, L"POST", path, nullptr,
					WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
				if (!req)
				{
					WinHttpCloseHandle(con);
					WinHttpCloseHandle(ses);
					return false;
				}

				wchar_t hdrs[] = L"Content-Type: application/json\r\n";
				BOOL ok = WinHttpSendRequest(req, hdrs, (DWORD)-1L,
					(LPVOID)body.data(), (DWORD)body.size(), (DWORD)body.size(), 0);
				if (!ok || !WinHttpReceiveResponse(req, nullptr))
				{
					WinHttpCloseHandle(req);
					WinHttpCloseHandle(con);
					WinHttpCloseHandle(ses);
					return false;
				}

				for (;;)
				{
					DWORD avail = 0;
					if (!WinHttpQueryDataAvailable(req, &avail) || avail == 0)
						break;

					std::size_t old = out.size();
					out.resize(old + avail);
					DWORD read = 0;
					if (!WinHttpReadData(req, out.data() + old, avail, &read))
					{
						out.resize(old);
						break;
					}
					out.resize(old + read);
					if (out.size() > (1u << 20))
						break;
				}

				WinHttpCloseHandle(req);
				WinHttpCloseHandle(con);
				WinHttpCloseHandle(ses);
				return !out.empty();
			}

			std::int64_t parse_uid_from_json(const std::string& json, const std::string& name)
			{
				std::string key = "\"name\":\"" + name + "\"";
				std::size_t pos = json.find(key);
				if (pos == std::string::npos)
				{
					std::string low = lower_copy(name);
					std::string jlow = lower_copy(json);
					pos = jlow.find("\"name\":\"" + low + "\"");
					if (pos == std::string::npos)
						pos = 0;
				}

				std::size_t id = json.find("\"id\":", pos == std::string::npos ? 0 : pos);
				if (id == std::string::npos)
					id = json.find("\"id\":");
				if (id == std::string::npos)
					return 0;

				id += 5;
				while (id < json.size() && (json[id] == ' ' || json[id] == '\t'))
					++id;

				char* end = nullptr;
				long long v = std::strtoll(json.c_str() + id, &end, 10);
				return (v > 0) ? (std::int64_t)v : 0;
			}

			bool resolve_name_to_uid(const std::string& name, std::int64_t& out_uid)
			{
				out_uid = 0;
				if (name.empty() || name == "unknown")
					return false;

				std::string body = "{\"usernames\":[\"";
				for (char c : name)
				{
					if (c == '"' || c == '\\')
						body.push_back('\\');
					body.push_back(c);
				}
				body += "\"],\"excludeBannedUsers\":false}";

				std::vector<std::uint8_t> raw;
				if (!http_post_json(L"users.roblox.com", L"/v1/usernames/users", body, raw))
					return false;

				std::string json(raw.begin(), raw.end());
				out_uid = parse_uid_from_json(json, name);
				return out_uid > 0;
			}

			bool parse_url(const std::string& url, std::wstring& host, std::wstring& path, bool& https)
			{
				https = true;
				const char* p = url.c_str();
				if (std::strncmp(p, "https://", 8) == 0)
					p += 8;
				else if (std::strncmp(p, "http://", 7) == 0)
				{
					p += 7;
					https = false;
				}
				else
					return false;

				const char* slash = std::strchr(p, '/');
				std::string h, path_a;
				if (!slash)
				{
					h = p;
					path_a = "/";
				}
				else
				{
					h.assign(p, slash);
					path_a = slash;
				}
				if (h.empty())
					return false;

				host.assign(h.begin(), h.end());
				path.assign(path_a.begin(), path_a.end());
				return true;
			}

			std::string find_image_url(const std::string& json, std::int64_t uid)
			{
				char key[64]{};
				std::snprintf(key, sizeof(key), "\"targetId\":%lld", (long long)uid);

				std::size_t pos = json.find(key);
				if (pos == std::string::npos)
				{
					std::snprintf(key, sizeof(key), "\"targetId\": %lld", (long long)uid);
					pos = json.find(key);
				}

				std::size_t from = (pos == std::string::npos) ? 0 : pos;
				std::size_t u = json.find("\"imageUrl\"", from);
				if (u == std::string::npos)
					return {};

				u = json.find(':', u);
				if (u == std::string::npos)
					return {};
				u = json.find('"', u);
				if (u == std::string::npos)
					return {};

				std::size_t a = u + 1;
				std::size_t b = json.find('"', a);
				if (b == std::string::npos || b <= a)
					return {};

				std::string url = json.substr(a, b - a);
				for (std::size_t i = 0; i + 1 < url.size();)
				{
					if (url[i] == '\\' && url[i + 1] == '/')
						url.erase(i, 1);
					else
						++i;
				}
				return url;
			}

			bool fetch_png(std::int64_t uid, std::vector<std::uint8_t>& png)
			{
				png.clear();
				if (uid <= 0)
					return false;

				wchar_t path[160]{};
				_snwprintf_s(path, _TRUNCATE,
					L"/v1/users/avatar-bust?userIds=%lld&size=420x420&format=Png&isCircular=false",
					(long long)uid);

				std::vector<std::uint8_t> raw;
				if (!http_get(L"thumbnails.roblox.com", INTERNET_DEFAULT_HTTPS_PORT, path, true, raw))
					return false;

				std::string json(raw.begin(), raw.end());
				if (json.find("\"Pending\"") != std::string::npos &&
					json.find("\"imageUrl\"") == std::string::npos)
					return false;

				std::string img = find_image_url(json, uid);
				if (img.empty())
					return false;

				std::wstring host, pth;
				bool https = true;
				if (!parse_url(img, host, pth, https))
					return false;

				INTERNET_PORT port = https ? INTERNET_DEFAULT_HTTPS_PORT : INTERNET_DEFAULT_HTTP_PORT;
				return http_get(host.c_str(), port, pth.c_str(), https, png);
			}

			ID3D11ShaderResourceView* make_srv(const std::vector<std::uint8_t>& png)
			{
				if (!render || !render->detail || !render->detail->device)
					return nullptr;

				int w = 0, h = 0, channels = 0;
				unsigned char* px = stbi_load_from_memory(png.data(), (int)png.size(), &w, &h, &channels, 4);
				if (!px || w <= 0 || h <= 0)
					return nullptr;

				D3D11_TEXTURE2D_DESC td{};
				td.Width = (UINT)w;
				td.Height = (UINT)h;
				td.MipLevels = 1;
				td.ArraySize = 1;
				td.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
				td.SampleDesc.Count = 1;
				td.Usage = D3D11_USAGE_DEFAULT;
				td.BindFlags = D3D11_BIND_SHADER_RESOURCE;

				D3D11_SUBRESOURCE_DATA sd{};
				sd.pSysMem = px;
				sd.SysMemPitch = (UINT)(w * 4);

				ID3D11Texture2D* tex = nullptr;
				HRESULT hr = render->detail->device->CreateTexture2D(&td, &sd, &tex);
				stbi_image_free(px);
				if (FAILED(hr) || !tex)
					return nullptr;

				ID3D11ShaderResourceView* srv = nullptr;
				hr = render->detail->device->CreateShaderResourceView(tex, nullptr, &srv);
				tex->Release();
				return SUCCEEDED(hr) ? srv : nullptr;
			}

			void worker_loop()
			{
				for (;;)
				{
					std::string name_job;
					std::int64_t uid = 0;
					{
						std::lock_guard<std::mutex> lk(g_mu);
						bool idle = g_queue.empty();
						if (!idle)
						{
							uid = g_queue.back();
							g_queue.pop_back();
							g_queued.erase(uid);
							g_loading.insert(uid);
						}
						if (idle)
						{
							// nothing to do - sleep outside the lock
						}
					}
					if (uid == 0)
					{
						std::this_thread::sleep_for(std::chrono::milliseconds(120));
						continue;
					}

					std::vector<std::uint8_t> png;
					const bool ok = fetch_png(uid, png);
					{
						std::lock_guard<std::mutex> lk(g_mu);
						g_loading.erase(uid);
						if (ok && !png.empty())
						{
							g_ready.push_back(Pending{ uid, std::move(png) });
							g_fail_at.erase(uid);
						}
						else
						{
							g_fail_at[uid] = now_secs() + 60.f;  // retry in 60s
						}
					}
				}
			}

			void ensure_workers()
			{
				// atomic guard - tick() (render thread) and get() (any thread)
				// race here; a plain bool could spawn duplicate worker sets
				bool expected = false;
				if (!s_workers_started.compare_exchange_strong(expected, true))
					return;
				for (int i = 0; i < 4; i++)
					std::thread(worker_loop).detach();
			}
		}

		void tick()
		{
			ensure_workers();

			// queue wanted uids from the player cache
			{
				std::lock_guard<std::mutex> lk(g_mu);
				std::lock_guard<std::mutex> clock(cache::mtx);
				for (const auto& ent : cache::players)
				{
					if (ent.user_id <= 0)
						continue;
					if (g_tex.count(ent.user_id) || g_queued.count(ent.user_id) || g_loading.count(ent.user_id))
						continue;
					auto fit = g_fail_at.find(ent.user_id);
					if (fit != g_fail_at.end() && now_secs() < fit->second)
						continue;
					else if (fit != g_fail_at.end())
						g_fail_at.erase(fit);
					if (g_queue.size() > 32)
						break;
					g_queue.push_back(ent.user_id);
					g_queued.insert(ent.user_id);
				}
			}

			// upload decoded pngs (render thread)
			std::vector<Pending> ready;
			{
				std::lock_guard<std::mutex> lk(g_mu);
				ready.swap(g_ready);
			}
			for (auto& p : ready)
			{
				ID3D11ShaderResourceView* srv = make_srv(p.png);
				std::lock_guard<std::mutex> lk(g_mu);
				if (!srv)
				{
					g_fail_at[p.uid] = now_secs() + 60.f;  // retry in 60s
					continue;
				}
				auto it = g_tex.find(p.uid);
				if (it != g_tex.end() && it->second.srv)
					it->second.srv->Release();
				g_tex[p.uid].srv = srv;
				g_fail_at.erase(p.uid);
			}
		}

		ID3D11ShaderResourceView* get(std::int64_t user_id)
		{
			if (user_id <= 0)
				return nullptr;
			ensure_workers();
			std::lock_guard<std::mutex> lk(g_mu);
			auto it = g_tex.find(user_id);
			if (it != g_tex.end())
				return it->second.srv;
			if (!g_queued.count(user_id) && !g_loading.count(user_id) && g_queue.size() < 64)
			{
				auto fit = g_fail_at.find(user_id);
				if (fit != g_fail_at.end())
				{
					if (now_secs() < fit->second)
						return nullptr;  // still cooling down
					g_fail_at.erase(fit);
				}
				g_queue.push_back(user_id);
				g_queued.insert(user_id);
			}
			return nullptr;
		}

		std::int64_t lookup_user_id(const std::string& username)
		{
			{
				std::lock_guard<std::mutex> lk(g_mu);
				auto it = g_name_uid.find(username);
				if (it != g_name_uid.end())
					return it->second;
				auto fit = g_name_fail_at.find(username);
				if (fit != g_name_fail_at.end())
					return 0;
			}
			std::int64_t uid = 0;
			if (resolve_name_to_uid(username, uid))
			{
				std::lock_guard<std::mutex> lk(g_mu);
				g_name_uid[username] = uid;
				return uid;
			}
			std::lock_guard<std::mutex> lk(g_mu);
			g_name_fail_at[username] = -1.f;
			return 0;
		}

		void clear()
		{
			std::lock_guard<std::mutex> lk(g_mu);
			for (auto& kv : g_tex)
			{
				if (kv.second.srv)
					kv.second.srv->Release();
			}
			g_tex.clear();
			g_queue.clear();
			g_queued.clear();
			g_loading.clear();
			g_ready.clear();
			g_fail_at.clear();
			g_name_uid.clear();
			g_name_fail_at.clear();
		}
	}
}
