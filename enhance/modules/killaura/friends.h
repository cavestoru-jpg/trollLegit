#pragma once

#include <string>
#include <vector>

namespace enhance::modules::killaura::friends_list
{
	void add(const std::string& name);
	void remove(const std::string& name);
	bool contains(const std::string& name);
	std::vector<std::string> all();
	void clear();
}
