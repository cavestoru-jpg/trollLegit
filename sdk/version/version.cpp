#include "version.h"

#include <sdk/classloader.h>
#include <enhance/utils/logger.h>

#include <cctype>
#include <cstring>
#include <string>
#include <vector>

namespace sdk
{
	namespace version
	{
		// The supported table, oldest first. Kept in step with tools/mc_artifacts.py
		// VERSIONS by tools/gen_mappings.py, which emits the same list into
		// mappings_gen.inc; the two are compared at bind time.
		static const char* const k_versions[] = {
			"1.20", "1.20.1", "1.20.2", "1.20.3", "1.20.4", "1.20.5", "1.20.6",
			"1.21", "1.21.1", "1.21.2", "1.21.3", "1.21.4", "1.21.5", "1.21.6",
			"1.21.7", "1.21.8", "1.21.9", "1.21.10", "1.21.11",
			"26.1", "26.1.1", "26.1.2", "26.2", "26.3",
		};
		static const int k_version_count = (int)(sizeof(k_versions) / sizeof(k_versions[0]));

		static int g_ordinal = -1;
		static bool g_exact = false;
		static bool g_detected = false;
		static std::string g_reported;
		static std::string g_description;
		static ns_kind g_ns = ns_kind::unknown;

		int ordinal_of(const char* version_name)
		{
			if (!version_name)
				return -1;
			for (int i = 0; i < k_version_count; ++i)
			{
				if (std::strcmp(k_versions[i], version_name) == 0)
					return i;
			}
			return -1;
		}

		// --- reading the game's own version.json ------------------------------

		static void clear_exception(JNIEnv* env)
		{
			if (env->ExceptionCheck())
			{
				env->ExceptionClear();
			}
		}

		// version.json sits at the root of every client jar from 1.14 onward and
		// holds {"id": "...", "name": "...", "protocol_version": ...}. Parsing one
		// string out of it is not worth a JSON library.
		static std::string json_string_field(const std::string& doc, const char* key)
		{
			std::string needle = std::string("\"") + key + "\"";
			size_t k = doc.find(needle);
			if (k == std::string::npos)
				return std::string();
			size_t colon = doc.find(':', k + needle.size());
			if (colon == std::string::npos)
				return std::string();
			size_t open = doc.find('"', colon);
			if (open == std::string::npos)
				return std::string();
			size_t close = doc.find('"', open + 1);
			if (close == std::string::npos)
				return std::string();
			return doc.substr(open + 1, close - open - 1);
		}

		static std::string read_resource(JNIEnv* env, jobject loader, const char* resource)
		{
			if (!loader)
				return std::string();

			jclass loader_class = env->GetObjectClass(loader);
			if (!loader_class)
			{
				clear_exception(env);
				return std::string();
			}

			jmethodID get_stream = env->GetMethodID(
				loader_class, "getResourceAsStream", "(Ljava/lang/String;)Ljava/io/InputStream;");
			if (!get_stream)
			{
				clear_exception(env);
				env->DeleteLocalRef(loader_class);
				return std::string();
			}

			jstring jname = env->NewStringUTF(resource);
			jobject stream = env->CallObjectMethod(loader, get_stream, jname);
			clear_exception(env);
			env->DeleteLocalRef(jname);
			env->DeleteLocalRef(loader_class);

			if (!stream)
				return std::string();

			std::string out;
			jclass stream_class = env->GetObjectClass(stream);
			// readAllBytes() is Java 9+; the oldest JVM in the supported range is 17.
			jmethodID read_all = stream_class
				? env->GetMethodID(stream_class, "readAllBytes", "()[B") : nullptr;
			if (read_all)
			{
				jbyteArray bytes = (jbyteArray)env->CallObjectMethod(stream, read_all);
				clear_exception(env);
				if (bytes)
				{
					jsize len = env->GetArrayLength(bytes);
					if (len > 0 && len < (1 << 20))
					{
						out.resize((size_t)len);
						env->GetByteArrayRegion(bytes, 0, len, (jbyte*)&out[0]);
						clear_exception(env);
					}
					env->DeleteLocalRef(bytes);
				}
			}
			else
			{
				clear_exception(env);
			}

			if (stream_class)
			{
				jmethodID close = env->GetMethodID(stream_class, "close", "()V");
				if (close)
				{
					env->CallVoidMethod(stream, close);
				}
				clear_exception(env);
				env->DeleteLocalRef(stream_class);
			}
			env->DeleteLocalRef(stream);
			return out;
		}

