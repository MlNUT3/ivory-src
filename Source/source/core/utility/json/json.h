#pragma once
// Minimal JSON reader/writer - just enough for the control panel messages
// (objects, arrays, strings, numbers, bools, null). No dependencies.
#include <string>
#include <vector>
#include <cstdlib>
#include <cstdio>

namespace json
{
	struct Value
	{
		enum Type { Null, Bool, Num, Str, Arr, Obj };

		Type type = Null;
		bool b = false;
		double n = 0.0;
		std::string s;
		std::vector<std::string> keys;   // Obj: parallel to items
		std::vector<Value> items;        // Arr: elements  |  Obj: values

		bool is_obj() const { return type == Obj; }
		bool is_arr() const { return type == Arr; }
		bool is_num() const { return type == Num; }
		bool is_str() const { return type == Str; }

		double as_num() const { return type == Num ? n : (type == Bool ? (b ? 1.0 : 0.0) : 0.0); }
		bool as_bool() const { return type == Bool ? b : (type == Num ? n != 0.0 : false); }
		std::string as_str() const { return type == Str ? s : std::string(); }

		const Value* find(const std::string& k) const
		{
			if (type != Obj) return nullptr;
			const size_t n_items = keys.size() < items.size() ? keys.size() : items.size();
			for (size_t i = 0; i < n_items; ++i)
				if (keys[i] == k) return &items[i];
			return nullptr;
		}

		void put(const std::string& k, Value v)
		{
			type = Obj;
			keys.push_back(k);
			items.push_back(std::move(v));
		}

		static Value mk(bool v) { Value x; x.type = Bool; x.b = v; return x; }
		static Value mk(double v) { Value x; x.type = Num; x.n = v; return x; }
		static Value mk(const std::string& v) { Value x; x.type = Str; x.s = v; return x; }
		static Value mkobj() { Value x; x.type = Obj; return x; }
		static Value mkarr() { Value x; x.type = Arr; return x; }
	};

	namespace detail
	{
		struct Parser
		{
			const char* p;
			const char* e;

