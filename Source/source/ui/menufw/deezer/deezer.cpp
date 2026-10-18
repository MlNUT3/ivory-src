#include "deezer.h"
#include "audio_engine.hpp"

#include <winhttp.h>
#include <algorithm>
#include <thread>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iomanip>

#pragma comment(lib, "winhttp.lib")

#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>

#pragma comment(lib, "mfplat.lib")
#pragma comment(lib, "mfreadwrite.lib")
#pragma comment(lib, "mfuuid.lib")
#pragma comment(lib, "ole32.lib")

#ifndef WINHTTP_OPTION_REDIRECT_POLICY
#define WINHTTP_OPTION_REDIRECT_POLICY 88
#endif
#ifndef WINHTTP_OPTION_REDIRECT_POLICY_ALWAYS
#define WINHTTP_OPTION_REDIRECT_POLICY_ALWAYS 2
#endif

namespace
{
    void skip_ws(const std::string& s, size_t& i)
    {
        while (i < s.size() && (s[i] == ' ' || s[i] == '\t' || s[i] == '\n' || s[i] == '\r'))
            i++;
    }

    std::string json_decode(const std::string& v)
    {
        std::string out;
        out.reserve(v.size());

        for (size_t i = 0; i < v.size(); ++i)
        {
            char c = v[i];
            if (c != '\\' || i + 1 >= v.size())
            {
                out += c;
                continue;
            }

            char n = v[++i];
            switch (n)
            {
            case 'n': out += '\n'; break;
            case 'r': out += '\r'; break;
            case 't': out += '\t'; break;
            case 'b': out += '\b'; break;
            case 'f': out += '\f'; break;
            case 'u':
            {
                if (i + 4 >= v.size())
                    return out;

                unsigned code = 0;
                for (int k = 1; k <= 4; ++k)
                {
                    char h = v[i + k];
                    code <<= 4;
                    if (h >= '0' && h <= '9') code |= (unsigned)(h - '0');
                    else if (h >= 'a' && h <= 'f') code |= (unsigned)(h - 'a' + 10);
                    else if (h >= 'A' && h <= 'F') code |= (unsigned)(h - 'A' + 10);
                }
                i += 4;

                if (code < 0x80)
                    out += (char)code;
                else if (code < 0x800)
                {
                    out += (char)(0xC0 | (code >> 6));
                    out += (char)(0x80 | (code & 0x3F));
                }
                else
                {
                    out += (char)(0xE0 | (code >> 12));
                    out += (char)(0x80 | ((code >> 6) & 0x3F));
                    out += (char)(0x80 | (code & 0x3F));
                }
                break;
            }
            default: out += n; break;
            }
        }
        return out;
    }

    bool key_colon(const std::string& s, const std::string& key, size_t& out)
    {
        const std::string pat = "\"" + key + "\"";
        size_t p = 0;

        while ((p = s.find(pat, p)) != std::string::npos)
        {
            size_t backslashes = 0;
            size_t q = p;
            while (q > 0 && s[q - 1] == '\\')
            {
                backslashes++;
                q--;
            }

            if (backslashes % 2 == 0)
            {
                size_t i = p + pat.size();
                skip_ws(s, i);
                if (i < s.size() && s[i] == ':')
                {
                    out = i;
                    return true;
                }
            }
            p += pat.size();
        }
        return false;
    }

    bool get_str(const std::string& s, const std::string& key, std::string& out)
    {
        size_t c;
        if (!key_colon(s, key, c))
            return false;

        size_t i = c + 1;
        skip_ws(s, i);
        if (i >= s.size() || s[i] != '"')
            return false;

        i++;
        std::string raw;
        while (i < s.size())
        {
            if (s[i] == '\\' && i + 1 < s.size())
            {
                raw += s[i];
                raw += s[i + 1];
                i += 2;
            }
            else if (s[i] == '"')
                break;
            else
                raw += s[i++];
        }

        out = json_decode(raw);
        return true;
    }

    long long get_num(const std::string& s, const std::string& key)
    {
        size_t c;
        if (!key_colon(s, key, c))
            return 0;

        size_t i = c + 1;
        skip_ws(s, i);

        bool neg = false;
        if (i < s.size() && s[i] == '-')
        {
            neg = true;
            i++;
        }

        long long v = 0;
        bool any = false;
        while (i < s.size() && s[i] >= '0' && s[i] <= '9')
        {
            v = v * 10 + (s[i] - '0');
            i++;
            any = true;
        }
        if (!any)
            return 0;
        return neg ? -v : v;
    }

    std::string subobj(const std::string& s, const std::string& key)
    {
        size_t c;
        if (!key_colon(s, key, c))
            return {};

        size_t i = c + 1;
        skip_ws(s, i);
        if (i >= s.size() || s[i] != '{')
            return {};

        int depth = 0;
        bool in_str = false;
        for (size_t j = i; j < s.size(); ++j)
        {
            char ch = s[j];
            if (in_str)
            {
                if (ch == '\\') j++;
                else if (ch == '"') in_str = false;
                continue;
            }
            if (ch == '"') { in_str = true; continue; }
            if (ch == '{') depth++;
            else if (ch == '}')
            {
                depth--;
                if (depth == 0)
                    return s.substr(i, j - i + 1);
            }
        }
        return {};
    }

    std::vector<std::string> arr_objs(const std::string& s, const std::string& key)
    {
        std::vector<std::string> out;

        size_t c;
        if (!key_colon(s, key, c))
            return out;

        size_t i = c + 1;
        skip_ws(s, i);
        if (i >= s.size() || s[i] != '[')
            return out;

        i++;
        while (i < s.size())
        {
            skip_ws(s, i);
            if (i >= s.size() || s[i] == ']')
                break;
            if (s[i] == ',')
            {
                i++;
                continue;
            }

            if (s[i] == '{')
            {
                int depth = 0;
                bool in_str = false;
                size_t j = i;
                for (; j < s.size(); ++j)
                {
                    char ch = s[j];
                    if (in_str)
                    {
                        if (ch == '\\') j++;
                        else if (ch == '"') in_str = false;
                        continue;
                    }
                    if (ch == '"') { in_str = true; continue; }
                    if (ch == '{') depth++;
                    else if (ch == '}')
                    {
                        depth--;
                        if (depth == 0)
                        {
                            out.push_back(s.substr(i, j - i + 1));
                            i = j + 1;
                            break;
                        }
                    }
                }
                if (j >= s.size())
                    break;
            }
            else if (s[i] == '"')
            {
                i++;
                while (i < s.size())
                {
                    if (s[i] == '\\') i += 2;
                    else if (s[i] == '"') { i++; break; }
                    else i++;
                }
            }
            else
                i++;
        }
        return out;
    }

    std::vector<std::string> arr_str(const std::string& s, const std::string& key)
    {
        std::vector<std::string> out;

        size_t c;
        if (!key_colon(s, key, c))
            return out;

        size_t i = c + 1;
        skip_ws(s, i);
        if (i >= s.size() || s[i] != '[')
            return out;

        i++;
        while (i < s.size())
        {
            skip_ws(s, i);
            if (i >= s.size() || s[i] == ']')
                break;
            if (s[i] == ',')
            {
                i++;
                continue;
            }
            if (s[i] == '"')
            {
                i++;
                std::string raw;
                while (i < s.size())
                {
                    if (s[i] == '\\' && i + 1 < s.size())
                    {
                        raw += s[i];
                        raw += s[i + 1];
                        i += 2;
                    }
                    else if (s[i] == '"')
                        break;
                    else
                        raw += s[i++];
                }
                out.push_back(json_decode(raw));
                i++;
                continue;
            }
            while (i < s.size() && s[i] != ',' && s[i] != ']')
                i++;
        }
        return out;
    }

    dz_track parse_audius_track(const std::string& o)
    {
        dz_track t;
        t.provider = prov_audius;
        get_str(o, "id", t.src_id);
        get_str(o, "title", t.title);
        t.duration = (int)get_num(o, "duration");

        const std::string u = subobj(o, "user");
        if (!u.empty())
            get_str(u, "name", t.artist);

        const std::string st = subobj(o, "stream");
        if (!st.empty())
        {
            get_str(st, "url", t.preview);
            t.mirrors = arr_str(st, "mirrors");
        }

        const std::string aw = subobj(o, "artwork");
        if (!aw.empty())
        {
            if (!get_str(aw, "480x480", t.cover))
                get_str(aw, "1000x1000", t.cover);
            t.cover_mirrors = arr_str(aw, "mirrors");
        }

        if (t.title.empty())
            t.src_id.clear();
        return t;
    }

    dz_track parse_track(const std::string& o)
    {
        dz_track t;
        t.id = get_num(o, "id");
        get_str(o, "title", t.title);
        get_str(o, "preview", t.preview);
        t.duration = (int)get_num(o, "duration");

        const std::string ar = subobj(o, "artist");
        if (!ar.empty())
            get_str(ar, "name", t.artist);

        const std::string al = subobj(o, "album");
        if (!al.empty())
        {
            if (!get_str(al, "cover_medium", t.cover))
                get_str(al, "cover", t.cover);
            get_str(al, "title", t.album);
        }
        return t;
    }

