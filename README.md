# lru-cache (`lru`)

A header-only **LRU cache** for C++17: an intrusive doubly-linked list over a
`std::unordered_map`, with optional time-based (TTL) eviction and a
policy-callback design that lets one container express LRU, LFU-ish, and
expiry behavior.

Extracted from [Xapiand](https://github.com/Kronuz/Xapiand), where it caches
schemas, compiled scripts, and more.

## Usage

```cpp
#include "lru.h"

lru::lru<int, std::string> cache(2);   // capacity 2 (0 = unbounded)

cache.emplace(1, "one");
cache.emplace(2, "two");
cache.emplace(3, "three");             // the oldest entry is evicted

cache.find(3) != cache.end();          // true
cache.at(3);                           // "three"
```

For finer control, the `*_and` variants take a callback returning a
`DropAction` / `GetAction` (`renew`, `relink`, `evict`, `stop`, …), so eviction
and access can be driven by your own policy. Construct with a `max_age` to get
TTL eviction.

## Build & test

Header-only. To run the smoke test:

```sh
c++ -std=c++17 -I. test/test.cc -o test/test && ./test/test
# or: cmake -B build && cmake --build build && ctest --test-dir build
```

Requires C++17.

## Notes

- **Trim-before-insert:** the cache trims to capacity at the *start* of each
  insert, so right after an overflowing insert it can transiently hold
  `capacity + 1` entries until the next operation.
- The iterator currently derives from `std::iterator`, which is deprecated in
  C++17 (compiles, but emits a warning) — a small modernization TODO.

## License

MIT — see [LICENSE](LICENSE).
