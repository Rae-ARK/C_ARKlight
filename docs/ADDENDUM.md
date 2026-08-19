# carklight — Addendum: `.arklight`, the compile-time model, and modular-internal packaging

Companion to [`PROPOSAL.md`](./PROPOSAL.md) and
[`IMPLEMENTATION.md`](./IMPLEMENTATION.md). This
addendum revises three things from the original proposal in light of
follow-up discussion. It does not replace those documents — it
supersedes specific sections, noted inline.

Status: addendum / design draft, not yet folded back into the main
proposal documents.

---

## 1. Two-tier consumption model (supersedes §3.4/§4's framing)

The original proposal treated `ark_load_ir` as *the* integration
point for everyone, with `ark_load_root` as a secondary "escape
hatch... not what any language binding is built against." That
hedging is gone. There are two honest, equally first-class paths, not
one path with an asterisk:

```
C (carklight's own frontend)
    → builds and consumes its own native tree directly
    → never round-trips through a serialized file
    → in-process, function calls, no format to version

Every other language (Python, JS, future Rust/Go/Swift/...)
    → always crosses through a binary .arklight file (§2)
    → this is the only contract those languages need to trust
    → carklight never needs to expose a native-tree ABI to them
```

C is a genuine special case because it *is* carklight's native
tongue — there is no reason for it to serialize to itself. Every
other language's job is just: build a tree in whatever way is
idiomatic for that language, then emit a `.arklight` file. What reads
that file back doesn't care which language wrote it.

---

## 2. `.arklight` — a real binary file format, not wire bytes

The original proposal's "canonical IR" was an in-memory serialization
crossing a single function call (`ark_load_ir(bytes, len)`). Making it
a real file on disk changes what it *is*: a portable artifact that
outlives one process, closer to a `.pyc`, a JVM `.class` file, or
LLVM bitcode than to a request/response payload. Filename: **`.arklight`**
— same instinct as Svelte's own compiled output: the authoring layer
does the work once, ahead of time, and leaves behind something small
and standalone that just runs.

To function as a durable file format (not just a serialization
scheme), it needs, at minimum:

- **Magic bytes + format version**, independent of carklight's own
  version number — so any consumer can reject or warn on an
  incompatible file instead of misreading it or crashing on it.
- **A schema-generation tag embedded in the file**, tied to the
  sync/graduation model in `PROPOSAL.md` §2. Since carklight
  only ever tracks a `DONE`-and-soaked subset of ARKlight's schema at
  any given version, a `.arklight` file needs to self-declare which
  schema generation it was built against, so a consumer can tell
  "this file uses a component/prop I don't know about" apart from
  "this file is corrupt."
- **A string table**, since component props and text content will
  dominate file size otherwise — dedup repeated strings (tag names,
  class names, repeated prop keys) rather than inlining them at every
  occurrence.

House style: hand-rolled, zero-dependency binary encoding, matching
the project's existing instinct (`arklight.packer`'s `.ark` bundle
format is already a hand-implemented polyglot binary format with
sealed-by-default HMAC/SHA-256/PBKDF2 — `.arklight` should follow the
same discipline rather than reaching for protobuf/flatbuffers/etc.).

**What `.arklight` is not:** it is not a general-purpose serialization
of arbitrary ARKlight state, and it is not versioned independently
per-language. One format, one spec, read by carklight regardless of
which frontend produced it.

---

## 3. Clarifying "ARKlight Virtual Machine" — compiler, not runtime

Resolved directly: **compile-time only.** `.arklight` in, a target
backend runs, output comes out, done. Nothing about it is loaded or
interpreted again after the build step — there is no on-device
execution model, no bytecode interpreter shipped inside an installed
app. The "VM" language describes a *multi-target dispatcher*, not a
virtual machine in the CPU-emulation sense — the closer analogy is
LLVM IR feeding multiple codegen backends, not a JVM/CLR executing
bytecode at runtime.

```
site.arklight
      │
      ▼
carklight (C)   ── loads the file, dispatches to ONE target backend
      │
      ├── HTML/CSS/JS     (today)
      ├── desktop app     (v0.060, eventually — DESIGN-NOTES.md)
      ├── android app     (v0.080, eventually — DESIGN-NOTES.md)
      └── ...whatever gets added later, same input file, same loader
```

This also means the desktop/Android backends described in
`DESIGN-NOTES.md` — which today operate on already-*built*
HTML/CSS/JS output and package it into an installable — could
eventually consume `.arklight` directly instead, collapsing "build,
then separately package" into one stage with one canonical input.
That's a real architectural decision for later, not something this
addendum resolves now — noted here only so it isn't lost.

**Side effect for future language frontends:** a future `@arklight/js`
authoring package doesn't need its own compiler at all under this
model. It only needs to build a tree in JS and emit `.arklight` — the
same artifact Python already produces. This likely removes the need
for the original proposal's "pure engine, hand-written reimplementation
per language" (§4/§4.1 of `PROPOSAL.md`) as a *permanent*
fixture, since there's less reason to avoid the compiled `.so` once
it's a small, portable, dependency-free binary. Worth revisiting that
non-goal later; not blocking anything today.

---

## 4. Build & packaging: modular internally, one `.so` externally