    dz_playlist parse_playlist(const std::string& o)
    {
        dz_playlist p;
        p.id = get_num(o, "id");
        get_str(o, "title", p.title);
        p.track_count = (int)get_num(o, "nb_tracks");

        if (!get_str(o, "cover_medium", p.cover) && !get_str(o, "picture_medium", p.cover))
            get_str(o, "picture", p.cover);
        return p;
    }

    std::string url_encode(const std::string& value)
    {
        std::ostringstream escaped;
        escaped.fill('0');
        escaped << std::hex;

        for (char c : value)
        {
            if (isalnum((unsigned char)c) || c == '-' || c == '_' || c == '.' || c == '~')
                escaped << c;
            else
                escaped << '%' << std::setw(2) << (int)((unsigned char)c);
        }
        return escaped.str();
    }

    bool split_url(const std::string& url, std::wstring& host, std::wstring& path)
    {
        const std::string scheme = "https://";
        if (url.rfind(scheme, 0) != 0)
            return false;

        const size_t start = scheme.size();
        const size_t slash = url.find('/', start);
        const std::string h = (slash == std::string::npos) ? url.substr(start) : url.substr(start, slash - start);
        const std::string p = (slash == std::string::npos) ? std::string("/") : url.substr(slash);

        if (h.empty())
            return false;

        host.assign(h.begin(), h.end());
        path.assign(p.begin(), p.end());
        return true;
    }

    struct http_guard
    {
        HINTERNET session = nullptr;
        HINTERNET connect = nullptr;
        HINTERNET request = nullptr;
        FILE* out = nullptr;
        const char* file = nullptr;
        bool committed = false;

        ~http_guard()
        {
            if (out)
            {
                fclose(out);
                if (!committed && file)
                    DeleteFileA(file);
            }
            if (request) WinHttpCloseHandle(request);
            if (connect)  WinHttpCloseHandle(connect);
            if (session)  WinHttpCloseHandle(session);
        }
    };