		static jobject system_classloader(JNIEnv* env)
		{
			jclass cl = env->FindClass("java/lang/ClassLoader");
			if (!cl)
			{
				clear_exception(env);
				return nullptr;
			}
			jmethodID get = env->GetStaticMethodID(
				cl, "getSystemClassLoader", "()Ljava/lang/ClassLoader;");
			jobject loader = get ? env->CallStaticObjectMethod(cl, get) : nullptr;
			clear_exception(env);
			env->DeleteLocalRef(cl);
			return loader;
		}

		static std::string system_property(JNIEnv* env, const char* key)
		{
			jclass sys = env->FindClass("java/lang/System");
			if (!sys)
			{
				clear_exception(env);
				return std::string();
			}
			jmethodID get = env->GetStaticMethodID(
				sys, "getProperty", "(Ljava/lang/String;)Ljava/lang/String;");
			std::string out;
			if (get)
			{
				jstring jkey = env->NewStringUTF(key);
				jstring value = (jstring)env->CallStaticObjectMethod(sys, get, jkey);
				clear_exception(env);
				if (value)
				{
					const char* chars = env->GetStringUTFChars(value, nullptr);
					if (chars)
					{
						out = chars;
						env->ReleaseStringUTFChars(value, chars);
					}
					env->DeleteLocalRef(value);
				}
				env->DeleteLocalRef(jkey);
			}
			clear_exception(env);
			env->DeleteLocalRef(sys);
			return out;
		}

		// --- matching a reported id to a supported table ----------------------

		// "1.21.11-rc1" and "1.21.11-pre2" are that version's API for our purposes.
		// A weekly snapshot ("25w46a") matches nothing and falls through to the
		// newest table, which is logged as a guess rather than a detection.
		static int nearest_ordinal(const std::string& reported, bool& exact_out)
		{
			exact_out = false;
			int exact = ordinal_of(reported.c_str());
			if (exact >= 0)
			{
				exact_out = true;
				return exact;
			}

			int best = -1;
			size_t best_len = 0;
			for (int i = 0; i < k_version_count; ++i)
			{
				const std::string candidate = k_versions[i];
				if (reported.size() <= candidate.size())
					continue;
				if (reported.compare(0, candidate.size(), candidate) != 0)
					continue;
				// only a separator may follow, so "1.21.1" does not swallow "1.21.11"
				const char next = reported[candidate.size()];
				if (next != '-' && next != '_' && next != ' ')
					continue;
				if (candidate.size() > best_len)
				{
					best = i;
					best_len = candidate.size();
				}
			}
			return best;
		}

		// Longest supported id that appears in `text` delimited by something that is
		// not a digit or a dot, so "1.21.1" cannot be picked out of "1.21.11" and
		// "fabric-loader-0.19.3-1.21.4" still yields 1.21.4.
		static std::string scan_for_version(const std::string& text)
		{
			std::string best;
			for (int i = 0; i < k_version_count; ++i)
			{
				const std::string candidate = k_versions[i];
				if (candidate.size() <= best.size())
					continue;

				size_t at = 0;
				while ((at = text.find(candidate, at)) != std::string::npos)
				{
					const bool left_ok = at == 0 ||
						(!isdigit((unsigned char)text[at - 1]) && text[at - 1] != '.');
					const size_t after = at + candidate.size();
					const bool right_ok = after >= text.size() ||
						(!isdigit((unsigned char)text[after]) && text[after] != '.');
					if (left_ok && right_ok)
					{
						best = candidate;
						break;
					}
					at += 1;
				}
			}
			return best;
		}

