# carklight — Proposal & Architecture

**A C ABI compiler core for ARKlight, carrying over only the *stable*
subset of the fast-moving Python project — with every high-level
frontend able to run on either a pure reimplementation (lazy to hack
on, no compiled dependency) or a thin native binding straight into
`libcarklight` (lazy to maintain, one core, every language benefits).**

Status: proposal / design draft
Baseline: [Rae-ARK/ARKlight](https://github.com/Rae-ARK/ARKlight) `alpha`
branch, current release **v0.0431**

---

## 1. The relationship between the two projects

This is the load-bearing decision of the whole proposal, so it goes
first.

**ARKlight (Python) is upstream and stays the fast-moving project.**
All new component vocabulary, all pipeline changes, all backend work —
everything on ARKlight's own roadmap (`docs/ARCHITECTURE.md`'s
Milestones table: `v0.044` JS capability expansion, the in-progress
reactive-core vdom staging, `v0.048` CSS `@media`, `v0.060`/`v0.080`
desktop/Android backends, `v0.100` user-defined components) happens
there first, in Python, the way it always has.

**carklight is downstream and only ever carries over what's already
settled.** It doesn't race ARKlight feature-for-feature. It syncs
periodically, pulling across only the parts of the schema and pipeline
that have graduated from ARKlight's own `PLANNED`/`IN PROGRESS` status
to `DONE` — and, in practice, sat at `DONE` through at least one more
ARKlight release without changing shape, since "just landed" and
"stable enough to freeze into a second implementation" aren't the same
bar.

This mirrors a pattern already in ARKlight's own docs:
`docs/CONFIGURABILITY.md`'s rule that something becomes user-facing
"when a real site could want it different *and* nothing already
reaches it" — carklight applies the same discipline one level up: a
feature only becomes cross-language-portable once it's proven out, not
while it's still being designed.

### The strategic framing this enables

ARKlight's own trajectory already leans toward "Python is the
authoring experience, not the runtime" — no Python in production
output was true from v0.001. This proposal pushes that one step
further: **Python is the reference implementation, not necessarily the
implementation every deployment has to execute.** A Python developer
still writes `site.py → ARKlight → static artifacts`, unchanged. A
Node developer can eventually write `site.js → @arklight/js →
carklight → static artifacts`, targeting the same underlying
semantics through the same IR contract (§3.4), without ARKlight-the-
Python-package being anywhere in their runtime path. That's a more
interesting story than "we rewrote our Python package in C because C
is fast" — it's "authoring and runtime were already separate concepts
in this project, and carklight is what makes that separation actually
load-bearing across languages, not just within Python."

### What "the first carklight" is

**carklight v1 is a straight C port of ARKlight v0.0431** — the
current alpha, as-is, including its one outstanding known gap (the
`srcset`/`poster`/`action`/`formaction` build-time-warning-not-fix from
the v0.0431 patch itself). No feature carklight didn't get from a
real, shipped ARKlight release. The baseline is a snapshot, not a
moving target. §6 lists exactly what's in it.

---

## 2. Sync model

```
ARKlight (Python, upstream)                    carklight (C, downstream)
──────────────────────────                     ──────────────────────────
v0.0431 (current alpha)  ───────baseline──────▶ carklight v1
   │
   │  active development continues:
   │  v0.044 (JS capability expansion)
   │  vdom-staging (Stage 3-8)
   │  v0.048 (CSS @media, Stage A/B)
   │  ...
   ▼
v0.044 ships + soaks ────┐
vdom-staging completes ──┼──▶  graduation review  ───▶ carklight v2
v0.048 ships + soaks ────┘     (see §2.1)
```

### 2.1 What triggers a sync

Scoped to one or more entries in ARKlight's own Milestones table
(`docs/ARCHITECTURE.md`) moving to `DONE` and staying there:

1. Ships in a real ARKlight release (a `CHANGELOG.md` entry, not just
   a merged branch).
2. Survives at least one subsequent ARKlight release unchanged in
   public shape (component names, required props, CLI flags) — the
   "soak" period. ARKlight's alpha-stage churn is fine and expected
   upstream; carklight doesn't need to absorb it turn by turn.