    bool http_fetch(const std::wstring& server, const std::wstring& path,
                    std::string* text, const char* file,
                    std::vector<unsigned char>* bytes = nullptr,
                    size_t cap = 0, int* status_out = nullptr,
                    const wchar_t* ua = L"Vade/1.0",
                    const wchar_t* method = L"GET",
                    const void* body = nullptr, DWORD body_len = 0,
                    const wchar_t* extra_headers = nullptr)
    {
        if (status_out)
            *status_out = 0;

        http_guard g;
        g.file = file;

        g.session = WinHttpOpen(ua,
                                WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                                WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
        if (!g.session)
            return false;

        WinHttpSetTimeouts(g.session, 10000, 10000, 30000, 30000);

        DWORD redirect = WINHTTP_OPTION_REDIRECT_POLICY_ALWAYS;
        WinHttpSetOption(g.session, WINHTTP_OPTION_REDIRECT_POLICY, &redirect, sizeof(redirect));

        bool ok = false;
        g.connect = WinHttpConnect(g.session, server.c_str(), INTERNET_DEFAULT_HTTPS_PORT, 0);
        if (g.connect)
        {
            g.request = WinHttpOpenRequest(g.connect, method, path.c_str(), nullptr,
                                           WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                                           WINHTTP_FLAG_SECURE);
            if (g.request &&
                WinHttpSendRequest(g.request,
                                   extra_headers ? extra_headers : WINHTTP_NO_ADDITIONAL_HEADERS,
                                   extra_headers ? -1L : 0,
                                   body ? (LPVOID)body : WINHTTP_NO_REQUEST_DATA,
                                   body ? body_len : 0, body_len, 0) &&
                WinHttpReceiveResponse(g.request, nullptr))
            {
                DWORD status_code = 0;
                DWORD status_size = sizeof(status_code);
                WinHttpQueryHeaders(g.request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                                    WINHTTP_HEADER_NAME_BY_INDEX, &status_code, &status_size,
                                    WINHTTP_NO_HEADER_INDEX);

                if (status_out)
                    *status_out = (int)status_code;

                // only touch the destination once we know the response is good, so a
                // failed request can never truncate a cached file
                if (file && status_code == 200)
                {
                    fopen_s(&g.out, file, "wb");
                    if (!g.out)
                        return false;
                }

                if (status_code == 200 || (file == nullptr && bytes == nullptr))
                {
                    ok = true;
                    DWORD avail = 0;
                    while (WinHttpQueryDataAvailable(g.request, &avail) && avail > 0)
                    {
                        char buf[8192];
                        DWORD read = 0;
                        const DWORD want = (avail > sizeof(buf)) ? (DWORD)sizeof(buf) : avail;
                        if (!WinHttpReadData(g.request, buf, want, &read) || read == 0)
                        {
                            ok = false;
                            break;
                        }

                        if (text)
                            text->append(buf, read);
                        if (bytes)
                        {
                            if (cap && bytes->size() + read > cap)
                            {
                                ok = false;
                                break;
                            }
                            bytes->insert(bytes->end(), buf, buf + read);
                        }
                        if (g.out)
                            fwrite(buf, 1, read, g.out);

                        avail = 0;
                    }
                }

                if (g.out)
                {
                    fclose(g.out);
                    g.out = nullptr;
                    if (ok)
                        g.committed = true;
                    else
                        DeleteFileA(file);
                }
            }
        }

        if (bytes && !ok)
            bytes->clear();

        return ok;
    }

    std::string http_get(const std::wstring& server, const std::wstring& path, int* status = nullptr,
                         const wchar_t* ua = L"Vade/1.0")
    {
        std::string text;
        http_fetch(server, path, &text, nullptr, nullptr, 0, status, ua);
        return text;
    }

    bool http_post_json(const std::wstring& server, const std::wstring& path,
                        const std::string& json, std::string& out, int* status = nullptr,
                        const wchar_t* ua = L"Vade/1.0")
    {
        return http_fetch(server, path, &out, nullptr, nullptr, 0, status, ua, L"POST",
                          json.data(), (DWORD)json.size(),
                          L"Content-Type: application/json\r\n");
    }

    std::mutex g_stream_err_mtx;
    std::string g_stream_err;

    void stream_log(const std::string& line)
    {
        char temp[MAX_PATH] = {};
        GetTempPathA(MAX_PATH, temp);
        const std::string dir = std::string(temp) + "vade\\";
        CreateDirectoryA(dir.c_str(), nullptr);

        FILE* f = nullptr;
        if (fopen_s(&f, (dir + "stream.log").c_str(), "a") != 0 || !f)
            return;

        SYSTEMTIME st{};
        GetLocalTime(&st);
        fprintf(f, "%02u:%02u:%02u.%03u %s\n",
                st.wHour, st.wMinute, st.wSecond, st.wMilliseconds, line.c_str());
        fclose(f);
    }

    void set_stream_err(const std::string& s)
    {
        {
            std::lock_guard<std::mutex> lock(g_stream_err_mtx);
            g_stream_err = s;
        }
        if (!s.empty())
            stream_log("ERR  " + s);
    }

    std::string get_stream_err()
    {
        std::lock_guard<std::mutex> lock(g_stream_err_mtx);
        return g_stream_err;
    }

    std::mutex g_sc_pace_mtx;
    uint64_t g_sc_last_req = 0;
    uint64_t g_sc_block_until = 0;

    void sc_backoff(uint64_t ms)
    {
        std::lock_guard<std::mutex> lock(g_sc_pace_mtx);
        const uint64_t until = GetTickCount64() + ms;
        if (until > g_sc_block_until)
            g_sc_block_until = until;
    }

    void sc_pace()
    {
        std::lock_guard<std::mutex> lock(g_sc_pace_mtx);

        uint64_t now = GetTickCount64();
        if (now < g_sc_block_until)
        {
            Sleep((DWORD)(g_sc_block_until - now));
            now = GetTickCount64();
        }

        const uint64_t gap = 200;
        if (g_sc_last_req != 0 && now < g_sc_last_req + gap)
            Sleep((DWORD)(g_sc_last_req + gap - now));
        g_sc_last_req = GetTickCount64();
    }

    bool http_get_bytes(const std::wstring& server, const std::wstring& path,
                        std::vector<unsigned char>& out, size_t cap = 0,
                        int* status_out = nullptr, const wchar_t* ua = L"Vade/1.0",
                        const wchar_t* extra_headers = nullptr)
    {
        return http_fetch(server, path, nullptr, nullptr, &out, cap, status_out, ua, L"GET",
                          nullptr, 0, extra_headers);
    }

    // ---------------- soundcloud ----------------

    std::mutex g_sc_mtx;
    std::string g_sc_cid;

    std::string sc_pick_progressive(const std::string& media)
    {
        std::string any;
        for (const auto& tc : arr_objs(media, "transcodings"))
        {
            const std::string fmt = subobj(tc, "format");
            std::string proto;
            if (!get_str(fmt, "protocol", proto) || proto != "progressive")
                continue;

            std::string url;
            if (!get_str(tc, "url", url) || url.empty())
                continue;

            std::string qual;
            get_str(tc, "quality", qual);
            if (qual == "sq")
                return url;
            if (any.empty())
                any = url;
        }
        return any;
    }

    std::vector<std::string> sc_js_urls(const std::string& s)
    {
        std::vector<std::string> out;
        const std::string key = "assets/";
        size_t p = 0;
        while ((p = s.find(key, p)) != std::string::npos)
        {
            size_t b = p;
            while (b > 0)
            {
                const unsigned char c = (unsigned char)s[b - 1];
                if (isalnum(c) || c == '.' || c == '-' || c == '_' || c == '/' || c == ':')
                    --b;
                else
                    break;
            }

            size_t e = p;
            while (e < s.size())
            {
                const unsigned char c = (unsigned char)s[e];
                if (isalnum(c) || c == '.' || c == '-' || c == '_' || c == '/')
                    ++e;
                else
                    break;
            }

            if (e - p > 8 && s.compare(e - 3, 3, ".js") == 0)
            {
                std::string u = s.substr(b, e - b);
                if (u.rfind("//", 0) == 0)
                    u = "https:" + u;
                if (u.rfind("https://", 0) == 0 &&
                    std::find(out.begin(), out.end(), u) == out.end())
                    out.push_back(u);
            }

            p = (e > p) ? e : p + key.size();
        }
        return out;
    }

    void sc_collect_ids(const std::string& s, std::vector<std::string>& out)
    {
        const std::string key = "client_id";
        size_t p = 0;
        while ((p = s.find(key, p)) != std::string::npos)
        {
            size_t i = p + key.size();
            while (i < s.size() && (s[i] == '"' || s[i] == '\'' || s[i] == ':' || s[i] == '=' ||
                                    s[i] == ' ' || s[i] == '\t' || s[i] == '\r' || s[i] == '\n' ||
                                    s[i] == '?' || s[i] == '&' || s[i] == '/'))
                i++;

            size_t j = i;
            while (j < s.size() && (isalnum((unsigned char)s[j]) || s[j] == '_' || s[j] == '-'))
                j++;

            if (j - i >= 24 && j - i <= 64)
            {
                const std::string v = s.substr(i, j - i);
                if (std::find(out.begin(), out.end(), v) == out.end())
                    out.push_back(v);
            }

            p = (i > p) ? i : p + key.size();
        }
    }

    std::string sc_resolve_cid(const std::string& cid, const std::string& trans_url,
                               int* status_out = nullptr)
    {
        if (status_out)
            *status_out = 0;
        if (cid.empty() || trans_url.empty())
            return {};

        std::wstring host, path;
        if (!split_url(trans_url, host, path))
            return {};

        const bool has_q = trans_url.find('?') != std::string::npos;
        const std::string spath(path.begin(), path.end());
        const std::string full = spath + (has_q ? "&" : "?") + "client_id=" + cid;
        const std::wstring wfull(full.begin(), full.end());

        sc_pace();

        int st = 0;
        const std::string body = http_get(host, wfull, &st);
        if (status_out)
            *status_out = st;
        if (st < 200 || st >= 300 || body.empty())
        {
            if (st == 429 || st == 503 || st == 0)
                sc_backoff(2500);
            stream_log("RESOLVE st=" + std::to_string(st) + " cid=" + cid + " path=" +
                       full.substr(0, full.find("?client_id")));
            set_stream_err(st ? "resolve http " + std::to_string(st) : "resolve no response");
            return {};
        }

        std::string url;
        if (!get_str(body, "url", url) || url.rfind("http", 0) != 0)
        {
            stream_log("RESOLVE no-url cid=" + cid + " body=" + body.substr(0, 160));
            set_stream_err("resolve returned no stream url");
            return {};
        }
        return url;
    }

    bool sc_ping(const std::string& cid)
    {
        const std::string path = "/search/tracks?q=music&limit=1&client_id=" + cid;
        const std::wstring w(path.begin(), path.end());
        int st = 0;
        const std::string body = http_get(L"api-v2.soundcloud.com", w, &st);
        if (st < 200 || st >= 300 || body.find("\"collection\"") == std::string::npos)
            return false;

        // search rights are not stream rights - prove this id resolves a real stream
        for (const auto& o : arr_objs(body, "collection"))
        {
            const std::string trans = sc_pick_progressive(subobj(o, "media"));
            if (trans.empty())
                continue;

            int rst = 0;
            if (!sc_resolve_cid(cid, trans, &rst).empty())
            {
                stream_log("PING ok cid=" + cid);
                return true;
            }

            // rate limits and blips are not proof the key is dead - only auth says so
            const bool keep = (rst != 401 && rst != 403);
            stream_log("PING cid=" + cid + " stream_st=" + std::to_string(rst) +
                       (keep ? " kept" : " rejected"));
            return keep;
        }
        stream_log("PING cid=" + cid + " no progressive in probe search");
        return true;
    }

    bool sc_client_id(std::string& out)
    {
        {
            std::lock_guard<std::mutex> lock(g_sc_mtx);
            if (!g_sc_cid.empty())
            {
                out = g_sc_cid;
                return true;
            }
        }

        const std::string html = http_get(L"soundcloud.com", L"/");
        if (!html.empty())
        {
            for (const auto& js : sc_js_urls(html))
            {
                std::wstring h, p;
                if (!split_url(js, h, p))
                    continue;

                const std::string bundle = http_get(h, p);
                if (bundle.empty())
                    continue;

                std::vector<std::string> cands;
                sc_collect_ids(bundle, cands);
                for (const auto& c : cands)
                {
                    if (!sc_ping(c))
                        continue;

                    {
                        std::lock_guard<std::mutex> lock(g_sc_mtx);
                        g_sc_cid = c;
                    }
                    out = c;
                    stream_log("CID  discovered=" + c);
                    return true;
                }
            }
        }

        return false;
    }

    void sc_invalidate(const std::string& bad)
    {
        std::lock_guard<std::mutex> lock(g_sc_mtx);
        if (!bad.empty() && g_sc_cid == bad)
            g_sc_cid.clear();
    }

    std::string sc_get(const std::string& base)
    {
        std::string cid;
        if (!sc_client_id(cid))
            return {};

        auto call = [&](const std::string& b, const std::string& c, int* out_st) -> std::string
        {
            const std::string path = b +
                (b.find('?') != std::string::npos ? "&" : "?") + "client_id=" + c;
            const std::wstring wpath(path.begin(), path.end());
            int st = 0;
            sc_pace();
            const std::string body = http_get(L"api-v2.soundcloud.com", wpath, &st);
            if (out_st)
                *out_st = st;
            if (st >= 200 && st < 300 && !body.empty())
                return body;
            if (st == 429 || st == 503 || st == 0)
                sc_backoff(2500);
            return {};
        };

        int st = 0;
        std::string body = call(base, cid, &st);
        if (!body.empty())
            return body;

        // only a rejected key justifies the rediscovery - rate limits keep the key
        if (st == 401 || st == 403)
        {
            sc_invalidate(cid);
            if (sc_client_id(cid))
                return call(base, cid, &st);
        }
        set_stream_err(st ? "api http " + std::to_string(st) : "api no response");
        return {};
    }

    std::string sc_resolve(const std::string& trans_url)
    {
        if (trans_url.empty())
            return {};

        std::string cid;
        if (!sc_client_id(cid))
        {
            set_stream_err("no soundcloud api key");
            return {};
        }

        int st = 0;
        std::string url = sc_resolve_cid(cid, trans_url, &st);
        if (!url.empty())
            return url;

        if (st == 401 || st == 403)
        {
            sc_invalidate(cid);
            if (sc_client_id(cid))
                return sc_resolve_cid(cid, trans_url, &st);
        }
        return {};
    }

    std::string sc_track_url(const std::string& body)
    {
        const std::string media = subobj(body, "media");
        if (media.empty())
            return {};
        return sc_pick_progressive(media);
    }

    dz_track parse_sc_track(const std::string& o)
    {
        dz_track t;
        t.provider = prov_soundcloud;
        t.id = get_num(o, "id");
        if (t.id > 0)
            t.src_id = std::to_string(t.id);
        t.needs_resolve = true;
        get_str(o, "title", t.title);
        t.duration = (int)(get_num(o, "duration") / 1000);

        const std::string pm = subobj(o, "publisher_metadata");
        if (pm.empty() || !get_str(pm, "artist", t.artist))
        {
            const std::string u = subobj(o, "user");
            if (!u.empty())
                get_str(u, "username", t.artist);
        }

        if (!get_str(o, "artwork_url", t.cover))
            get_str(o, "thumbnail_url", t.cover);

        const std::string media = subobj(o, "media");
        if (!media.empty())
            t.resolve_url = sc_pick_progressive(media);
        return t;
    }

    dz_playlist parse_sc_playlist(const std::string& o)
    {
        dz_playlist p;
        p.provider = prov_soundcloud;
        p.id = get_num(o, "id");
        if (p.id > 0)
            p.src_id = std::to_string(p.id);
        get_str(o, "title", p.title);
        p.track_count = (int)get_num(o, "track_count");
        get_str(o, "artwork_url", p.cover);
        return p;
    }

    dz_playlist parse_audius_playlist(const std::string& o)
    {
        dz_playlist p;
        p.provider = prov_audius;
        get_str(o, "id", p.src_id);
        if (!get_str(o, "playlist_name", p.title))
            get_str(o, "title", p.title);

        if (!p.src_id.empty())
        {
            unsigned long long h = 1469598103934665603ULL;
            for (unsigned char c : p.src_id)
            {
                h ^= (unsigned long long)c;
                h *= 1099511628211ULL;
            }
            p.id = (long long)((h & 0x3FFFFFFFFFFFFFFFULL) | 1ULL);
        }

        long long n = get_num(o, "track_count");
        if (n <= 0)
        {
            std::string raw;
            if (get_str(o, "track_count", raw))
                n = atoi(raw.c_str());
        }
        p.track_count = (int)n;

        const std::string aw = subobj(o, "artwork");
        if (!aw.empty())
        {
            if (!get_str(aw, "150x150", p.cover))
                get_str(aw, "480x480", p.cover);
        }
        return p;
    }

    // ---------------- youtube: full-length rescue ----------------

    const wchar_t* k_yt_ua =
        L"Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) "
        L"Chrome/131.0.0.0 Safari/537.36";

    const char* k_yt_key = "AIzaSyAO_FJ2SlqU8Q4STEHLGCilw_Y9_11qcW8";

    std::string yt_norm(const std::string& s)
    {
        std::string o;
        for (char c : s)
        {
            const unsigned char u = (unsigned char)c;
            if (isalnum(u))
                o.push_back((char)tolower(u));
        }
        return o;
    }

    std::vector<std::string> yt_search_ids(const std::string& query, int limit)
    {
        std::vector<std::string> ids;
        const std::string p = "/results?search_query=" + url_encode(query);
        const std::wstring wp(p.begin(), p.end());
        const std::string html = http_get(L"www.youtube.com", wp, nullptr, k_yt_ua);
        if (html.empty())
            return ids;

        const std::string key = "\"videoId\":\"";
        size_t pos = 0;
        while ((int)ids.size() < limit && (pos = html.find(key, pos)) != std::string::npos)
        {
            pos += key.size();
            if (pos + 11 > html.size())
                break;

            const std::string id = html.substr(pos, 11);
            bool clean = true;
            for (char c : id)
                if (!isalnum((unsigned char)c) && c != '-' && c != '_')
                    clean = false;

            if (clean && std::find(ids.begin(), ids.end(), id) == ids.end())
                ids.push_back(id);
        }
        return ids;
    }

    struct yt_stream
    {
        std::string url;
        std::string title;
        int seconds = 0;
    };

    bool yt_stream_for(const std::string& video_id, yt_stream& out, std::string& err)
    {
        const std::string body =
            "{\"context\":{\"client\":{\"clientName\":\"ANDROID\",\"clientVersion\":\"20.10.38\","
            "\"androidSdkVersion\":30,\"hl\":\"en\",\"gl\":\"US\"}},\"videoId\":\"" + video_id +
            "\",\"contentCheckOk\":true,\"racyCheckOk\":true}";

        const std::string path = std::string("/youtubei/v1/player?key=") + k_yt_key;
        const std::wstring wpath(path.begin(), path.end());

        std::string resp;
        int st = 0;
        if (!http_post_json(L"www.youtube.com", wpath, body, resp, &st, k_yt_ua) || resp.empty())
        {
            err = "youtube http " + std::to_string(st);
            return false;
        }

        const std::string sd = subobj(resp, "streamingData");
        if (sd.empty())
        {
            err = "youtube returned no streaming data";
            return false;
        }

        // adaptive renditions carry gir=yes and the edge refuses any range past ~1.1mb,
        // so only progressive (itag 18/22) or gir-less audio ever plays whole
        std::string best;
        int best_score = -1;
        for (const std::string& key : { std::string("formats"), std::string("adaptiveFormats") })
        {
            for (const auto& f : arr_objs(sd, key))
            {
                std::string mime, url;
                if (!get_str(f, "mimeType", mime) || !get_str(f, "url", url))
                    continue;
                if (url.rfind("http", 0) != 0 || url.find("gir=yes") != std::string::npos)
                    continue;

                const long long itag = get_num(f, "itag");
                int score = 10;
                if (itag == 18)
                    score = 100;
                else if (itag == 22)
                    score = 90;
                else if (mime.find("audio/mp4") != std::string::npos)
                    score = 50;
                else if (mime.find("audio/") != std::string::npos)
                    score = 40;

                if (score > best_score)
                {
                    best_score = score;
                    best = url;
                }
            }
        }

        if (best.empty())
        {
            err = "youtube only offered range-capped renditions";
            return false;
        }

        const std::string vd = subobj(resp, "videoDetails");
        if (!vd.empty())
            get_str(vd, "title", out.title);

        std::string len;
        if (!vd.empty() && get_str(vd, "lengthSeconds", len))
            out.seconds = atoi(len.c_str());
        if (out.seconds <= 0 && !vd.empty())
            out.seconds = (int)get_num(vd, "lengthSeconds");

        out.url = best;
        return true;
    }

    void yt_mf_startup()
    {
        static const bool once = SUCCEEDED(MFStartup(MF_VERSION, MFSTARTUP_FULL));
        (void)once;
    }

    bool yt_aac_to_wav(const std::vector<unsigned char>& mp4, std::vector<unsigned char>& wav,
                       std::string& err)
    {
        if (mp4.size() < 64)
        {
            err = "audio too small";
            return false;
        }

        yt_mf_startup();

        char temp[MAX_PATH] = {};
        GetTempPathA(MAX_PATH, temp);
        const std::string dir = std::string(temp) + "vade\\";
        CreateDirectoryA(dir.c_str(), nullptr);
        const std::string src = dir + "decode.m4a";

        {
            FILE* f = nullptr;
            if (fopen_s(&f, src.c_str(), "wb") != 0 || !f)
            {
                err = "cannot write temp audio";
                return false;
            }
            fwrite(mp4.data(), 1, mp4.size(), f);
            fclose(f);
        }

        wchar_t wsrc[MAX_PATH] = {};
        MultiByteToWideChar(CP_ACP, 0, src.c_str(), -1, wsrc, MAX_PATH);

        IMFSourceReader* reader = nullptr;
        HRESULT hr = MFCreateSourceReaderFromURL(wsrc, nullptr, &reader);
        if (FAILED(hr) || !reader)
        {
            err = "source reader " + std::to_string((long)hr);
            DeleteFileA(src.c_str());
            return false;
        }

        reader->SetStreamSelection(MF_SOURCE_READER_ALL_STREAMS, FALSE);
        reader->SetStreamSelection(MF_SOURCE_READER_FIRST_AUDIO_STREAM, TRUE);

        IMFMediaType* pcm = nullptr;
        MFCreateMediaType(&pcm);
        if (pcm)
        {
            pcm->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio);
            pcm->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_PCM);
            pcm->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS, 2);
            pcm->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, 44100);
            pcm->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, 16);
            pcm->SetUINT32(MF_MT_AUDIO_BLOCK_ALIGNMENT, 4);
            pcm->SetUINT32(MF_MT_AUDIO_AVG_BYTES_PER_SECOND, 44100 * 4);
            hr = reader->SetCurrentMediaType(MF_SOURCE_READER_FIRST_AUDIO_STREAM, nullptr, pcm);
            pcm->Release();
        }
        if (FAILED(hr))
        {
            err = "cannot negotiate pcm " + std::to_string((long)hr);
            reader->Release();
            DeleteFileA(src.c_str());
            return false;
        }

        DWORD channels = 2, rate = 44100, bits = 16;
        IMFMediaType* out_type = nullptr;
        if (SUCCEEDED(reader->GetCurrentMediaType(MF_SOURCE_READER_FIRST_AUDIO_STREAM, &out_type)) &&
            out_type)
        {
            UINT32 v = 0;
            if (SUCCEEDED(out_type->GetUINT32(MF_MT_AUDIO_NUM_CHANNELS, &v)) && v) channels = v;
            v = 0;
            if (SUCCEEDED(out_type->GetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, &v)) && v) rate = v;
            v = 0;
            if (SUCCEEDED(out_type->GetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, &v)) && v) bits = v;
            out_type->Release();
        }

        {
            char fl[160];
            sprintf_s(fl, "YT   out ch=%d rate=%d bits=%d", (int)channels, (int)rate, (int)bits);
            stream_log(fl);
        }

        std::vector<unsigned char> pcm_data;
        for (;;)
        {
            DWORD stream_index = 0, flags = 0;
            LONGLONG timestamp = 0;
            IMFSample* sample = nullptr;

            hr = reader->ReadSample(MF_SOURCE_READER_FIRST_AUDIO_STREAM, 0, &stream_index, &flags,
                                    &timestamp, &sample);
            if (FAILED(hr))
            {
                err = "read sample " + std::to_string((long)hr);
                break;
            }
            if ((flags & MF_SOURCE_READERF_ENDOFSTREAM) || !sample)
            {
                if (sample)
                    sample->Release();
                break;
            }

            IMFMediaBuffer* buffer = nullptr;
            if (SUCCEEDED(sample->ConvertToContiguousBuffer(&buffer)) && buffer)
            {
                DWORD cur = 0;
                buffer->GetCurrentLength(&cur);

                BYTE* data = nullptr;
                DWORD len = 0;
                if (SUCCEEDED(buffer->Lock(&data, &len, nullptr)) && data && len)
                {
                    // Lock reports the buffer ceiling, not the valid bytes
                    if (cur && cur < len)
                        len = cur;
                    pcm_data.insert(pcm_data.end(), data, data + len);
                    buffer->Unlock();
                }
                buffer->Release();
            }
            sample->Release();

            if (pcm_data.size() > (size_t)256 * 1024 * 1024)
            {
                err = "decoded audio too large";
                break;
            }
        }

        reader->Release();
        DeleteFileA(src.c_str());

        if (pcm_data.size() < 1024)
        {
            if (err.empty())
                err = "no pcm produced";
            return false;
        }

        const DWORD byte_rate = rate * channels * (bits / 8);
        const WORD block_align = (WORD)(channels * (bits / 8));

        wav.assign(44 + pcm_data.size(), 0);
        unsigned char* w = wav.data();
        auto wr32 = [&](size_t o, DWORD v)
        {
            w[o] = (unsigned char)(v & 255);
            w[o + 1] = (unsigned char)((v >> 8) & 255);
            w[o + 2] = (unsigned char)((v >> 16) & 255);
            w[o + 3] = (unsigned char)((v >> 24) & 255);
        };
        auto wr16 = [&](size_t o, WORD v)
        {
            w[o] = (unsigned char)(v & 255);
            w[o + 1] = (unsigned char)((v >> 8) & 255);
        };

        memcpy(w, "RIFF", 4);
        wr32(4, (DWORD)(36 + pcm_data.size()));
        memcpy(w + 8, "WAVEfmt ", 8);
        wr32(16, 16);
        wr16(20, 1);
        wr16(22, (WORD)channels);
        wr32(24, rate);
        wr32(28, byte_rate);
        wr16(32, block_align);
        wr16(34, (WORD)bits);
        memcpy(w + 36, "data", 4);
        wr32(40, (DWORD)pcm_data.size());
        memcpy(w + 44, pcm_data.data(), pcm_data.size());
        return true;
    }

    bool playable_track(const dz_track& t)
    {
        return !t.preview.empty() || !t.resolve_url.empty() || !t.src_id.empty();
    }
}

