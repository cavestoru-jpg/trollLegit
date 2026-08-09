#include <enhance/enhance.h>
#include "world.h"
#include <sdk/mappings/mappings.hpp>

sdk::world_client::world_client(jobject world)
{
	this->world = world;
}

sdk::world_client::~world_client()
{
}

std::vector<jobject> sdk::world_client::get_players()
{
	std::vector<jobject> players;
	auto env = enhance::instance->get_env();
	if (!env || !world) return players;

	jclass world_class = env->GetObjectClass(world);
	if (!world_class) return players;

	jfieldID fid = env->GetFieldID(world_class, sdk::mappings::players_field_name, sdk::mappings::players_field_sig);
	if (!fid)
	{
		env->DeleteLocalRef(world_class);
		return players;
	}

	jobject players_list = env->GetObjectField(world, fid);
	if (!players_list)
	{
		env->DeleteLocalRef(world_class);
		return players;
	}

	jclass list_class = env->GetObjectClass(players_list);
	if (!list_class)
	{
		env->DeleteLocalRef(world_class);
		env->DeleteLocalRef(players_list);
		return players;
	}

	jmethodID size_method = env->GetMethodID(list_class, "size", "()I");
	jmethodID get_method = env->GetMethodID(list_class, "get", "(I)Ljava/lang/Object;");

	if (size_method && get_method)
	{
		jint list_size = env->CallIntMethod(players_list, size_method);
		for (jint i = 0; i < list_size; i++)
		{
			jobject player = env->CallObjectMethod(players_list, get_method, i);
			if (player)
			{
				players.push_back(player);
			}
		}
	}

	env->DeleteLocalRef(list_class);
	env->DeleteLocalRef(players_list);
	env->DeleteLocalRef(world_class);

	return players;
}


std::vector<jobject> sdk::world_client::get_entities()
{
	std::vector<jobject> entities;
	auto env = enhance::instance->get_env();
	if (!env || !world) return entities;

	jclass world_class = env->GetObjectClass(world);
	if (!world_class) return entities;

	jmethodID mid_get = env->GetMethodID(world_class,
		sdk::mappings::client_world_get_entities_name,
		sdk::mappings::client_world_get_entities_sig);
	if (env->ExceptionCheck()) env->ExceptionClear();
	env->DeleteLocalRef(world_class);
	if (!mid_get) return entities;

	jobject iterable = env->CallObjectMethod(world, mid_get);
	if (env->ExceptionCheck()) { env->ExceptionClear(); return entities; }
	if (!iterable) return entities;

	// Iterable/Iterator are bootstrap classes, so FindClass resolves them even
	// on a thread whose loader cannot see net/minecraft/*.
	jclass iterable_cls = env->FindClass("java/lang/Iterable");
	jclass iterator_cls = env->FindClass("java/util/Iterator");
	if (env->ExceptionCheck()) env->ExceptionClear();
	if (!iterable_cls || !iterator_cls)
	{
		env->DeleteLocalRef(iterable);
		return entities;
	}

	jmethodID mid_iterator = env->GetMethodID(iterable_cls, "iterator", "()Ljava/util/Iterator;");
	jmethodID mid_has_next = env->GetMethodID(iterator_cls, "hasNext", "()Z");
	jmethodID mid_next     = env->GetMethodID(iterator_cls, "next", "()Ljava/lang/Object;");
	env->DeleteLocalRef(iterable_cls);
	env->DeleteLocalRef(iterator_cls);

	if (!mid_iterator || !mid_has_next || !mid_next)
	{
		env->DeleteLocalRef(iterable);
		return entities;
	}

	jobject it = env->CallObjectMethod(iterable, mid_iterator);
	env->DeleteLocalRef(iterable);
	if (env->ExceptionCheck()) { env->ExceptionClear(); return entities; }
	if (!it) return entities;

	// Bounded. A busy world can hold thousands of entities, and every one of
	// them costs a local ref the caller has to free; an unbounded walk here
	// would exhaust the frame's local reference table.
	constexpr size_t k_max = 512;

	while (entities.size() < k_max)
	{
		const jboolean more = env->CallBooleanMethod(it, mid_has_next);
		if (env->ExceptionCheck()) { env->ExceptionClear(); break; }
		if (!more) break;

		jobject ent = env->CallObjectMethod(it, mid_next);
		if (env->ExceptionCheck()) { env->ExceptionClear(); break; }
		if (ent) entities.push_back(ent);
	}

	env->DeleteLocalRef(it);
	return entities;
}
