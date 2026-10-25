#include "lru.h"
#include <cassert>
#include <memory>
#include <new>

struct AllocationState {
	std::size_t bytes = 0;
	bool refuse = false;
};
template <class T> struct Allocator {
	using value_type = T;
	std::shared_ptr<AllocationState> state;
	Allocator() = delete;
	explicit Allocator(std::shared_ptr<AllocationState> value) : state(std::move(value)) {}
	template <class U> Allocator(const Allocator<U>& other) : state(other.state) {}
	T* allocate(std::size_t count) {
		if (state->refuse) throw std::bad_alloc();
		auto result = std::allocator<T>{}.allocate(count);
		state->bytes += count * sizeof(T);
		return result;
	}
	void deallocate(T* value, std::size_t count) noexcept {
		std::allocator<T>{}.deallocate(value, count);
		state->bytes -= count * sizeof(T);
	}
	template <class U> bool operator==(const Allocator<U>& other) const noexcept {
		return state == other.state;
	}
	template <class U> bool operator!=(const Allocator<U>& other) const noexcept {
		return !(*this == other);
	}
};
int main() {
	using Node = lru::node<std::pair<const int, int>, lru::base_node>;
	using A = Allocator<std::pair<const int, Node>>;
	using Cache = lru::lru<int, int, std::hash<int>, std::equal_to<int>, Node, A>;
	auto state = std::make_shared<AllocationState>();
	{
		Cache cache(2, std::chrono::milliseconds(0), A(state));
		state->refuse = true;
		bool refused = false;
		try { cache.emplace(1, 10); } catch (const std::bad_alloc&) { refused = true; }
		assert(refused && cache.empty());
		state->refuse = false;
		cache.emplace(1, 10);
		cache.emplace(2, 20);
		cache.emplace(3, 30);
		assert(state->bytes > 0 && cache.at(3) == 30);
		assert(cache.at(1) == 10);
		cache.emplace(4, 40);
		assert(cache.at(4) == 40 && cache.at(1) == 10);
		assert(cache.find(2) == cache.end());
	}
	assert(state->bytes == 0);
}
