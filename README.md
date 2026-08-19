# carklight

**The compiler core behind ARKlight.**
Written in C. Stable by design. Callable from anywhere.

---

## Overview

carklight takes an already-built ARKlight site — normalized,
validated, expressed as intent, not markup — and turns it into real
files: HTML, CSS, JavaScript, and a sealed `.ark` bundle.

It does one job, and it does it in C: a small, dependency-free
library behind a stable ABI. No interpreter. No runtime. Just a
compiler, and a header any language can link against.

```c
ArkSite* site = ark_load_arklight(bytes, len, &err);
ArkBuildResult* result = ark_build(site, 0, &err);
ark_write_output(result, "ARK", &err);
```

That's the whole surface most callers ever touch. `bytes`/`len` here
are the contents of a `.arklight` file — the machine-facing encoding
of a site's IR. See
[`docs/TERMINOLOGY.md`](./docs/TERMINOLOGY.md) if "IR" vs.
"`.arklight`" isn't already familiar: short version, IR and
`.arklight` both exist and both matter, but for different readers —
IR is the human-facing, conceptual tree (what shows up in docs,
diagrams, and debug output), `.arklight` is the binary, machine-facing
encoding of that same tree (what's actually portable — versioned,
self-describing, safe to write to disk and hand to a different
process, possibly a different language's binding, later). Only
`.arklight` ever crosses this boundary; IR never does.

---

## Where it fits

ARKlight, the Python project, is where sites are authored and where
the language evolves — new components, new backend behavior, the
whole roadmap. carklight doesn't try to keep pace with that. It picks
up only what's already shipped, and stayed unchanged through another
release.

```
ARKlight (Python)  ──  moves fast, defines what's next
       │
       │  ships, then stays unchanged for a release
       ▼
carklight (C)       ──  moves slowly, on purpose
```

**carklight v1 tracks ARKlight v0.0431**, as released. Nothing newer,
nothing experimental. What that includes is below.

---

## Get started

```bash
cmake -S . -B build     # configure; each backend is its own CMake target,
                          #   linked into one libcarklight at the end
cmake --build build      # libcarklight.a / .so / .dll
ctest --test-dir build   # runs against ARKlight's own test fixtures
cpack --config build/CPackConfig.cmake
                          # packages the .so/.dll/.dylib + header + .pc file
                          #   (drop-in replacement for `make install`)
```

Zero external dependencies. No crypto library, no build system
dependency beyond CMake/CTest/CPack themselves. Internally the source
tree is modular — one directory and one CMake target per backend
(`core/`, `backends/html/`, `backends/css/`, `backends/js/`, and
later `backends/desktop/`, `backends/android/`) — but the externally
visible artifact is unchanged: one `libcarklight.so`/`.dll`/`.dylib`
plus `carklight.h`. See
[`docs/ADDENDUM.md`](./docs/ADDENDUM.md) §4 for why it's
shaped this way.

---

## The API

```c
#include "carklight.h"

char* err = NULL;

ArkSite* site = ark_load_arklight(bytes, len, &err);
ArkBuildResult* result = ark_build(site, /* default backends */ 0, &err);

ark_write_output(result, "ARK", &err);
ark_pack("ARK", "site.ark", /* passphrase */ NULL, /* plain */ 0, &err);

ark_free_result(result);
ark_free_site(site);
```

One boundary in, one function to build, one to pack. Every language
binding — Python, JavaScript, anything with C FFI — talks to carklight
through the same `.arklight` file: the machine-facing encoding of a
site's IR, described in full in
[`docs/ADDENDUM.md`](./docs/ADDENDUM.md) §2, with the
broader architecture in
[`docs/PROPOSAL.md`](./docs/PROPOSAL.md).

carklight's own C frontend is the one exception: it builds and
consumes its own native `ArkNode*` tree directly, in-process, and
never round-trips through a `.arklight` file at all (`ark_load_root`,
`docs/PROPOSAL.md` §3.4).

---

## What v1 includes

Everything shipped in ARKlight v0.0431:

- The full component set — layout, text, forms, tables, media,
  responsive images, native widgets, and more. Around 87 components
  in total.
- HTML, CSS, and JS backends, including the intrinsic-responsive
  layout classes and the closed behavior/action vocabulary.
- Page metadata — title, description, favicon, Open Graph.
- Custom CSS classes via `site.style(...)`.
- `.ark` bundles — sealed by default, plain on request.

Nothing from ARKlight's in-progress work is here yet. That's
intentional — see [`docs/PROPOSAL.md`](./docs/PROPOSAL.md)
for the sync model, and
[`docs/IMPLEMENTATION.md`](./docs/IMPLEMENTATION.md) for how
this was built, stage by stage.

---

## Part of ARKlight

carklight is the backend. It doesn't author sites, and it isn't where
you'd go to write one.

- **[ARKlight](https://github.com/Rae-ARK/ARKlight)** — the Python
  package. Write your site here.
- **`@arklight/js`** — the JavaScript/TypeScript package. Same idea,
  for Node.

Both can run entirely on their own, in pure Python or pure JS. Both
can also link straight into carklight instead. Either way, the output
is the same.

---

## Docs map

All long-form docs now live under [`docs/`](./docs):

- [`docs/TERMINOLOGY.md`](./docs/TERMINOLOGY.md) — IR vs.
  `.arklight`, start here if a term is unclear.
- [`docs/PROPOSAL.md`](./docs/PROPOSAL.md) — architecture,
  sync model, C ABI surface.
- [`docs/ADDENDUM.md`](./docs/ADDENDUM.md) — the `.arklight`
  file format, the compile-time model, modular-internal build.
- [`docs/IMPLEMENTATION.md`](./docs/IMPLEMENTATION.md) —
  staged build-out, Stage -1 through Stage 8.
- [`docs/DESIGN-NOTES.md`](./docs/DESIGN-NOTES.md) — later backends
  (desktop, Android) and the internal GUI library they may share.
  Forward-looking notes only — nothing here is committed or built yet.

---

## License

Tracks upstream ARKlight's license — see
[Rae-ARK/ARKlight `LICENSE`](https://github.com/Rae-ARK/ARKlight/blob/alpha/LICENSE).