		// Under Fabric the class path holds the loader, not the game, so the launch
		// command is often the only place the version appears. Prism and the vanilla
		// launcher both put it there, either as versions/<id>/<id>.jar or as the
		// profile name (fabric-loader-<x>-<id>).
		static std::string version_from_properties(JNIEnv* env, const char** how)
		{
			static const char* const keys[] = { "java.class.path", "sun.java.command" };
			for (int i = 0; i < 2; ++i)
			{
				const std::string value = system_property(env, keys[i]);
				if (value.empty())
					continue;
				const std::string found = scan_for_version(value);
				if (!found.empty())
				{
					*how = keys[i];
					return found;
				}
			}
			return std::string();
		}

		bool detect(JNIEnv* env)
		{
			if (g_detected)
				return g_ordinal >= 0;
			if (!env)
				return false;

			g_detected = true;

			std::string doc = read_resource(env, sdk::classloader::get_classloader(), "version.json");
			if (doc.empty())
			{
				jobject system = system_classloader(env);
				if (system)
				{
					doc = read_resource(env, system, "version.json");
					env->DeleteLocalRef(system);
				}
			}

			std::string reported = doc.empty() ? std::string() : json_string_field(doc, "id");
			if (reported.empty() && !doc.empty())
				reported = json_string_field(doc, "name");

			const char* how = "version.json";
			if (reported.empty())
				reported = version_from_properties(env, &how);

			if (reported.empty())
			{
				g_ordinal = k_version_count - 1;
				g_exact = false;
				g_reported = "unknown";
				logger::log_error(
					"[version] could not read version.json or find a version on the class path; "
					"assuming " + std::string(k_versions[g_ordinal]) +
					" -- every symbol below is a guess");
				return false;
			}

			g_reported = reported;
			bool exact = false;
			int ord = nearest_ordinal(reported, exact);
			if (ord < 0)
			{
				ord = k_version_count - 1;
				logger::log_error("[version] " + reported + " is not a supported version; "
					"falling back to the " + std::string(k_versions[ord]) + " table");
			}
			g_ordinal = ord;
			g_exact = exact;

			logger::log("[version] detected " + reported + " via " + how +
				(exact ? std::string(" (exact)")
				       : std::string(" -> using the ") + k_versions[ord] + " table"));
			return true;
		}

		bool detected() { return g_detected && g_ordinal >= 0; }
		int ordinal() { return g_ordinal; }
		const char* reported() { return g_reported.empty() ? "unknown" : g_reported.c_str(); }
		bool exact() { return g_exact; }

		const char* name()
		{
			if (g_ordinal < 0 || g_ordinal >= k_version_count)
				return "unknown";
			return k_versions[g_ordinal];
		}

		bool at_least(const char* version_name)
		{
			const int want = ordinal_of(version_name);
			return want >= 0 && g_ordinal >= want;
		}

		bool before(const char* version_name)
		{
			const int want = ordinal_of(version_name);
			return want >= 0 && g_ordinal >= 0 && g_ordinal < want;
		}

		ns_kind ns() { return g_ns; }
		void set_ns(ns_kind kind) { g_ns = kind; g_description.clear(); }

		const char* ns_name()
		{
			switch (g_ns)
			{
			case ns_kind::official:     return "official";
			case ns_kind::intermediary: return "intermediary";
			case ns_kind::obfuscated:   return "obfuscated";
			default:                    return "unknown";
			}
		}

		const char* describe()
		{
			if (g_description.empty())
			{
				g_description = std::string(name()) + " (" + ns_name() + ")";
				if (!g_exact && g_ordinal >= 0)
					g_description += " [reported " + g_reported + "]";
			}
			return g_description.c_str();
		}
	}
}
