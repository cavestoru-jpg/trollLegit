// Binds every constant in mappings.hpp to the spelling the running game uses.
//
// The table is generated (tools/gen_mappings.py) from tools/symbols/symbols.json,
// which keys each symbol by its official Mojang name. That key is the only one
// that survives the whole supported range: intermediary stops existing after
// 1.21.11 and obfuscated names are re-rolled every release, but the official name
// is what 26.x runs natively and what both other namespaces are derived from.
//
// Binding happens once, after the class loader is up and before any lookup. A
// symbol the running version does not have binds to "", which every consumer
// already treats as absent.

#include <sdk/includes.h>
#include <sdk/classloader.h>
#include <sdk/version/version.h>
#include <enhance/utils/logger.h>

#include <cstring>
#include <string>
#include <vector>

namespace sdk
{
	namespace mappings
	{
		// --- generated data ---------------------------------------------------

#define MAPPINGS_GEN_STRINGS
#define MAPPINGS_GEN_VERSIONS
#define MAPPINGS_GEN_TABLE
#include "mappings_gen.inc"
#undef MAPPINGS_GEN_STRINGS
#undef MAPPINGS_GEN_VERSIONS
#undef MAPPINGS_GEN_TABLE

		static const int k_symbol_count =
			(int)(sizeof(g_table[0][0]) / sizeof(g_table[0][0][0]));
		static const int k_version_count =
			(int)(sizeof(g_table[0]) / sizeof(g_table[0][0]));
		static const int k_namespace_count =
			(int)(sizeof(g_table) / sizeof(g_table[0]));

		static const char* const g_symbol_ids[] = {
#define MAPPINGS_ID(index, id) #id,
#define MAPPINGS_GEN_IDS
#include "mappings_gen.inc"
#undef MAPPINGS_GEN_IDS
#undef MAPPINGS_ID
		};

		// --- the constants themselves ----------------------------------------

		const char* version = "unknown";

#define MAPPINGS_SYM(index, name_ident, sig_ident) \
	const char* name_ident = ""; const char* sig_ident = "";
#define MAPPINGS_SYM_NAME(index, name_ident) const char* name_ident = "";
#define MAPPINGS_SYM_SIG(index, sig_ident)   const char* sig_ident = "";
#define MAPPINGS_GEN_BINDINGS
#include "mappings_gen.inc"
#undef MAPPINGS_GEN_BINDINGS
#undef MAPPINGS_SYM_SIG
#undef MAPPINGS_SYM_NAME
#undef MAPPINGS_SYM

		static bool g_bound = false;
		static std::vector<const char*> g_unresolved;
		static int g_ns = 0;
		static int g_ver = 0;

		static void bind_all(int ns, int ver)
		{
#define MAPPINGS_SYM(index, name_ident, sig_ident)            \
	name_ident = g_strings[g_table[ns][ver][index][0]];       \
	sig_ident  = g_strings[g_table[ns][ver][index][1]];
#define MAPPINGS_SYM_NAME(index, name_ident) \
	name_ident = g_strings[g_table[ns][ver][index][0]];
#define MAPPINGS_SYM_SIG(index, sig_ident) \
	sig_ident = g_strings[g_table[ns][ver][index][1]];
#define MAPPINGS_GEN_BINDINGS
#include "mappings_gen.inc"
#undef MAPPINGS_GEN_BINDINGS
#undef MAPPINGS_SYM_SIG
#undef MAPPINGS_SYM_NAME
#undef MAPPINGS_SYM
		}

		static int index_of(const char* id)
		{
			for (int i = 0; i < k_symbol_count; ++i)
			{
				if (std::strcmp(g_symbol_ids[i], id) == 0)
					return i;
			}
			return -1;
		}

		// --- namespace probe ---------------------------------------------------

		// Finding the client class is not enough on its own: an obfuscated name is
		// three letters long and could belong to anything on the class path. The
		// probe also resolves the static singleton field on it, which no unrelated
		// class will have with the same name and descriptor.
		static bool namespace_answers(JNIEnv* env, int ns, int ver)
		{
			const char* class_name = g_strings[g_table[ns][ver][index_of("minecraftclass")][1]];
			const char* field_name = g_strings[g_table[ns][ver][index_of("minecraftclient")][0]];
			const char* field_sig  = g_strings[g_table[ns][ver][index_of("minecraftclient")][1]];
			if (!class_name[0] || !field_name[0])
				return false;

			jclass klass = sdk::classloader::find_class(env, class_name);
			if (!klass)
			{
				if (env->ExceptionCheck())
					env->ExceptionClear();
				return false;
			}

			jfieldID fid = env->GetStaticFieldID(klass, field_name, field_sig);
			if (env->ExceptionCheck())
				env->ExceptionClear();
			env->DeleteLocalRef(klass);
			return fid != nullptr;
		}