void c_deezer::search(const std::string& q)
{
    if (loading || q.empty())
        return;

    loading = true;
    has_results = false;
    query = q;
    selected_user_playlist = -1;

    const std::string enc = url_encode(q);
    const int prov = provider;

    std::thread([this, enc, prov]()
    {
        std::vector<dz_track> out;
        try
        {
            const std::wstring wenc(enc.begin(), enc.end());

            if (prov == prov_soundcloud)
            {
                const std::string body = sc_get("/search/tracks?q=" + enc + "&limit=40");

                if (!body.empty())
                {
                    for (const auto& o : arr_objs(body, "collection"))
                    {
                        dz_track t = parse_sc_track(o);
                        if (!t.title.empty() && !t.resolve_url.empty())
                            out.push_back(std::move(t));
                    }
                }
            }
            else if (prov == prov_audius)
            {
                const std::string body = http_get(L"api.audius.co",
                    L"/v1/tracks/search?query=" + wenc + L"&limit=40&app_name=vade");

                if (!body.empty())
                {
                    for (const auto& o : arr_objs(body, "data"))
                    {
                        dz_track t = parse_audius_track(o);
                        if (!t.title.empty() && !t.preview.empty())
                            out.push_back(std::move(t));
                    }
                }
            }
            else
            {
                const std::string body = http_get(L"api.deezer.com", L"/search?q=" + wenc);

                if (!body.empty())
                {
                    for (const auto& o : arr_objs(body, "data"))
                    {
                        dz_track t = parse_track(o);
                        if (!t.title.empty())
                            out.push_back(std::move(t));
                    }
                }
            }
        }
        catch (...) {}

        std::lock_guard<std::mutex> lock(mtx);
        pending_tracks = std::move(out);
        tracks_pending = true;
    }).detach();
}