3. Its `tests/` fixtures are stable enough to serve as carklight's own
   parity fixtures (§3.3).

Not yet eligible, per ARKlight's current roadmap: `v0.044`, vdom-staging
Stages 3–8, `v0.048` Stage B, `v0.060`, `v0.080`, `v0.100` — all
`PLANNED`/`IN PROGRESS` today.

### 2.2 Versioning

carklight version numbers are deliberately **not** the same as
ARKlight's — matching numbers would wrongly imply feature parity.
Instead each carklight release states its baseline explicitly:

```
carklight v1  →  baseline: ARKlight v0.0431
carklight v2  →  baseline: ARKlight v0.0XX (whatever's DONE + soaked
                  at sync time)
```

The changelog names the exact milestone(s) carried over, e.g.
"carklight v2: carries over ARKlight v0.044 and vdom-staging Stages
3–8, both DONE and soaked as of ARKlight v0.045." Runtime-introspectable
too — see `ark_carklight_version()`/`ark_arklight_baseline()` in §3.4.

---

## 3. Architecture

### 3.1 Pipeline split

```
Python Source
    │
    ▼
Python AST          arklight.parser.discover  (unchanged, stays Python)
    ▼
ARK AST              arklight.parser.loader + arklight.api  (unchanged,
                      executes the site file for real — same trick
                      Flask uses for config files)
┈┈┈┈┈┈┈┈┈┈┈┈┈┈┈┈┈┈┈┈┈┈┈┈┈┈┈┈┈┈┈┈┈┈┈┈┈┈┈┈┈┈┈┈┈┈┈┈┈┈┈┈┈┈┈┈┈┈┈┈┈┈┈┈┈┈┈┈
  IR boundary — the one contract carklight and every frontend must agree on
┈┈┈┈┈┈┈┈┈┈┈┈┈┈┈┈┈┈┈┈┈┈┈┈┈┈┈┈┈┈┈┈┈┈┈┈┈┈┈┈┈┈┈┈┈┈┈┈┈┈┈┈┈┈┈┈┈┈┈┈┈┈┈┈┈┈┈┈
    ▼
Normalization → Validation → Website IR → HTML/CSS/JS backends → pack
    (this half exists TWICE per high-level frontend — see §4 — once as
     a pure reimplementation, once as a thin call into libcarklight)
```

Everything above the IR boundary is *inherently* Python (or, for JS,
inherently something Node's V8 does) — real code execution, not on any
migration path. Everything below it is what carklight ports, and what
each frontend can choose to run either itself or via the native lib.

### 3.2 The schema is the single generated source of truth

`arklight.ir.schema.SCHEMA` (~87 components as of v0.0431) is never
hand-duplicated. A generator, run against whichever ARKlight version a
carklight release syncs from, emits:

- the C `component_type_t` enum, prop tables, and builder function
  signatures (`ark_heading`, `ark_container_arr`, ...)
- the IR (de)serialization code for every execute-and-serialize
  frontend
- the **pure-language reimplementation's** schema tables too (a
  Python `dict`/JS `object` mirroring `SCHEMA` exactly), so the manual
  engine and the native engine are graded against the identical
  component list, never two hand-maintained copies drifting apart

Same discipline ARKlight already uses for its JS backend's
`BEHAVIOR_REGISTRY`/`ACTION_REGISTRY` (one generated file per entry),
extended across the language boundary and across the pure/native split.

### 3.3 Parity testing — semantic parity mandatory, byte parity where it's a meaningful invariant

ARKlight's existing suite (`tests/test_html_backend.py`,
`test_css_backend.py`, `test_js_backend.py`, `test_ir_build.py`,
`test_normalize.py`, `test_validate.py`, `test_pack.py`,
`test_pipeline_end_to_end.py`) is the fixture source, run through
**every engine of every frontend**:

```
                    ARKlight-py (canonical reference)
                              │
        ┌─────────────────────┼─────────────────────┐
        ▼                     ▼                     ▼
  Python · pure          Python · native       JS · pure engine
  (existing engine)      (ctypes → libcarklight) (reimplementation)
                              ▼
                        JS · native engine
                       (N-API → libcarklight)
```

