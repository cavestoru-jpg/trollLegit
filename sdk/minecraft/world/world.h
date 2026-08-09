#pragma once

#include <sdk/includes.h>
#include <vector>

namespace sdk
{
	class world_client
	{
	private:
		jobject world;

	public:
		world_client(jobject world);
		~world_client();

		std::vector<jobject> get_players();

		// Every loaded entity, not just the players list. Needed by anything
		// that can target a mob: the players field does not contain them, so a
		// scan built on get_players() can never match one however wide its
		// filters are.
		std::vector<jobject> get_entities();
	};
}