Public surface stays exactly what the original README already
assumed — one `libcarklight.so`/`.dll`/`.dylib` + `carklight.h`. What
changes is how it's built internally: CMake, not raw `make`, with
each backend as its own target, linked together into a single shared
object at the end. CPack handles the platform packaging step on top
(the `.so`/`.dll`/`.dylib` + header + `.pc` file bundle) — a drop-in
replacement for the original `make install`, not a redesign of it.

```
carklight/
  core/              IR loading, validation, shared tree utilities
  backends/
    html/
    css/
    js/
    desktop/         (later — v0.060)
    android/         (later — v0.080)
  CMakeLists.txt     each backend: add_library(carklight_<name> STATIC ...)
                     linked together into one libcarklight.so
```

Whether `desktop/` and `android/` end up sharing one internal GUI
implementation underneath, rather than being written independently,
is an open idea, not a decision — see `DESIGN-NOTES.md`.

Why this shape specifically:

- **A backend rewrite stays contained.** Touching the HTML backend
  means touching one directory, one CMake target, rebuilding one
  static lib — it doesn't ripple into CSS/JS/core, the same isolation
  benefit a fully-separate `.so`-per-backend design would give.
- **No runtime ABI between backends is needed**, because nothing is
  dynamically loaded at runtime — everything resolves at *link* time.
  The only contract that needs versioning discipline is `.arklight`
  itself (§2); the core-to-backend boundary is internal C linkage, not
  a public API surface (see §4.1).
- **Slimming down later is a build flag, not a rewrite.** If a
  genuinely minimal HTML-only build is ever wanted, that's
  `-DCARKLIGHT_BACKEND_ANDROID=OFF` at CMake configure time, not an
  architecture change — the modularity needed for that is already
  present from day one, it's just a question of what gets linked in.

### 4.1 The backend interface

The core-to-backend boundary named above is a single, fixed contract
every backend implements, rather than each backend exposing its own
one-off set of functions for `core/` to call by name:

```c
typedef struct ArkBackend {
    const char* name;                  // "html", "css", "js", ...
    uint32_t    flag;                  // one of ARK_BACKEND_* (PROPOSAL.md §3.4)
    int  (*init)(struct ArkBackend* self, char** err_out);        // optional
    int  (*render)(struct ArkBackend* self, const ArkSite* site,
                    ArkBuildResult* out, char** err_out);         // required
    int  (*postprocess)(struct ArkBackend* self, ArkBuildResult* out,
                          char** err_out);                        // optional
    void (*shutdown)(struct ArkBackend* self);                    // optional
} ArkBackend;
```

`ark_build` (`PROPOSAL.md` §3.4) doesn't contain any HTML-, CSS-, or
JS-specific logic itself. It walks a fixed, compile-time-registered
array of `ArkBackend` entries — one per `backends/<name>/` directory
in §4's layout — and calls `render` (then `postprocess`, if non-`NULL`)
on each entry whose `flag` is set in the caller's `backend_flags`, in
registration order. Adding a backend that isn't on the roadmap yet is
a new directory, a new CMake target, and one new array entry — never a
change to `ark_build`'s own logic, and never a reason to touch another
backend's source.

This is what makes §4's directory split load-bearing rather than
cosmetic: each `backends/<name>/` directory owns exactly one
`ArkBackend` implementation, builds and tests in isolation, and has no
visibility into any other backend's internals. `core/` only ever calls
through the struct above — never into a specific backend's private
functions — so disabling one (`-DCARKLIGHT_BACKEND_ANDROID=OFF`),
replacing one, or porting a revised version of one from ARKlight-py
(`IMPLEMENTATION.md` Stages 4–5) touches exactly that backend's
directory and nothing else.

`init`/`shutdown` are part of the struct from v1 even though HTML,
CSS, and JS all leave them `NULL` and do their setup inline inside
`render` — they exist so a backend with real lifecycle needs (a future
desktop backend opening a window-toolkit handle, say) fits the same
contract everything else already does, instead of the interface
needing a shape change the day one finally requires them.

---

## 5. Summary of what this addendum changes

| Topic | Original proposal | This addendum |
|---|---|---|
| C's own integration path | `ark_load_root`, framed as a secondary escape hatch | First-class, permanent — C never touches the file format at all |
| Non-C language integration | Serialize to "canonical IR bytes" (in-memory contract) | Emit a real file: `.arklight` — versioned, schema-tagged, on disk |
| "ARKlight VM" | Not named in the original proposal | Named explicitly: compile-time multi-target dispatcher, no runtime interpretation, ever |
| Desktop/Android backends | Package already-*built* HTML/CSS/JS output | Could eventually consume `.arklight` directly — noted as a later option, not decided |
| Build system | `make` / `make install` | CMake (modular internal targets) + CPack (platform packaging), same external `.so` surface |
| Core-to-backend dispatch | Implied, unspecified | Fixed `ArkBackend` interface (§4.1), compile-time-registered array, no per-backend special-casing in `ark_build` |
| Future JS pure-engine (§4/§4.1 of the original proposal) | Permanent parallel reimplementation, alongside a native binding | Likely unnecessary long-term once `.arklight` exists — JS only needs to *emit* the file |

Everything in `PROPOSAL.md` §1 (upstream/downstream
relationship), §2 (sync model), and `IMPLEMENTATION.md`'s
staged build-out (Stages 0–8) still holds — this addendum only
touches the boundary shape and the packaging model, not the
core/upstream relationship or the implementation ordering.
