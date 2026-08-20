# core_handler — proposed centralized allocation layer

**Status:** draft / reference notes, not an official carklight repo
document. Written against `C_ARKlight` as of commit `800968b`
("Stage 5b - JS Backend done.") for the user's own reference when
raising this with the project.

Companion to `disk_handler.md`. These are deliberately kept as two
separate proposals, not one combined document — see "Why not one
`core_handler` doing both" below.

---

## 1. Problem this addresses

As of Stage 5b, raw allocation calls (`malloc`/`realloc`/`calloc`/
`free`) are **not centralized** — they appear directly in 9 separate
files:

```
core/build_result.c
core/ir_build.c
core/node.c
core/normalize.c
core/site.c
core/validate.c
backends/css/render.c
backends/html/render.c
backends/js/render.c
```

46 call sites total. Notably, this includes all three backends —
which per `docs/ADDENDUM.md` §4.1 are only supposed to interact with
the rest of the system through the opaque public API, yet each has
independently rolled its own growable-buffer logic (e.g. `strbuf_t` /
`sb_reserve` in `backends/html/render.c`) rather than sharing one
hardened implementation.

Consequences of the current state:
- No single choke point for allocation-failure injection/testing
  ("what happens if malloc fails at this specific call site").
- No single place to add allocation tracking/logging beyond what
  ASan/LeakSanitizer already gives at the process level.
- Any future move to a pooled/arena allocator (for performance, or
  for MCU-class targets per `docs/ARKVM.md`'s memory constraints)
  requires touching all 9 files individually.
- Growable-buffer logic (reserve/grow/append) is duplicated per
  backend instead of shared.

## 2. Proposed scope

A single new file, living **inside `core/`** (not a new top-level
directory) — `core/alloc.c`, declared in `internal.h` — since this is
exactly the kind of foundational, "every later stage inherits this"
decision `docs/IMPLEMENTATION.md` says belongs as early as possible.

Proposed surface (illustrative, not final signatures):

```c
void*  ark_alloc(size_t size);
void*  ark_realloc(void* ptr, size_t size);
void   ark_dealloc(void* ptr);

/* Shared growable-buffer helper, replacing each backend's private
 * strbuf_t-style implementation. */
typedef struct ArkBuf ArkBuf;
int    ark_buf_init(ArkBuf* buf);
int    ark_buf_reserve(ArkBuf* buf, size_t extra);
int    ark_buf_append(ArkBuf* buf, const char* s);
int    ark_buf_append_n(ArkBuf* buf, const char* s, size_t n);
void   ark_buf_free(ArkBuf* buf);
```

Every existing `malloc`/`realloc`/`free` call site across the 9 files
above gets replaced with the corresponding `ark_*` call. No behavior
change otherwise — this is a mechanical extraction, not a new
allocation *strategy* (arenas/pools stay out of scope here; this
layer is what would make adding one later a one-file change instead
of a 9-file change).

## 3. What this explicitly does NOT do

This is job #1 only — a memory-handling utility layer. It is **not**:

- A dispatcher that decides which backend's `render`/`init`/
  `postprocess`/`shutdown` to call. That job already exists — see
  `ArkBackend` in `include/carklight.h` (a vtable-style struct of
  function pointers, one instance per backend: `ark_html_backend()`,
  `ark_css_backend()`, `ark_js_backend()`). Nothing currently calls
  those three functions yet; the real dispatcher is reserved for
  `lib_glue.c`, whose current placeholder comment explicitly says
  top-level orchestration lands there once a later stage needs it.
- A place for backend-specific "custom implementations" of anything.
  Backend-specific logic belongs in each backend's own directory,
  behind the existing `ArkBackend` contract — introducing a second,
  competing interface abstraction inside `core_handler` for the same
  job duplicates `ArkBackend` rather than complementing it.

## 4. Why not one `core_handler` doing both memory AND dispatch

The project's existing discipline (per `docs/IMPLEMENTATION.md`'s own
stage ordering) is one seam, one concern, at every layer so far:
normalize doesn't know about validate; validate doesn't know about IR
build; backends don't know about each other. Merging "generic
allocation wrapper" and "cross-backend contract dispatch" into one
module breaks that pattern — a bug in one becomes harder to isolate
from a bug in the other, which undermines the exact debuggability
this proposal is meant to buy.

## 5. Timing / cost of waiting

Stages 0–5 already shipped directly against raw `malloc`/`free`.
Retrofitting later means touching all 9 files regardless of when it
happens — the cost doesn't shrink by waiting, and it only grows once
Stage 6 (disk I/O) and Stage 7 (ABI wiring, ark_load_ir
deserialization — itself allocation-heavy) add more call sites on top
of the current 46. Doing this before Stage 6 starts avoids stacking
a 10th and 11th file onto the eventual retrofit.

## 6. Open questions to raise with the project

- Does an allocator-wrapper layer count as "core" for the purposes of
  `docs/ADDENDUM.md` §4's modular-internally-one-`.so`-externally
  build wiring, or does it need its own CMake target?
- Should `ArkBuf` replace `backends/html/render.c`'s existing
  `strbuf_t` outright (breaking change to that file), or coexist
  until CSS/JS backends need the same growable-buffer logic and a
  natural consolidation point appears?