void c_deezer::load_charts()
{
    if (loading)
        return;

    loading = true;
    has_results = false;
    list_title = "charts";
    selected_user_playlist = -1;

    static int sc_rot = 0;
    const int sc_base = sc_rot;
    sc_rot += 2;

    const int prov = provider;

    std::thread([this, prov, sc_base]()
    {
        std::vector<dz_track> out;
        std::vector<dz_playlist> lists;
        std::vector<std::string> seen;
        try
        {
            auto push_track = [&](dz_track t)
            {
                if (t.title.empty())
                    return;
                if (t.provider == prov_soundcloud && t.resolve_url.empty())
                    return;
                if (!t.src_id.empty())
                {
                    if (std::find(seen.begin(), seen.end(), t.src_id) != seen.end())
                        return;
                    seen.push_back(t.src_id);
                }
                out.push_back(std::move(t));
            };

            if (prov == prov_soundcloud)
            {
                static const char* kHot[] = {
                    "new music", "hip hop", "rap", "chill", "electronic",
                    "house", "techno", "lofi", "drill", "r&b"
                };
                const int hot_n = (int)(sizeof(kHot) / sizeof(kHot[0]));

                for (int k = 0; k < 4; ++k)
                {
                    const std::string q = kHot[(sc_base + k) % hot_n];
                    const std::string body =
                        sc_get("/search/tracks?q=" + url_encode(q) + "&limit=30");
                    for (const auto& o : arr_objs(body, "collection"))
                        push_track(parse_sc_track(o));
                }

                for (int k = 0; k < 2 && lists.size() < 16; ++k)
                {
                    const std::string pbody = sc_get("/search/playlists?q=" +
                                                     url_encode(kHot[(sc_base + 4 + k) % hot_n]) +
                                                     "&limit=15");
                    for (const auto& o : arr_objs(pbody, "collection"))
                    {
                        dz_playlist p = parse_sc_playlist(o);
                        if (!p.title.empty())
                            lists.push_back(std::move(p));
                    }
                    if (lists.empty())
                    {
                        for (const auto& o : arr_objs(pbody, "data"))
                        {
                            dz_playlist p = parse_sc_playlist(o);
                            if (!p.title.empty())
                                lists.push_back(std::move(p));
                        }
                    }
                }
            }
            else if (prov == prov_audius)
            {
                const std::string body = http_get(L"api.audius.co",
                    L"/v1/tracks/trending?app_name=vade&limit=40");
                for (const auto& o : arr_objs(body, "data"))
                    push_track(parse_audius_track(o));

                const std::string pbody = http_get(L"api.audius.co",
                    L"/v1/playlists/trending?app_name=vade&limit=12");
                for (const auto& o : arr_objs(pbody, "data"))
                {
                    dz_playlist p = parse_audius_playlist(o);
                    if (!p.title.empty())
                        lists.push_back(std::move(p));
                }
            }
            else
            {
                const std::string body = http_get(L"api.deezer.com", L"/chart");

                if (!body.empty())
                {
                    const std::string tracks = subobj(body, "tracks");
                    for (const auto& o : arr_objs(tracks, "data"))
                    {
                        dz_track t = parse_track(o);
                        if (!t.title.empty())
                            out.push_back(std::move(t));
                    }

                    const std::string playlists = subobj(body, "playlists");
                    for (const auto& o : arr_objs(playlists, "data"))
                    {
                        dz_playlist p = parse_playlist(o);
                        if (!p.title.empty())
                            lists.push_back(std::move(p));
                    }
                }
            }
        }
        catch (...) {}

        std::lock_guard<std::mutex> lock(mtx);
        pending_tracks = std::move(out);
        tracks_pending = true;
        if (!lists.empty())
        {
            pending_playlists = std::move(lists);
            playlists_pending = true;
        }
    }).detach();
}

void c_deezer::load_playlists()
{
    if (loading)
        return;

    loading = true;
    has_playlists = false;
    selected_user_playlist = -1;

    const int prov = provider;

    std::thread([this, prov]()
    {
        std::vector<dz_playlist> out;
        try
        {
            if (prov == prov_soundcloud)
            {
                static const char* kQ[] = { "hip hop", "chill", "electronic", "pop", "gaming" };
                const std::string q = kQ[(int)((GetTickCount64() / 1000ULL) % 5ULL)];

                const std::string body =
                    sc_get("/search/playlists?q=" + url_encode(q) + "&limit=24");
                for (const auto& o : arr_objs(body, "collection"))
                {
                    dz_playlist p = parse_sc_playlist(o);
                    if (!p.title.empty())
                        out.push_back(std::move(p));
                }
            }
            else if (prov == prov_audius)
            {
                const std::string body = http_get(L"api.audius.co",
                    L"/v1/playlists/trending?app_name=vade&limit=24");
                for (const auto& o : arr_objs(body, "data"))
                {
                    dz_playlist p = parse_audius_playlist(o);
                    if (!p.title.empty())
                        out.push_back(std::move(p));
                }
            }
            else
            {
                const std::string body = http_get(L"api.deezer.com", L"/chart");

                if (!body.empty())
                {
                    const std::string lists = subobj(body, "playlists");
                    for (const auto& o : arr_objs(lists, "data"))
                    {
                        dz_playlist p = parse_playlist(o);
                        if (!p.title.empty())
                            out.push_back(std::move(p));
                    }
                }
            }
        }
        catch (...) {}

        std::lock_guard<std::mutex> lock(mtx);
        pending_playlists = std::move(out);
        playlists_pending = true;
    }).detach();
}