			void ws() { while (p < e && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')) ++p; }

			bool lit(const char* k)
			{
				const char* q = p;
				while (*k && q < e && *q == *k) { ++q; ++k; }
				if (*k) return false;
				p = q;
				return true;
			}

			bool string_(std::string& out)
			{
				if (p >= e || *p != '"') return false;
				++p;
				out.clear();
				while (p < e && *p != '"')
				{
					char c = *p++;
					if (c != '\\') { out.push_back(c); continue; }
					if (p >= e) return false;
					char esc = *p++;
					switch (esc)
					{
					case '"': out.push_back('"'); break;
					case '\\': out.push_back('\\'); break;
					case '/': out.push_back('/'); break;
					case 'b': out.push_back('\b'); break;
					case 'f': out.push_back('\f'); break;
					case 'n': out.push_back('\n'); break;
					case 'r': out.push_back('\r'); break;
					case 't': out.push_back('\t'); break;
					case 'u':
					{
						if (e - p < 4) return false;
						unsigned cp = 0;
						for (int i = 0; i < 4; ++i)
						{
							char h = *p++;
							unsigned d = (h >= '0' && h <= '9') ? (h - '0')
								: (h >= 'a' && h <= 'f') ? (h - 'a' + 10)
								: (h >= 'A' && h <= 'F') ? (h - 'A' + 10) : 16u;
							if (d > 15) return false;
							cp = cp * 16 + d;
						}
						// UTF-8 encode (surrogate pairs are not expected in our data)
						if (cp < 0x80) out.push_back((char)cp);
						else if (cp < 0x800)
						{
							out.push_back((char)(0xC0 | (cp >> 6)));
							out.push_back((char)(0x80 | (cp & 0x3F)));
						}
						else
						{
							out.push_back((char)(0xE0 | (cp >> 12)));
							out.push_back((char)(0x80 | ((cp >> 6) & 0x3F)));
							out.push_back((char)(0x80 | (cp & 0x3F)));
						}
						break;
					}
					default: return false;
					}
				}
				if (p >= e || *p != '"') return false;
				++p;
				return true;
			}

			bool value(Value& v)
			{
				ws();
				if (p >= e) return false;
				const char c = *p;
				if (c == '{') return object(v);
				if (c == '[') return array(v);
				if (c == '"') { v.type = Value::Str; return string_(v.s); }
				if (lit("true")) { v.type = Value::Bool; v.b = true; return true; }
				if (lit("false")) { v.type = Value::Bool; v.b = false; return true; }
				if (lit("null")) { v.type = Value::Null; return true; }
				return number(v);
			}

			bool number(Value& v)
			{
				char* end = nullptr;
				const double d = std::strtod(p, &end);
				if (end == p) return false;
				p = end;
				v.type = Value::Num;
				v.n = d;
				return true;
			}

			bool object(Value& v)
			{
				if (*p != '{') return false;
				++p;
				v.type = Value::Obj;
				ws();
				if (p < e && *p == '}') { ++p; return true; }
				for (;;)
				{
					ws();
					std::string k;
					if (!string_(k)) return false;
					ws();
					if (p >= e || *p != ':') return false;
					++p;
					Value child;
					if (!value(child)) return false;
					v.keys.push_back(std::move(k));
					v.items.push_back(std::move(child));
					ws();
					if (p < e && *p == ',') { ++p; continue; }
					if (p < e && *p == '}') { ++p; return true; }
					return false;
				}
			}

			bool array(Value& v)
			{
				if (*p != '[') return false;
				++p;
				v.type = Value::Arr;
				ws();
				if (p < e && *p == ']') { ++p; return true; }
				for (;;)
				{
					Value child;
					if (!value(child)) return false;
					v.items.push_back(std::move(child));
					ws();
					if (p < e && *p == ',') { ++p; continue; }
					if (p < e && *p == ']') { ++p; return true; }
					return false;
				}
			}
		};
	}

	inline bool parse(const std::string& text, Value& out)
	{
		detail::Parser p{ text.c_str(), text.c_str() + text.size() };
		out = Value{};
		return p.value(out);
	}

	inline void esc(const std::string& in, std::string& out)
	{
		for (char c : in)
		{
			switch (c)
			{
			case '"': out += "\\\""; break;
			case '\\': out += "\\\\"; break;
			case '\n': out += "\\n"; break;
			case '\r': out += "\\r"; break;
			case '\t': out += "\\t"; break;
			default:
				if ((unsigned char)c < 0x20) { char b[8]; std::snprintf(b, sizeof(b), "\\u%04x", (unsigned char)c); out += b; }
				else out.push_back(c);
			}
		}
	}

	inline void dump(const Value& v, std::string& out)
	{
		switch (v.type)
		{
		case Value::Null: out += "null"; break;
		case Value::Bool: out += v.b ? "true" : "false"; break;
		case Value::Num:
		{
			char b[40];
			if (v.n == (double)(long long)v.n) std::snprintf(b, sizeof(b), "%lld", (long long)v.n);
			else std::snprintf(b, sizeof(b), "%.6g", v.n);
			out += b;
			break;
		}
		case Value::Str: out.push_back('"'); esc(v.s, out); out.push_back('"'); break;
		case Value::Arr:
		{
			out.push_back('[');
			for (size_t i = 0; i < v.items.size(); ++i) { if (i) out.push_back(','); dump(v.items[i], out); }
			out.push_back(']');
			break;
		}
		case Value::Obj:
		{
			out.push_back('{');
			const size_t n = v.keys.size() < v.items.size() ? v.keys.size() : v.items.size();
			for (size_t i = 0; i < n; ++i)
			{
				if (i) out.push_back(',');
				out.push_back('"'); esc(v.keys[i], out); out += "\":";
				dump(v.items[i], out);
			}
			out.push_back('}');
			break;
		}
		}
	}

	inline std::string dump(const Value& v) { std::string s; dump(v, s); return s; }
}