		static sdk::version::ns_kind probe_namespace(JNIEnv* env, int ver)
		{
			// Official first: on 26.x it is the only one that exists, and on the
			// older range it only answers for a mojmap-remapped instance, which is
			// exactly what we would want to use if someone runs one.
			const sdk::version::ns_kind order[] = {
				sdk::version::ns_kind::official,
				sdk::version::ns_kind::intermediary,
				sdk::version::ns_kind::obfuscated,
			};

			for (int i = 0; i < 3; ++i)
			{
				const int ns = (int)order[i];
				if (ns >= k_namespace_count)
					continue;
				if (namespace_answers(env, ns, ver))
					return order[i];
			}
			return sdk::version::ns_kind::unknown;
		}

		// --- entry point -------------------------------------------------------

		bool bind(JNIEnv* env)
		{
			if (g_bound)
				return true;
			if (!env)
				return false;

			// The generated table and sdk::version must agree on what the version
			// list is, or every lookup below is off by a row.
			if (k_version_count != (int)(sizeof(g_version_names) / sizeof(g_version_names[0])))
			{
				logger::log_error("[mappings] generated table is inconsistent; not binding");
				return false;
			}

			sdk::version::detect(env);
			int ver = sdk::version::ordinal();
			if (ver < 0 || ver >= k_version_count)
				ver = k_version_count - 1;

			if (std::strcmp(g_version_names[ver], sdk::version::name()) != 0)
			{
				logger::log_error(std::string("[mappings] version table mismatch: generated '") +
					g_version_names[ver] + "' vs runtime '" + sdk::version::name() +
					"' -- regenerate with tools/gen_mappings.py");
				return false;
			}

			sdk::version::ns_kind ns = probe_namespace(env, ver);
			if (ns == sdk::version::ns_kind::unknown)
			{
				// Nothing answered. Pick what the loader implies so the log shows a
				// concrete guess rather than a dead client, and say so plainly.
				ns = sdk::classloader::is_fabric() ? sdk::version::ns_kind::intermediary
				                                   : sdk::version::ns_kind::official;
				logger::log_error(std::string("[mappings] no namespace answered on ") +
					sdk::version::name() + "; assuming " +
					(ns == sdk::version::ns_kind::intermediary ? "intermediary" : "official") +
					" -- if the menu says 0 symbols resolved, this is why");
			}
			sdk::version::set_ns(ns);

			g_ns = (int)ns;
			g_ver = ver;
			bind_all((int)ns, ver);
			version = sdk::version::name();

			g_unresolved.clear();
			for (int i = 0; i < k_symbol_count; ++i)
			{
				if (g_table[(int)ns][ver][i][0] == 0 && g_table[(int)ns][ver][i][1] == 0)
					g_unresolved.push_back(g_symbol_ids[i]);
			}

			g_bound = true;

			std::string summary = std::string("[mappings] bound ") +
				std::to_string(k_symbol_count - (int)g_unresolved.size()) + "/" +
				std::to_string(k_symbol_count) + " symbols for " + sdk::version::describe();
			logger::log(summary);

			if (!g_unresolved.empty())
			{
				std::string missing;
				for (size_t i = 0; i < g_unresolved.size(); ++i)
				{
					if (i)
						missing += ", ";
					missing += g_unresolved[i];
				}
				logger::log("[mappings] absent on this version: " + missing);
			}
			return true;
		}

		bool bound() { return g_bound; }
		int symbol_count() { return k_symbol_count; }
		const std::vector<const char*>& unresolved() { return g_unresolved; }

		const char* owner_of(const char* symbol_id)
		{
			if (!g_bound || !symbol_id)
				return "";
			const int i = index_of(symbol_id);
			if (i < 0)
				return "";
			return g_strings[g_table[g_ns][g_ver][i][2]];
		}
	}
}