void c_deezer::load_playlist(const dz_playlist& pl)
{
    if (loading)
        return;

    loading = true;
    has_results = false;
    selected_playlist = -1;
    selected_user_playlist = -1;

    const int prov = pl.provider;
    const long long dz_id = pl.id;
    const std::string sid = pl.src_id;
    const std::string hint = pl.title;

    std::thread([this, prov, dz_id, sid, hint]()
    {
        std::vector<dz_track> out;
        std::string title = hint;
        try
        {
            if (prov == prov_soundcloud && !sid.empty())
            {
                const std::string body = sc_get("/playlists/" + sid);

                if (!body.empty())
                {
                    if (title.empty())
                        get_str(body, "title", title);

                    for (const auto& o : arr_objs(body, "tracks"))
                    {
                        dz_track t = parse_sc_track(o);
                        if (!t.title.empty())
                            out.push_back(std::move(t));
                    }
                }
            }
            else if (prov == prov_audius && !sid.empty())
            {
                const std::wstring wpath = L"/v1/playlists/" +
                    std::wstring(sid.begin(), sid.end()) +
                    L"/tracks?app_name=vade&limit=200";
                const std::string body = http_get(L"api.audius.co", wpath);

                if (!body.empty())
                {
                    for (const auto& o : arr_objs(body, "data"))
                    {
                        dz_track t = parse_audius_track(o);
                        if (!t.title.empty() && !t.preview.empty())
                            out.push_back(std::move(t));
                    }
                }
            }
            else if (dz_id > 0)
            {
                const std::string path = "/playlist/" + std::to_string(dz_id);
                const std::wstring wpath(path.begin(), path.end());
                const std::string body = http_get(L"api.deezer.com", wpath);

                if (!body.empty())
                {
                    if (title.empty())
                        get_str(body, "title", title);

                    const std::string tracks = subobj(body, "tracks");
                    for (const auto& o : arr_objs(tracks, "data"))
                    {
                        dz_track t = parse_track(o);
                        if (!t.title.empty())
                            out.push_back(std::move(t));
                    }
                }
            }
        }
        catch (...) {}

        std::lock_guard<std::mutex> lock(mtx);
        pending_tracks = std::move(out);
        tracks_pending = true;
        if (!title.empty())
        {
            pending_title = title;
            title_pending = true;
        }
    }).detach();
}

void c_deezer::play(int index)
{
    if (index < 0 || index >= (int)results.size())
        return;

    queue = results;
    queue_index = index;

    if (!playable_track(queue[index]))
    {
        const int clicked = index;
        int i = index;
        for (int n = 0; n < (int)queue.size(); ++n)
        {
            i = (i + 1) % (int)queue.size();
            if (playable_track(queue[i]))
                break;
        }

        if (!playable_track(queue[i]) || i == clicked)
        {
            stream_log("REJ  clicked=" + std::to_string(clicked) + " nothing playable");
            status = "no stream available";
            return;
        }

        queue_index = i;
        stream_log("REJ  clicked=" + std::to_string(clicked) + " no stream -> idx=" +
                   std::to_string(i));
        status = "that row has no stream - playing the next one";
    }

    play_track(queue[queue_index], queue, queue_index);
}

void c_deezer::play_queue_at(int index)
{
    if (queue.empty())
        return;
    if (index < 0 || index >= (int)queue.size())
        return;
    if (index == queue_index && playing)
        return;

    queue_index = index;
    play_track(queue[index], queue, index);
}

void c_deezer::play_track(const dz_track& t, const std::vector<dz_track>& source, int index)
{
    if (!playable_track(t))
    {
        stream_log("REJ  idx=" + std::to_string(index) + " unplayable in play_track");
        status = "no stream available";
        return;
    }

    active = true;
    downloading = true;
    ended = false;
    user_paused = false;
    playing = false;
    loaded = false;
    pos_ms = 0;
    len_ms = 0;
    now_title = t.title;
    now_artist = t.artist;
    now_album = t.album;
    status = t.provider == prov_deezer ? "loading preview..." : "loading track...";

    audio::stop();

    {
        char pl[512];
        sprintf_s(pl, "PLAY idx=%d prov=%d src=%s rev_len=%d title=%.120s",
                  index, t.provider, t.src_id.c_str(),
                  (int)t.resolve_url.size(), t.title.c_str());
        stream_log(pl);
    }

    std::thread([this, t, source, index]()
        {
            set_stream_err("");
            dz_track track = t;
        std::vector<unsigned char> audio_bytes;
        try
        {
            char temp[MAX_PATH] = {};
            GetTempPathA(MAX_PATH, temp);

            const std::string dir = std::string(temp) + "vade\\";
            CreateDirectoryA(dir.c_str(), nullptr);

            const std::string cover = dir +
                (track.src_id.empty() ? std::to_string(track.id)
                                      : std::string("a") + track.src_id) + ".jpg";

            const bool soundcloud = (track.provider == prov_soundcloud);
            const bool audius = (track.provider == prov_audius);

            auto norm_txt = [](const std::string& s)
            {
                std::string o;
                for (char c : s)
                {
                    const unsigned char u = (unsigned char)c;
                    if (isalnum(u))
                        o.push_back((char)tolower(u));
                }
                return o;
            };

            // soundcloud retires plain mp3 renditions for ad-supported tracks.
            // first rescue is the whole song from youtube, last resort the 30s preview.
            bool yt_tried = false;
            auto open_fallback = [&](bool allow_yt) -> bool
            {
                const std::string want_t = norm_txt(track.title);
                const std::string want_a = norm_txt(track.artist);
                if (want_t.empty())
                    return false;

                if (allow_yt)
                {
                    yt_tried = true;
                    const std::string q = track.title +
                                          (track.artist.empty() ? "" : " " + track.artist);
                    yt_stream best;
                    bool got = false;

                    for (const auto& vid : yt_search_ids(q, 6))
                    {
                        yt_stream cand;
                        std::string e;
                        if (!yt_stream_for(vid, cand, e) || cand.url.empty())
                            continue;

                        const std::string nt = norm_txt(cand.title);
                        const bool match = nt.empty() || want_t.empty() ||
                                           nt.find(want_t) != std::string::npos ||
                                           want_t.find(nt) != std::string::npos;

                        if (match)
                        {
                            best = cand;
                            got = true;
                            break;
                        }
                        if (!got)
                            best = cand;
                        got = true;
                    }

                    if (got && !best.url.empty())
                    {
                        track.preview = best.url;
                        track.mirrors.clear();
                        set_stream_err("");
                        status = "soundcloud closed this stream - playing the full track";
                        stream_log("FB   youtube '" + best.title + "' " +
                                   std::to_string(best.seconds) + "s");
                        {
                            std::string u = best.url;
                            if (u.size() > 4000)
                                u.resize(4000);
                            stream_log("URL  " + u);
                        }
                        return true;
                    }
                }

                const std::string q = url_encode(track.title + " " + track.artist);
                const std::string req = "/search?q=" + q + "&limit=10";
                const std::wstring wreq(req.begin(), req.end());
                const std::string body = http_get(L"api.deezer.com", wreq);
                if (body.empty())
                    return false;

                std::string best_title, best_preview;
                for (const auto& o : arr_objs(body, "data"))
                {
                    std::string ti, pv, ar;
                    if (!get_str(o, "title", ti) || !get_str(o, "preview", pv) || pv.empty())
                        continue;

                    const std::string ao = subobj(o, "artist");
                    if (!ao.empty())
                        get_str(ao, "name", ar);

                    const bool t_ok = (norm_txt(ti) == want_t);
                    const bool a_ok = want_a.empty() || norm_txt(ar) == want_a;
                    if (t_ok && a_ok)
                    {
                        best_title = ti;
                        best_preview = pv;
                        break;
                    }
                    if (best_preview.empty() && t_ok)
                    {
                        best_title = ti;
                        best_preview = pv;
                    }
                }

                if (best_preview.empty())
                    return false;

                track.preview = best_preview;
                track.mirrors.clear();
                set_stream_err("");
                status = "soundcloud closed this stream - playing a 30s preview";
                stream_log("FB   deezer preview '" + best_title + "'");
                return true;
            };

            if (soundcloud)
            {
                std::string trans = track.resolve_url;
                if (trans.empty() && !track.src_id.empty())
                {
                    const std::string body = sc_get("/tracks/" + track.src_id);
                    if (!body.empty())
                        trans = sc_track_url(body);
                }

                if (trans.empty())
                    set_stream_err(track.src_id.empty()
                                       ? "track carries no soundcloud id"
                                       : "no progressive transcode on this track");

                std::string fresh = sc_resolve(trans);

                if (fresh.empty() && !track.src_id.empty())
                {
                    const std::string body = sc_get("/tracks/" + track.src_id);
                    const std::string alt = sc_track_url(body);
                    if (!alt.empty() && alt != trans)
                    {
                        fresh = sc_resolve(alt);
                        if (!fresh.empty())
                            track.resolve_url = alt;
                    }
                }

                if (!fresh.empty())
                    track.preview = fresh;
            }
            else if (audius && track.needs_resolve && !track.src_id.empty())
            {
                const std::wstring wpath = L"/v1/tracks/" +
                    std::wstring(track.src_id.begin(), track.src_id.end()) + L"?app_name=vade";
                const std::string body = http_get(L"api.audius.co", wpath);

                if (!body.empty())
                {
                    const std::string st = subobj(body, "stream");
                    if (!st.empty())
                    {
                        std::string fresh;
                        if (get_str(st, "url", fresh) && !fresh.empty())
                            track.preview = fresh;
                        std::vector<std::string> m = arr_str(st, "mirrors");
                        if (!m.empty())
                            track.mirrors = std::move(m);
                    }
                    if (track.cover.empty())
                    {
                        const std::string aw = subobj(body, "artwork");
                        if (!aw.empty())
                            get_str(aw, "480x480", track.cover);
                    }
                }
            }
            else if (!audius && track.needs_resolve && track.id > 0)
            {
                const std::string req = "/track/" + std::to_string(track.id);
                const std::wstring wreq(req.begin(), req.end());
                const std::string body = http_get(L"api.deezer.com", wreq);

                if (!body.empty())
                {
                    std::string fresh;
                    if (get_str(body, "preview", fresh) && !fresh.empty())
                        track.preview = fresh;
                    if (track.cover.empty())
                    {
                        const std::string al = subobj(body, "album");
                        if (!al.empty())
                            get_str(al, "cover_medium", track.cover);
                    }
                }
            }

            std::wstring host, path;

            if (track.preview.empty() && soundcloud)
                open_fallback(!yt_tried);

            if (track.preview.empty() && get_stream_err().empty())
                set_stream_err("no stream url for this track");

            if (!track.preview.empty() && split_url(track.preview, host, path))
            {
                const size_t kMaxAudio = (size_t)48 * 1024 * 1024;

                auto try_host = [&](const std::wstring& h) -> bool
                {
                    if (h.empty())
                        return false;
                    audio_bytes.clear();
                    int st = 0;
                    const bool yt_host = h.find(L"googlevideo.com") != std::wstring::npos ||
                                         h.find(L"youtube.com") != std::wstring::npos;
                    const wchar_t* ua = yt_host ? k_yt_ua : L"Vade/1.0";
                    const bool gir = yt_host && path.find(L"gir=yes") != std::wstring::npos;
                    // gir renditions 403 an open-ended range and cap the end at ~1.1mb
                    const wchar_t* hdrs = yt_host
                        ? (gir ? L"Range: bytes=0-1126399\r\n"
                                L"Referer: https://www.youtube.com/\r\n"
                                L"Origin: https://www.youtube.com\r\n"
                              : L"Referer: https://www.youtube.com/\r\n"
                                L"Origin: https://www.youtube.com\r\n")
                        : nullptr;
                    const bool ok = http_get_bytes(h, path, audio_bytes, kMaxAudio, &st, ua, hdrs);
                    if (!ok || audio_bytes.size() < 2048)
                        set_stream_err(st ? "cdn http " + std::to_string(st)
                                          : "cdn no response");
                    return ok;
                };

                if (!try_host(host))
                {
                    for (const auto& m : track.mirrors)
                    {
                        std::wstring mh, mp;
                        if (!split_url(m, mh, mp))
                            continue;
                        if (try_host(mh))
                            break;
                    }
                }

                // one more shot with a freshly minted stream url before we give up the row
                if (audio_bytes.size() < 2048 && soundcloud && !track.src_id.empty())
                {
                    const std::string detail = sc_get("/tracks/" + track.src_id);
                    const std::string fresh_trans = sc_track_url(detail);
                    const std::string fresh_url =
                        fresh_trans.empty() ? std::string() : sc_resolve(fresh_trans);

                    if (!fresh_url.empty() && split_url(fresh_url, host, path))
                    {
                        try_host(host);
                        for (const auto& m : track.mirrors)
                        {
                            if (audio_bytes.size() >= 2048)
                                break;
                            std::wstring mh, mp;
                            if (!split_url(m, mh, mp))
                                continue;
                            if (try_host(mh))
                                break;
                        }
                    }
                }

                // soundcloud retired this one - fall back to the open preview and fetch it
                if (audio_bytes.size() < 2048 && soundcloud && open_fallback(!yt_tried) &&
                    split_url(track.preview, host, path))
                {
                    if (!try_host(host))
                    {
                        for (const auto& m : track.mirrors)
                        {
                            if (audio_bytes.size() >= 2048)
                                break;
                            std::wstring mh, mp;
                            if (!split_url(m, mh, mp))
                                continue;
                            if (try_host(mh))
                                break;
                        }
                    }
                }

                if (audio_bytes.size() < 2048)
                    audio_bytes.clear();
            }

            if (GetFileAttributesA(cover.c_str()) == INVALID_FILE_ATTRIBUTES && split_url(track.cover, host, path))
            {
                http_fetch(host, path, nullptr, cover.c_str());

                if (GetFileAttributesA(cover.c_str()) == INVALID_FILE_ATTRIBUTES)
                {
                    for (const auto& m : track.cover_mirrors)
                    {
                        std::wstring mh, mp;
                        if (!split_url(m, mh, mp))
                            continue;
                        http_fetch(mh, path, nullptr, cover.c_str());
                        if (GetFileAttributesA(cover.c_str()) != INVALID_FILE_ATTRIBUTES)
                            break;
                    }
                }
            }

            // youtube hands back a plain m4a - turn it into wav for the decoder
            if (audio_bytes.size() > 64 && audio_bytes.size() < (size_t)48 * 1024 * 1024 &&
                audio_bytes[4] == 'f' && audio_bytes[5] == 't' &&
                audio_bytes[6] == 'y' && audio_bytes[7] == 'p')
            {
                std::vector<unsigned char> wav;
                std::string derr;
                if (yt_aac_to_wav(audio_bytes, wav, derr))
                {
                    char dl[160];
                    sprintf_s(dl, "YT   decoded m4a -> wav %d bytes", (int)wav.size());
                    stream_log(dl);
                    audio_bytes.swap(wav);
                }
                else
                {
                    stream_log("YT   decode failed: " + derr);
                    set_stream_err("audio decode: " + derr);
                    audio_bytes.clear();
                }
            }

            {
                std::lock_guard<std::mutex> lock(mtx);
                strncpy_s(cover_path, cover.c_str(), _TRUNCATE);
                pending_audio = std::move(audio_bytes);
                pending_queue = source;
                pending_index = index;
                ready_to_play = true;
            }
        }
        catch (...) {}
        downloading = false;
    }).detach();
}