An earlier draft of this document stated the bar as "byte-for-byte
parity" across the board. That's stronger than the invariant actually
needs to be, and treating it as a universal promise would create false
failures wherever byte equality isn't a meaningful property to hold —
filesystem metadata, generated ordering, platform-specific packaging,
timestamps, path separators, or compression details can differ between
two correct implementations without either being wrong. The corrected
bar, in two tiers:

- **Semantic parity is mandatory, for every engine, always.** Same
  input site produces the same component tree, the same validation
  errors (or lack of them), the same rendered structure and attributes,
  the same `.ark` contents once unpacked — regardless of incidental
  serialization differences.
- **Byte-for-byte identity is required specifically where a backend
  already guarantees deterministic, canonical serialization** — e.g.
  the generated HTML/CSS/JS text output for a given IR, which ARKlight's
  Python backends already produce deterministically today. Where a
  backend's own contract doesn't promise byte determinism (e.g. `.ark`
  packing, if timestamps or compression parameters ever entered the
  picture), the test asserts semantic equality after canonicalization
  instead of raw byte equality.

This is a healthier test contract: it still makes carklight fully
accountable to the Python reference (nothing here weakens that), it
just doesn't manufacture failures out of incidental representation
differences that were never part of what "correct" means.

### 3.4 C ABI surface

```c
// carklight.h — versioned per §2.2

typedef struct ArkSite ArkSite;
typedef struct ArkNode ArkNode;
typedef struct ArkBuildResult ArkBuildResult;

// --- Native tree building (used directly by C, and by any FFI-direct
//     language's native engine) --------------------------------------
ArkNode* ark_page(ArkNode** children, size_t n, /* head-metadata */ ...);
ArkNode* ark_heading(int level, const char* text);
ArkNode* ark_text(const char* text);
ArkNode* ark_button(const char* text, const char* on_click /* nullable */);
ArkNode* ark_container_arr(ArkNode** children, size_t n);
#define ark_container(...) ark_container_arr(ARK_CHILDREN(__VA_ARGS__))
// ... one constructor per SCHEMA entry, generated (§3.2)

// --- IR ingestion — THE stable ABI boundary. Every language binding,
//     regardless of how it built its tree, is expected to serialize to
//     this canonical IR format (§3.2) and call this one function. This
//     is the integration point that gets documented, versioned, and
//     guaranteed not to change shape within a carklight major version. --
ArkSite* ark_load_ir(const uint8_t* ir_bytes, size_t len, char** err_out);
void     ark_free_site(ArkSite* site);

// --- Native tree ingestion — lower-level, optional, NOT the primary
//     integration surface. Exists only for callers already holding an
//     in-process ArkNode* tree built via the constructors above (i.e.
//     carklight's own C frontend, and nothing else by default) who want
//     to skip a serialize/deserialize round trip they don't need. Every
//     other binding — including Rust/Go/Swift per §4 — is expected to
//     go through ark_load_ir instead, so there is exactly one public
//     representation every language needs to understand, not two. This
//     function may change shape more freely across releases than
//     ark_load_ir, precisely because it isn't the contract. --
ArkSite* ark_load_root(ArkNode* root) /* advanced / internal use */;

// --- Building ----------------------------------------------------------
#define ARK_BACKEND_HTML (1u << 0)
#define ARK_BACKEND_CSS  (1u << 1)
#define ARK_BACKEND_JS   (1u << 2)

ArkBuildResult* ark_build(ArkSite* site, uint32_t backend_flags, char** err_out);
size_t          ark_result_file_count(const ArkBuildResult* r);
const char*     ark_result_file_path(const ArkBuildResult* r, size_t i);
const uint8_t*  ark_result_file_data(const ArkBuildResult* r, size_t i, size_t* len_out);
int             ark_write_output(const ArkBuildResult* r, const char* output_dir, char** err_out);
void            ark_free_result(ArkBuildResult* r);

// --- .ark bundle ---------------------------------------------------------
int ark_pack(const char* build_dir, const char* out_path,
             const char* passphrase /* nullable */, int plain, char** err_out);
int ark_unpack(const char* bundle_path, const char* out_dir,
               const char* passphrase /* nullable */, char** err_out);

// --- Introspection (backs `arklight search`) ------------------------------
int ark_schema_lookup(const char* component_name, char* json_schema_out,
                       size_t out_len, char* suggestions_out[5]);

// --- Version/provenance --------------------------------------------------
const char* ark_carklight_version(void);      // e.g. "carklight v1"
const char* ark_arklight_baseline(void);       // e.g. "ARKlight v0.0431"

void ark_free_string(char* s);
```

