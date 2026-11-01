# lru-cache

Part of [Kronuz C++ Libraries](https://github.com/Kronuz/cpp-libraries), a collection of small, reusable C++ libraries.

[![CI](https://github.com/Kronuz/lru-cache/actions/workflows/ci.yml/badge.svg)](https://github.com/Kronuz/lru-cache/actions/workflows/ci.yml)
[![License: MIT](https://img.shields.io/badge/license-MIT-blue.svg)](LICENSE)
![C++17](https://img.shields.io/badge/C%2B%2B-17-blue.svg)
![Header-only](https://img.shields.io/badge/header--only-yes-green.svg)

Header-only LRU cache for C++17: O(1) lookup and renewal, an optional time-to-live, and per-entry eviction callbacks.
Values live in a `std::unordered_map`, and an intrusive doubly-linked list
threaded through its nodes keeps them in recency order. Reach for it to cache
derived data (schemas, compiled scripts, parsed configs) and decide, entry by
entry, what stays.

## ✨ Highlights

- **O(1) lookup and renewal.** A lookup is one hash and one node; renewing an
  entry relinks two pointers already in hand.
- **Time-to-live eviction.** `aging_lru` threads a second, age-ordered list
  through the same nodes and evicts entries older than `max_age`.
- **Your eviction policy.** `find_and`, `emplace_and` and `trim_and` take a
  callback that answers `leave`, `renew`, `relink` or `evict` per entry; a trim
  callback can also `stop` the walk.
- **Peek without touching.** `exists` and `find_and_leave` look an entry up
  without renewing it.
- **Allocator-aware.** The three-argument constructor hands a stateful allocator
  to the backing map.
- **One header, no dependencies.**

## 🚀 Quick start

```cpp
#include "lru.h"

#include <chrono>
#include <cstdio>
#include <string>

using namespace std::chrono_literals;

int main() {
	lru::lru<int, std::string> cache(2);       // capacity 2; 0 means unbounded
	cache.emplace(1, "one");
	cache.emplace(2, "two");
	cache.find(1);                             // a hit renews 1
	cache.emplace(3, "three");                 // trims before it inserts: 3 entries
	cache.trim();                              // evicts the least recent: 2
	std::printf("%d %d %d\n", cache.exists(1), cache.exists(2), cache.exists(3));  // 1 0 1

	lru::aging_lru<std::string, std::string> sessions(1000, 30s);  // 30 s time-to-live
	sessions.emplace("token", "user-42");
	sessions.trim();                           // evicts whatever expired
	std::printf("%zu\n", sessions.size());     // 1
}
```

[`examples/demo.cc`](examples/demo.cc) prints the cache after every operation,
so you can watch recency decide who stays.

## 📦 Installation

Requires a C++17 compiler and CMake 3.14 or newer; no dependencies. CI builds it
on Linux (x86_64, arm64), macOS (arm64, x86_64), and FreeBSD, plus ASan, UBSan,
and TSan builds.

```cmake
include(FetchContent)
FetchContent_Declare(lru_cache
  GIT_REPOSITORY https://github.com/Kronuz/lru-cache.git
  GIT_TAG        08f240301898f9594421a2fbc9876b8fda5492bb)  # or main for the tip
FetchContent_MakeAvailable(lru_cache)

target_link_libraries(my_app PRIVATE lru_cache::lru_cache)
```

```cpp
#include "lru.h"
```

Without CMake, copy `lru.h` onto your include path.

## 🎯 When to use it

Use it for a bounded cache with LRU order, a time-to-live, or your own mix of the
two, owned by one thread.

Not for:

- **Sharing across threads as is.** There is no internal locking; guard it
  yourself.
- **A hard capacity at every instant.** `emplace` trims before it inserts, so
  right after an overflowing insert the cache holds one extra entry until the
  next trim.
- **Holding iterators across changes.** An erase, an evicting lookup or a trim
  invalidates iterators to the entries it removes.

## ⚡ Performance

[`lru_cache_bench`](bench/bench.cc) times a cache in front of a slower source: each
operation looks a key up with `find_and_renew` and, on a miss, inserts it with
`emplace`. Keys are skewed towards small values and drawn from a universe twice the
capacity, so about half the lookups hit. The comparator is the textbook LRU, a
`std::list` of entries (most recent first) and a `std::unordered_map` from key to list
iterator (`ListMapLru` in the same file); no single LRU library is the usual choice in
C++, and this is the structure hand-written caches use. It is built with the capacity
plus one because `lru::lru` trims before it inserts, and the benchmark checks on every
row that both caches count the same hits. Each cell is ns per operation, the range
over two runs of best-of-5; "× faster" is the textbook's time over `lru::lru`'s in the
better run, so a value below 1 is a loss.

| keys, capacity | M4 Pro `lru::lru` | M4 Pro textbook | M4 Pro × faster | i9 `lru::lru` | i9 textbook | i9 × faster |
|---|---:|---:|---:|---:|---:|---:|
| `uint64_t`, 1,024 | 23.3 to 23.4 | 26.0 to 28.8 | 1.12 | 90.3 to 94.2 | 87.4 to 94.9 | 0.97 |
| `uint64_t`, 65,536 | 33.7 to 34.5 | 45.6 to 45.8 | 1.35 | 236.2 to 251.9 | 273.5 to 279.1 | 1.16 |
| `uint64_t`, 1,048,576 | 98.8 to 125.5 | 115.5 to 140.1 | 1.17 | 331.1 to 333.0 | 328.8 to 335.1 | 0.99 |
| `std::string`, 65,536 | 73.0 to 77.7 | 80.9 to 95.9 | 1.11 | 260.8 to 266.4 | 439.4 to 442.5 | 1.68 |

Apple M4 Pro (macOS 26) and Intel Core i9-9980HK (macOS 15, x86-64), one thread,
Apple clang 17.0.0 with `-O3 -DNDEBUG` (CMake Release).

```sh
cmake -B build -DCMAKE_BUILD_TYPE=Release -DLRU_CACHE_BUILD_BENCH=ON
cmake --build build --target lru_cache_bench
./build/lru_cache_bench
```

A lookup touches one hash and one node, and renewing relinks pointers already in
hand. At a million entries both caches spend their time on cache misses in the
hash table, and the gap closes; on the i9 the two are level at 1,024 and at a
million entries.

## 📚 Documentation

- [docs/API.md](docs/API.md): construction, insertion, lookup, the callbacks,
  removal, iteration, and caveats.
- [ARCHITECTURE.md](ARCHITECTURE.md): the node layout, the two lists, and how
  trimming walks them.
- [CHANGELOG.md](CHANGELOG.md): what changed.
- [AGENTS.md](AGENTS.md): repository map, invariants, and traps.
- [Kronuz C++ Libraries](https://github.com/Kronuz/cpp-libraries/blob/main/AGENTS.md#lru-cache): the catalog entry.

## 🛠️ Building and testing

```sh
cmake -B build
cmake --build build
ctest --test-dir build
```

`./build/lru_cache_demo` runs the demo. Add `-DLRU_CACHE_BUILD_BENCH=ON` to
build `lru_cache_bench`. The iterator derives from the deprecated
`std::iterator`, so builds print `-Wdeprecated-declarations` warnings.

## 🤝 Contributing

Issues and pull requests are welcome. Read [AGENTS.md](AGENTS.md) and the
catalog's [rules of engagement](https://github.com/Kronuz/cpp-libraries/blob/main/AGENTS.md#rules-of-engagement)
first; CI must pass on every platform.

## 📄 License

MIT, Copyright (c) 2026 Germán Méndez Bravo and Copyright (c) 2015-2020 Dubalu
LLC. See [LICENSE](LICENSE). Extracted from [Xapiand](https://github.com/Kronuz/Xapiand),
where it caches schemas and compiled scripts.
