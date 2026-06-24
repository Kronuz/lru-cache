# AGENTS.md

Guidance for AI agents and new contributors working in this repo. Read
`ARCHITECTURE.md` before making non-trivial changes to `lru.h`.

## Repo map

```
lru.h              The entire library. Header-only, ~810 lines. Everything is here.
test/test.cc       Smoke test. Builds and runs in seconds.
CMakeLists.txt     INTERFACE target + one ctest registration.
README.md          User-facing docs.
ARCHITECTURE.md    Internal design.
LICENSE            MIT, Copyright (c) 2015-2019 Dubalu LLC.
```

There is no `src/`, no build system to speak of beyond the CMake glue, and no
dependencies. The library is `lru.h` and nothing else.

## Build and run the test

```sh
# Direct compile (fastest):
c++ -std=c++17 -I. test/test.cc -o test/test && ./test/test

# Or via CMake:
cmake -B build && cmake --build build && ctest --test-dir build
```

Expected output: `lru-cache OK: oldest evicted, 3 live entries (cap 2)`. The
build emits exactly one `-Wdeprecated-declarations` warning from `std::iterator`
(see traps); that is expected, not a regression.

## Conventions

- C++17, header-only. No `.cpp` translation unit for the library, no linking.
  Keep it that way — everything stays in `lru.h`.
- Tabs for indentation (match the existing file).
- Double quotes in code examples.
- The public surface is `namespace lru`; implementation details live in
  `namespace lru::detail`. New internals go in `detail`.
- No new dependencies. The includes are all standard library, and that is a
  feature.
- If you change behavior, update `test/test.cc` and the relevant README/ARCH
  sections in the same change.

## Load-bearing invariants

Break either of these and the cache corrupts silently. Both are explained in
`ARCHITECTURE.md`; the short version:

1. Map/intrusive-list coupling. Each value lives inside a `node` stored *in* the
   `std::unordered_map`, and the linked list is threaded through those same
   objects (`lru.h:279`, `lru.h:298`). This only works because the map is a
   node-based container with stable value addresses. Do not replace it with a
   container that moves its values (e.g. a flat/open-addressed map) without
   re-architecting the list. Every node that is in the map must be linked into
   the recency list (and the age list, if aging), and every node erased from the
   map must be unlinked first. The pattern is always: `node->unlink()` *then*
   `_map.erase(...)` (`lru.h:430`, `lru.h:665`, `lru.h:706`). Erasing without
   unlinking leaves dangling list pointers; unlinking without erasing leaks a map
   entry out of the list.

2. The action-enum contract. `OnDrop` returns a `DropAction` and `OnGet` returns
   a `GetAction` (`lru.h:320`). The container is the only thing that performs the
   structural change for an action — callbacks decide, the container acts. If you
   add an action, you must handle it in *every* `switch` that dispatches it:
   `find_and` (both the mutable and const overloads, `lru.h:419` and `lru.h:475`)
   for `GetAction`, and both phases of `trim_and` (`lru.h:695`, `lru.h:723`) for
   `DropAction`. A missing case is a silent fall-through.

## How to extend: adding an eviction policy

You almost never need to touch the container to add a policy. A policy is a
callback.

- For access-time behavior, pass an `OnGet` lambda to `find_and` / `at_and`:

  ```cpp
  cache.find_and(
      [](const cache_t::value_type& kv, bool over_capacity, bool expired) {
          // inspect kv, decide:
          return cache_t::GetAction::renew;   // or leave / relink / evict
      },
      key);
  ```

- For eviction behavior, pass an `OnDrop` lambda to `emplace_and` / `insert_and`
  / `trim_and`:

  ```cpp
  cache.trim_and(
      [](const cache_t::value_type& kv, bool overflowed, bool expired) {
          if (overflowed || expired) return cache_t::DropAction::evict;
          return cache_t::DropAction::stop;   // bail once the rest is fine
      });
  ```

Return `stop` from a drop policy as soon as the remaining entries are all fine,
or `trim_and` keeps walking the whole list. Provide a fixed-policy convenience
wrapper (like `find_and_renew`, `lru.h:444`) if the new policy is common enough
to deserve a named shorthand.

Only edit the container itself if the policy needs information the callback
doesn't currently receive (then you change the callback signature and every call
site), or if you're adding a new action enum value (then see invariant 2).

## Traps

- Trim-before-insert: `emplace_and` trims at the *start* (`lru.h:345`), so right
  after an overflowing insert the cache holds `capacity + 1` entries until the
  next operation. Tests must allow for this — `test/test.cc:24` asserts a range,
  not an exact count. Do not "fix" this by asserting exact capacity.
- `std::iterator` deprecation: the iterator base class (`lru.h:59`) is deprecated
  in C++17 and produces a warning on every build. It is a known TODO, not a new
  problem. If you modernize it, replace the base with explicit member typedefs
  and verify the iterator still satisfies `bidirectional_iterator`.
- `max_age` on a non-aging cache: a non-zero `max_age` only works with the aging
  node type. Passing it to a plain `lru::lru` trips an `assert` (`lru.h:338`),
  which compiles out under `NDEBUG`. Use `aging_lru` whenever you need a TTL.
- Two lists on aging nodes: an `aging_base_node` is in *two* intrusive lists
  (recency and age). `unlink`/`link` handle both (`lru.h:216`, `lru.h:223`) —
  call them, never poke the raw pointers.
- Not thread-safe. Don't add a lock inside the container expecting callers to
  rely on it; the design leaves synchronization to the caller.

## Provenance

This library was extracted from
[Xapiand](https://github.com/Kronuz/Xapiand), where it backs the schema and
compiled-script caches. When in doubt about intended behavior, the Xapiand
history is the original context. Keep it self-contained and dependency-free so
it stays usable outside that project.
