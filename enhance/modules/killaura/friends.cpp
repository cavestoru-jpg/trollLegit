#include "friends.h"
#include "../../globals/globals.h"
#include <algorithm>
#include <mutex>

namespace enhance::modules::killaura::friends_list
{

static std::mutex g_mtx;

static bool ieq(char a, char b)
{
	if (a >= 'A' && a <= 'Z') a = static_cast<char>(a + 32);
	if (b >= 'A' && b <= 'Z') b = static_cast<char>(b + 32);
	return a == b;
}

static bool icase_equal(const std::string& a, const std::string& b)
{
	if (a.size() != b.size()) return false;
	for (size_t i = 0; i < a.size(); ++i)
		if (!ieq(a[i], b[i])) return false;
	return true;
}

void add(const std::string& name)
{
	if (name.empty()) return;
	std::lock_guard<std::mutex> lk(g_mtx);
	auto& v = globals::killaura_friends;
	for (const auto& f : v) if (icase_equal(f, name)) return;
	v.push_back(name);
}

void remove(const std::string& name)
{
	std::lock_guard<std::mutex> lk(g_mtx);
	auto& v = globals::killaura_friends;
	v.erase(std::remove_if(v.begin(), v.end(),
		[&](const std::string& s) { return icase_equal(s, name); }), v.end());
}

bool contains(const std::string& name)
{
	std::lock_guard<std::mutex> lk(g_mtx);
	for (const auto& f : globals::killaura_friends)
		if (icase_equal(f, name)) return true;
	return false;
}

std::vector<std::string> all()
{
	std::lock_guard<std::mutex> lk(g_mtx);
	return globals::killaura_friends;
}

void clear()
{
	std::lock_guard<std::mutex> lk(g_mtx);
	globals::killaura_friends.clear();
}

}