---

## 4. Frontends: pure engine + native engine, per language

Every high-level frontend (Python, JS) ships **both** engines from the
start, switchable at runtime. Native-only languages (Rust/Go/Swift
added later) only ever need the native path — there's no reason to
hand-write a second implementation of normalize/validate/backends in
Rust when calling into carklight is just as fast and exactly as
portable.

**Every native engine, in every language, integrates through
`ark_load_ir` — the serialized IR is the one stable public boundary
(§3.4).** This applies even to C itself and to FFI-direct languages
like Rust/Go/Swift: they may *build* their tree however is idiomatic
for that language (native structs, a builder chain), but they
serialize it to canonical IR bytes before crossing into carklight,
the same as Python and JS's native engines do. `ark_load_root` exists
as a documented escape hatch for in-process C callers who want to skip
that round trip, but it's explicitly not what any language binding is
built against — this keeps the number of public representations every
binding has to understand at exactly one, instead of two competing
integration models.

```
Language frontend  →  canonical IR (serialized)  →  libcarklight  →  backend
```
not
```
Language frontend  →  its own native tree mapping  →  libcarklight
```

| Frontend | Pure engine | Native engine |
|---|---|---|
| **Python** | `arklight`'s existing pipeline (`arklight.ir.*`, `arklight.backend.*`) — the reference implementation, always correct-by-definition since it's what ARKlight-py *is* | `ctypes`/`cffi` binding: serialize the ARK AST to IR bytes, call `ark_load_ir` → `ark_build`, same public `Site`/`Page`/component API |
| **JS/Node** | A hand-written reimplementation of normalize/validate/HTML-CSS-JS backends in plain JS, generated schema tables from §3.2 — no native binary required at all, works anywhere Node (or even a browser/edge runtime) runs | N-API addon or spawn-the-compiled-CLI (§4.3): serialize the walked object tree to IR bytes, call `ark_load_ir` → `ark_build` |
| **C** | *(not applicable — C is carklight's native tongue, there's only one engine)* | Builds a tree via the native constructors; may use `ark_load_root` directly in-process (§3.4), since it's the one caller that escape hatch exists for |
| Rust / Go / Swift / ... | *(not offered — see §5 Non-goals)* | Idiomatic builder API in that language, generated schema-aware serializer emits IR bytes, calls `ark_load_ir` — same integration shape as Python/JS's native engine, not a bespoke tree-passing convention per language |

### 4.1 Why both, not one — the actual payoff

- **Pure engine = lazy prototyping, zero build toolchain.** Anyone
  contributing to the JS or Python frontend doesn't need a C compiler,
  doesn't need to rebuild a native addon on every change, and can run
  entirely inside `npm test`/`pytest` with nothing else installed.
  This is exactly the "rapid iteration" half of the ask — new
  component types, new backend behavior, whatever's still `PLANNED` on
  ARKlight's own roadmap, gets prototyped here first, in whichever
  language is most convenient, before it's anywhere near carklight-
  eligible per §2.
- **Native engine = lazy maintenance, write heavy lifting once.**
  Once a feature has synced into carklight (§2), every frontend's
  native engine gets it "for free" the moment carklight is rebuilt —
  no need to hand-port the same normalize/validate/backend logic into
  JS *and* keep it in sync with Python *and* eventually Rust. This is
  the "endpoint where we can just import the backend" half of the ask.
- **Neither engine is a second-class citizen.** Both are real,
  supported, parity-tested (§3.3) code paths — the choice between them
  is about tradeoffs (build-toolchain-free vs. fastest/most-portable),
  not about one being a stub or a fallback.

### 4.2 Selecting an engine

Same shape in both languages — an explicit flag, with an environment
variable default and a sane auto-detect fallback:

```bash
# Python
arklight build site.py --engine=pure      # default while carklight
arklight build site.py --engine=native    #   hasn't synced a feature
                                            #   the site uses yet
```

```bash
# JS
npx arklight build site.js --engine=pure
npx arklight build site.js --engine=native
```

If `--engine` is omitted: try native first (it's faster and, once
carklight has synced, exercises the shared, most-battle-tested core);
if the site uses any component/prop not yet covered by the currently
installed carklight's baseline (checked via `ark_arklight_baseline()`,
§3.4), fall back to pure automatically with a one-line notice — never
a hard failure just because a site is using something carklight hasn't
caught up to yet.

### 4.3 JS distribution specifics

`@arklight/js` on npm ships the pure engine unconditionally (pure JS,
no native code, works in any JS environment including ones that can't
load a native addon — browser playgrounds, some edge runtimes). The
native engine is an optional accelerant: either an N-API addon
distributed per-platform via `optionalDependencies` (the `sharp`/
`esbuild` pattern) or, simpler for v1, spawning a bundled `carklight`
binary over stdin/stdout. Either way, `npm install @arklight/js` never
*requires* a C toolchain on the consumer's machine — worst case it
falls back to pure, same as the auto-detect behavior in §4.2.

---

## 5. Non-goals

- **carklight racing ARKlight feature-for-feature.** §1–2 exist
  specifically so it doesn't. Lagging behind `PLANNED`/`IN PROGRESS`
  milestones is the design working as intended.
- **A pure reimplementation for every language.** Only offered for
  Python and JS, because those are the two with their own dynamic
  runtime *and* an active reason to want a build-toolchain-free path
  (rapid prototyping in Python since it's upstream; broad
  browser/edge reach for JS). Rust/Go/Swift get native-only bindings
  (§4, last row) — a hand-maintained second implementation in each
  would be pure maintenance cost with no corresponding benefit, since
  neither has the same "no C toolchain available" story JS sometimes
  does.
- **Re-executing user code in C.** carklight never parses or
  interprets a scripting language — only already-built trees or
  already-serialized IR bytes cross into it.
- **A new crypto dependency.** `.ark` sealing stays HMAC/SHA-256/
  PBKDF2, hand-implemented, matching `arklight.packer`'s existing
  stdlib-only stance.
- **Silently absorbing upstream churn.** Anything still `IN PROGRESS`
  or `PLANNED` on ARKlight's roadmap isn't carklight's concern yet, in
  either engine, just because a pure-engine prototype of it might
  already exist upstream.

---

## 6. What baseline v0.0431 gets you in carklight v1

Grounded directly in ARKlight's current `README.md`/`CHANGELOG.md`:

- The full ~87-component schema (v0.001 core + both vocabulary
  addenda: semantic layout, text-level semantics, forms, tables,
  media, numbered/description lists, responsive images, native
  widgets, zero-JS `Dialog`, bidi/ruby text, table column grouping,
  `IFrame`, `NoScript`).
- HTML/CSS/JS backends, the intrinsic-responsive-layout utility
  classes (`.stack`/`.cluster`/`.sidebar`/`.switcher`/`.grid`/
  `.center`/`.reel`/`.fluid-heading`), and the closed `on_click`/
  `behavior_target` vocabulary (`toggle`, `scroll-to`, and the rest of
  `BEHAVIOR_REGISTRY`/`ACTION_REGISTRY`).
- `Page(...)` head metadata (v0.043) and `Backend.postprocess(...)`.
- `Site(max_width=..., bg=...)`, `site.style(name, rules)`, CSS
  pseudo-class shorthand (v0.042).
- The full `.ark` bundle format: sealed-by-default polyglot packing,
  `--passphrase`/`--plain`, pack/unpack.
- `arklight search <name>` schema introspection.
- The v0.0431 patch's build-time warnings for unrouted `srcset`/
  `poster`/`action`/`formaction`.

Not included yet, because ARKlight itself hasn't shipped/soaked them:
`v0.044`, vdom-staging Stages 3–8, `v0.048` Stage B, `v0.060` onward.
Both the Python and JS **pure engines**, however, are free to prototype
any of that ahead of a carklight sync — that's exactly what they're
for.