std::string c_deezer::user_pl_path()
{
    char base[MAX_PATH] = {};
    const DWORD n = GetEnvironmentVariableA("APPDATA", base, MAX_PATH);

    std::string dir;
    if (n > 0 && n < MAX_PATH)
        dir = std::string(base) + "\\Vade\\";
    else
    {
        char temp[MAX_PATH] = {};
        GetTempPathA(MAX_PATH, temp);
        dir = std::string(temp) + "vade\\";
    }

    CreateDirectoryA(dir.c_str(), nullptr);
    return dir + "playlists.cfg";
}

namespace
{
    std::string one_line(const std::string& s)
    {
        std::string out = s;
        for (char& c : out)
            if ((unsigned char)c < 0x20 || c == '\x1f')
                c = ' ';
        return out;
    }

    void split_fields(const std::string& body, std::vector<std::string>& out)
    {
        size_t p = 0;
        while (true)
        {
            const size_t n = body.find('\x1f', p);
            if (n == std::string::npos)
            {
                out.push_back(body.substr(p));
                break;
            }
            out.push_back(body.substr(p, n - p));
            p = n + 1;
        }
    }

    std::string join_list(const std::vector<std::string>& v)
    {
        std::string out;
        for (size_t i = 0; i < v.size(); ++i)
        {
            if (i)
                out += ',';
            out += v[i];
        }
        return out;
    }

    void split_list(const std::string& body, std::vector<std::string>& out)
    {
        out.clear();
        size_t p = 0;
        while (p < body.size())
        {
            const size_t n = body.find(',', p);
            if (n == std::string::npos)
            {
                if (p < body.size())
                    out.push_back(body.substr(p));
                break;
            }
            if (n > p)
                out.push_back(body.substr(p, n - p));
            p = n + 1;
        }
    }
}

void c_deezer::ensure_user_playlists()
{
    if (user_pl_loaded)
        return;
    user_pl_loaded = true;

    FILE* f = nullptr;
    if (fopen_s(&f, user_pl_path().c_str(), "rb") != 0 || !f)
        return;

    char line[16384];
    while (fgets(line, sizeof(line), f))
    {
        std::string s = line;
        while (!s.empty() && (s.back() == '\n' || s.back() == '\r'))
            s.pop_back();

        if (s.rfind("pl=", 0) == 0)
        {
            user_playlists.push_back(dz_user_playlist{ s.substr(3), {} });
            continue;
        }
        if (s.rfind("tr=", 0) == 0 && !user_playlists.empty())
        {
            std::vector<std::string> fields;
            split_fields(s.substr(3), fields);
            if (fields.size() < 7)
                continue;

            dz_track t;
            t.id = _strtoi64(fields[0].c_str(), nullptr, 10);
            t.title = fields[1];
            t.artist = fields[2];
            t.album = fields[3];
            t.cover = fields[4];
            t.preview = fields[5];
            t.duration = atoi(fields[6].c_str());

            if (fields.size() >= 11)
            {
                t.provider = atoi(fields[7].c_str());
                t.src_id = fields[8];
                split_list(fields[9], t.mirrors);
                split_list(fields[10], t.cover_mirrors);
            }
            t.needs_resolve = true;

            user_playlists.back().tracks.push_back(std::move(t));
        }
    }
    fclose(f);
}

