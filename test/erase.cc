#include "lru.h"
#include <array>
#include <cstdio>
#include <stdexcept>
#include <string>

static void check(bool value, const char *message) {
	if (!value)
		throw std::runtime_error(message);
}
template <class Cache> static void exercise() {
	Cache cache(16);
	check(cache.erase("missing") == 0, "empty erase count");
	cache.emplace("only", 42);
	check(cache.erase("only") == 1, "final entry must report one removal");
	check(cache.size() == 0, "final entry remains");
	check(cache.erase("only") == 0, "repeated erase count");
	for (int key = 0; key != 8; ++key)
		cache.emplace(std::to_string(key), key);
	std::size_t removed = 0;
	for (int key : std::array<int, 8>{3, 7, 1, 0, 5, 4, 2, 6}) {
		const auto name = std::to_string(key);
		const auto before = cache.size();
		removed += cache.erase(name);
		check(cache.size() + 1 == before, "erase size delta");
		check(!cache.exists(name), "erased key remains");
		check(cache.erase(name) == 0, "missing erase count");
	}
	check(removed == 8 && cache.size() == 0, "total erase count");
	cache.emplace("reused", 17);
	check(cache.at("reused") == 17 && cache.erase("reused") == 1, "cache reuse");
}
int main() {
	try {
		exercise<lru::lru<std::string, int>>();
		exercise<lru::aging_lru<std::string, int>>();
		std::puts("erase counts: plain and aging caches pass");
	} catch (const std::exception &error) {
		std::fprintf(stderr, "%s\n", error.what());
		return 1;
	}
}