void c_deezer::save_user_playlists()
{
    FILE* f = nullptr;
    if (fopen_s(&f, user_pl_path().c_str(), "wb") != 0 || !f)
        return;

    const char fs = '\x1f';
    for (const auto& pl : user_playlists)
    {
        fprintf(f, "pl=%s\n", one_line(pl.name).c_str());
        for (const auto& t : pl.tracks)
            fprintf(f, "tr=%lld%c%s%c%s%c%s%c%s%c%s%c%d%c%d%c%s%c%s%c%s\n",
                    t.id, fs, one_line(t.title).c_str(), fs,
                    one_line(t.artist).c_str(), fs, one_line(t.album).c_str(), fs,
                    one_line(t.cover).c_str(), fs, one_line(t.preview).c_str(), fs,
                    t.duration, fs,
                    t.provider, fs, one_line(t.src_id).c_str(), fs,
                    one_line(join_list(t.mirrors)).c_str(), fs,
                    one_line(join_list(t.cover_mirrors)).c_str());
    }
    fclose(f);
}

int c_deezer::create_user_playlist(const std::string& name)
{
    ensure_user_playlists();

    std::string n = name;
    while (!n.empty() && n.front() == ' ')
        n.erase(n.begin());
    while (!n.empty() && n.back() == ' ')
        n.pop_back();
    if (n.empty())
        return -1;

    for (const auto& pl : user_playlists)
        if (pl.name == n)
            return -1;

    user_playlists.push_back(dz_user_playlist{ n, {} });
    save_user_playlists();
    return (int)user_playlists.size() - 1;
}

void c_deezer::delete_user_playlist(int index)
{
    ensure_user_playlists();
    if (index < 0 || index >= (int)user_playlists.size())
        return;

    user_playlists.erase(user_playlists.begin() + index);
    save_user_playlists();

    if (selected_user_playlist == index)
    {
        selected_user_playlist = -1;
        results.clear();
        has_results = false;
        list_title = "search results";
    }
    else if (selected_user_playlist > index)
        selected_user_playlist--;
}

bool c_deezer::add_to_user_playlist(int index, const dz_track& t)
{
    ensure_user_playlists();
    if (index < 0 || index >= (int)user_playlists.size())
        return false;
    if (t.id <= 0 && t.src_id.empty())
        return false;

    for (const auto& e : user_playlists[index].tracks)
        if (e.id == t.id && e.src_id == t.src_id)
            return false;

    dz_track copy = t;
    copy.needs_resolve = true;
    user_playlists[index].tracks.push_back(std::move(copy));
    save_user_playlists();
    return true;
}

void c_deezer::remove_from_user_playlist(int index, int track_index)
{
    ensure_user_playlists();
    if (index < 0 || index >= (int)user_playlists.size())
        return;

    auto& tracks = user_playlists[index].tracks;
    if (track_index < 0 || track_index >= (int)tracks.size())
        return;

    tracks.erase(tracks.begin() + track_index);
    save_user_playlists();

    if (selected_user_playlist == index)
    {
        results = tracks;
        if (queue_index > track_index)
            queue_index--;
        if (queue_index >= (int)results.size())
            queue_index = (int)results.size() - 1;
    }
}

void c_deezer::open_user_playlist(int index)
{
    ensure_user_playlists();
    if (index < 0 || index >= (int)user_playlists.size())
        return;

    selected_user_playlist = index;
    selected_id = -1;
    selected_playlist = -1;
    list_title = user_playlists[index].name;
    results = user_playlists[index].tracks;
    has_results = true;
    loading = false;
}

void c_deezer::pump()
{
    std::vector<dz_track> tracks;
    std::vector<dz_playlist> lists;
    bool got_tracks = false;
    bool got_lists = false;

    bool ready = false;
    std::vector<unsigned char> audio;

    {
        std::lock_guard<std::mutex> lock(mtx);

        if (tracks_pending)
        {
            tracks = std::move(pending_tracks);
            tracks_pending = false;
            got_tracks = true;
        }
        if (playlists_pending)
        {
            lists = std::move(pending_playlists);
            playlists_pending = false;
            got_lists = true;
        }
        if (title_pending)
        {
            list_title = std::move(pending_title);
            title_pending = false;
        }
        if (ready_to_play)
        {
            audio = std::move(pending_audio);
            pending_audio.clear();
            ready_to_play = false;
            ready = true;
            queue = std::move(pending_queue);
            queue_index = pending_index;
        }
    }

    if (got_tracks)
    {
        results = std::move(tracks);
        has_results = true;
        loading = false;
    }
    if (got_lists)
    {
        playlists = std::move(lists);
        has_playlists = true;
        loading = false;
    }
    if (ready)
        start_playback(std::move(audio));
}

void c_deezer::start_playback(std::vector<unsigned char> bytes)
{
    if (bytes.size() < 2048)
    {
        downloading = false;

        if (queue_index >= 0 && queue_index < (int)queue.size())
        {
            dz_track& bad = queue[queue_index];
            if (bad.provider != prov_deezer)
            {
                bad.preview.clear();
                bad.resolve_url.clear();
            }
        }

        const std::string err = get_stream_err();
        stream_log("BLK  bytes=" + std::to_string(bytes.size()) + " err=" + err);
        if (++fail_streak >= 4)
        {
            fail_streak = 0;
            sc_backoff(4000);
            status = err.empty() ? "all streams blocked - backing off 4s"
                                 : "all streams blocked: " + err;
            return;
        }

        status = err.empty() ? "stream blocked - skipping"
                             : "stream blocked: " + err + " - skipping";
        advance();
        return;
    }

    if (!audio::init())
    {
        stream_log("AUD  engine init failed");
        status = "audio device unavailable - try play again";
        downloading = false;
        return;
    }

    const char* perr = audio::play(bytes);

    if (perr || !audio::playing())
    {
        stream_log(perr ? std::string("AUD  play failed: ") + perr
                        : std::string("AUD  engine reports not playing"));
        status = "playback failed";
        downloading = false;
        return;
    }

    loaded = true;
    user_paused = false;
    playing = true;
    ended = false;
    pos_ms = 0;
    last_poll = 0;
    fail_streak = 0;
    len_ms = (int)(audio::duration_sec() * 1000.0);
    downloading = false;
    status.clear();
    set_stream_err("");
    stream_log("OK   idx=" + std::to_string(queue_index) + " len=" + std::to_string(len_ms));
}

void c_deezer::advance()
{
    if (queue.empty())
        return;

    for (int attempt = 0; attempt < (int)queue.size(); ++attempt)
    {
        int next_index = queue_index + 1 + attempt;
        if (next_index >= (int)queue.size())
            next_index %= (int)queue.size();

        if (!playable_track(queue[next_index]))
        {
            stream_log("SKIP unplayable idx=" + std::to_string(next_index));
            continue;
        }

        queue_index = next_index;
        play_track(queue[next_index], queue, next_index);
        return;
    }
}

void c_deezer::update()
{
    pump();

    if (!loaded)
        return;

    const uint64_t now = GetTickCount64();
    if (last_poll == 0 || now - last_poll > 150)
    {
        last_poll = now;

        pos_ms = (int)(audio::position_sec() * 1000.0);

        if (len_ms <= 0)
        {
            const int dur = (int)(audio::duration_sec() * 1000.0);
            if (dur > 0)
                len_ms = dur;
        }

        playing = audio::playing();
    }

    if (!ended && !user_paused && !downloading && len_ms > 1000 && !audio::playing() &&
        pos_ms >= len_ms - 1000)
    {
        ended = true;
        playing = false;
        pos_ms = len_ms;
        advance();
    }
}

void c_deezer::toggle_play_pause()
{
    if (!loaded)
        return;

    if (playing)
    {
        audio::pause();
        playing = false;
        user_paused = true;
    }
    else
    {
        if (len_ms > 1000 && pos_ms >= len_ms - 1000)
        {
            audio::seek_sec(0.0);
            pos_ms = 0;
            ended = false;
        }
        audio::resume();
        playing = true;
        user_paused = false;
    }
}

void c_deezer::next()
{
    if (queue.empty())
        return;

    int next_index = queue_index + 1;
    if (next_index >= (int)queue.size())
        next_index = 0;

    queue_index = next_index;
    play_track(queue[next_index], queue, next_index);
}

void c_deezer::previous()
{
    if (queue.empty())
        return;

    int prev_index = queue_index - 1;
    if (prev_index < 0)
        prev_index = (int)queue.size() - 1;

    queue_index = prev_index;
    play_track(queue[prev_index], queue, prev_index);
}

void c_deezer::seek(float p)
{
    if (!loaded || len_ms <= 0)
        return;

    p = p < 0.0f ? 0.0f : (p > 1.0f ? 1.0f : p);
    const double target = (double)len_ms * (double)p / 1000.0;

    audio::seek_sec(target);

    pos_ms = (int)(len_ms * p);
    last_poll = 0;
    if (pos_ms < len_ms - 1000)
    {
        ended = false;
        user_paused = false;
    }
}

void c_deezer::stop()
{
    audio::stop();
    playing = false;
    loaded = false;
    user_paused = false;
    ended = false;
    pos_ms = 0;
    len_ms = 0;
    active = false;
    queue_index = -1;
    status.clear();
}

float c_deezer::progress() const
{
    if (len_ms <= 0)
        return 0.0f;
    const float p = (float)pos_ms / (float)len_ms;
    return p < 0.0f ? 0.0f : (p > 1.0f ? 1.0f : p);
}
