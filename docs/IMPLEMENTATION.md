# carklight — Implementation Plan

Companion to [`PROPOSAL.md`](./PROPOSAL.md). This
document breaks the C port into stages that are each independently
buildable, independently testable against ARKlight-py's own fixtures,
and ordered so that later stages never have to debug an unstable
foundation underneath them.

Ordering principle: **roughly safest-to-get-wrong first, riskiest
last.** Pure tree recursion before string generation; string generation
before I/O and crypto; the public ABI wiring — the seam every other
language eventually depends on — dead last, once everything beneath it
is already known-correct in isolation.

Each stage lists: what it does, what it deliberately does *not* yet
do, its test fixture source, and what makes it a reasonable unit for a
student (or anyone) to actually finish before moving on.

Not every fixture needs porting to be usable here. ARKlight-py's
`examples/hello_site` is ordinary site-authoring code, not pipeline
implementation, so it needs no C equivalent written by hand — the same
file doubles as a manual smoke-test input for carklight from Stage 3
onward (the first stage that produces a full IR tree from real input),
the same way it already does for ARKlight-py itself.

---

## Stage -1 — Docs

**Scope:** this repository's own documents
(`PROPOSAL.md`, `ADDENDUM.md`, `TERMINOLOGY.md`, `ARKVM.md`,
`DESIGN-NOTES.md`, this file) don't mirror anything in ARKlight-py —
the Python project has no equivalent need to document a C ABI, a sync
model, or a backend interface to itself. This stage covers keeping
that set internally consistent as it's written: terminology settled
in one document (e.g. `TERMINOLOGY.md`'s IR-vs-`.arklight` split, or
`ADDENDUM.md` §4.1's backend interface) gets propagated to every other
document still using older phrasing, rather than left to drift.

**Test fixtures:** none — there's no code yet for a fixture to exercise.
The check is editorial: every cross-reference between these documents
still points at a section that exists and says what the reference
claims it says.

**Explicitly deferred:** anything Stages 0–8 cover below. This stage
is only about the documents agreeing with each other and with the
architecture they describe, before there's code to hold them
accountable to it.

**Why numbered before Stage 0 rather than left unnumbered:** it's
still ordered, boundable work with its own "done" condition, the same
as every other stage — it just produces prose instead of C, and
happens continuously alongside whichever numbered stage is current
rather than being finished once and left behind.

---

## Stage 0 — Data model & header (no logic)

**Status: implemented.** `include/carklight.h`, `core/`, and
`tests/test_stage0_alloc.c`.

**Scope:** `carklight.h` — the `ArkNode`/`ArkSite`/`ArkBuildResult`
opaque struct declarations, the `component_type_t` enum (hand-written
for now; generation is Stage 8), and a matched `ark_*_new`/`ark_free_*`
pair for every type. No pipeline logic runs. No test fixtures apply —
the "test" is: build a tree by hand, free it, run under a memory
checker (`valgrind`/ASan), confirm zero leaks and zero double-frees.
Implemented as `tests/test_stage0_alloc.c`, run plain via `ctest` and
under LeakSanitizer/AddressSanitizer via
`-DCARKLIGHT_ENABLE_ASAN=ON` at configure time (valgrind works too,
against the same binary — ASan is the default here only because it
needs no separately installed tool).

**Explicitly deferred:** any actual normalize/validate/render logic;
IR (de)serialization (Stage 7); the public `ark_load_ir` contract
itself, though its *shape* should already be sketched here since it
constrains what the structs need to support.

**Why first:** every later stage inherits whatever ownership decisions
get made here. Getting "who allocates, who frees, who borrows" settled
on paper — before any real data flows through it — is the single
highest-leverage decision in the whole project. Revisiting it later
means touching every stage downstream.

---

## Stage 1 — Normalize

**Scope:** mirrors `arklight.ir.normalize` — flatten nested lists,
drop `None`/`False`-equivalent children, wrap bare strings as `Text`
nodes. Pure recursive tree transformation: `ArkNode* in → ArkNode*
out`, no strings-as-output, no I/O.

**Test fixtures:** `tests/test_normalize.py`, translated by hand into
C test cases (build the "before" tree via Stage 0 constructors, run
`ark_normalize`, assert the "after" tree matches the Python fixture's
expected shape).

**Explicitly deferred:** anything schema-aware — Stage 1 doesn't know
or care whether a component type is valid, only how to flatten/prune
generic tree shape.

**Why here:** the cleanest possible introduction to recursive
tree-walking in C, with a hard boundary (no schema knowledge needed
yet) that keeps the scope genuinely small.

---

## Stage 2 — Validate

**Scope:** mirrors `arklight.ir.validate` — schema membership check
(is this a known `component_type_t`?), required-prop presence, the
text-only-children rule. Introduces the schema table (hand-written,
mirroring `SCHEMA`'s shape) and the `char** err_out` error-propagation
convention for the first time, since this is the first stage that can
meaningfully fail on bad input rather than just transform it.

**Test fixtures:** `tests/test_validate.py` — both the passing cases
and, importantly, the *rejection* cases (missing required prop, wrong
child type in a text-only component), since a validator that only
tests the happy path isn't tested.

**Explicitly deferred:** IR construction; anything backend-specific.

**Why here:** first stage where getting the error message right
matters as much as getting the logic right — sets the `err_out`
pattern every later fallible function reuses.

---

## Stage 3 — Website IR / build

**Scope:** mirrors `arklight.ir.build` — converts the validated,
normalized ARK-AST-shaped tree into the backend-independent `IRNode`
tree (type/props/children, modeling intent rather than HTML).
Mechanically the simplest stage relative to its neighbors, but it's
the checkpoint the existing Python pipeline itself treats as a
distinct stage, so carklight keeps the same seam.

**Test fixtures:** `tests/test_ir_build.py`.

**Explicitly deferred:** all rendering — this stage produces a tree,
never text.

**Why here:** small and low-risk on purpose, positioned as a breather
between the two validation-heavy stages before it and the
string-generation-heavy stages after it.

---

## Stage 4 — HTML backend

**Scope:** mirrors `arklight.backend.html.render` — the first stage
producing actual output bytes, and the first backend written against
the `ArkBackend` interface (`ADDENDUM.md` §4.1): its `render` fills an
`ArkBuildResult` from a validated IR tree, `init`/`postprocess`/
`shutdown` left `NULL` in v1. Tag mapping per component type,
attribute escaping, internal `Link`/`Image` href/src rewriting to
relative file paths.

Any later revision to ARKlight-py's HTML backend — a refactor of
`arklight.backend.html.render` itself, or an htmx-based rewrite of the
`on_click`/`behavior_target` vocabulary it renders — is not a special
case for carklight to plan around ahead of time. It becomes eligible
the same way any other feature does, per `PROPOSAL.md` §2 (shipped,
then soaked through a subsequent ARKlight release unchanged), and
lands here as a new implementation of the same `render` entry against
the same interface, confined to `backends/html/` and never touching
`backends/css/`, `backends/js/`, or `core/`.

**Test fixtures:** `tests/test_html_backend.py` — deliberately the
largest fixture file in the existing suite, which makes this stage the
most immediately checkable: build a tree, render it, `diff` the output
string against what ARKlight-py produced for the identical input.

**Explicitly deferred:** CSS/JS backends (Stage 5); this stage only
needs to satisfy the "byte-for-byte where the backend guarantees
deterministic serialization" bar from the proposal's parity-testing
section (§3.3) for HTML text specifically.

**Why here, and why alone rather than bundled with CSS/JS:** string-
building discipline (buffer growth, escaping correctness, no
format-string bugs) is the real risk in this stage, and it deserves to
be debugged in isolation rather than simultaneously with two smaller,
structurally similar backends that would just multiply the same class
of bug across three code paths at once.

---

## Stage 5 — CSS + JS backends

**Scope:** mirrors `arklight.backend.css.render` (the default
stylesheet, intrinsic-responsive-layout utility classes, custom
`site.style()` classes) and `arklight.backend.js.render` (the tiny
fixed behavior/action runtime — `BEHAVIOR_REGISTRY`/`ACTION_REGISTRY`).
Each is its own `ArkBackend` implementation (`ADDENDUM.md` §4.1) in
its own `backends/css/`/`backends/js/` directory, same interface Stage
4 already established. Grouped into one stage because both are
structurally "render a fixed, closed vocabulary as text," same shape
as Stage 4 but smaller — doing HTML alone first means whatever
string-generation lessons got learned there apply directly here
instead of being learned three times over.

Of the two, CSS is the narrower port: a closed set of utility classes
and a default stylesheet, with the least translation needed between
ARKlight-py's shape and C's beyond ordinary buffer/ownership handling
— it carries over closest to as-is of anything in v1. JS carries the
same interface but a larger closed vocabulary
(`BEHAVIOR_REGISTRY`/`ACTION_REGISTRY`), and — like the HTML backend
refactor noted in Stage 4 — any future expansion of that vocabulary
upstream (ARKlight's `v0.044` JS capability work, or an htmx-based
`on_click`/`behavior_target` rewrite, per `PROPOSAL.md` §1's Milestones
reference) syncs in on the same §2 schedule, as a new `render`
implementation confined to `backends/js/`.

**Test fixtures:** `tests/test_css_backend.py`, `tests/test_js_backend.py`.

**Explicitly deferred:** `postprocess` hook wiring beyond what's
needed to satisfy existing fixtures — both backends leave it `NULL`
per §4.1 until something actually needs it; that's pipeline
orchestration, not backend logic, and belongs conceptually with
Stage 7's ABI wiring.

---

## Stage 6 — `.ark` pack/unpack

**Scope:** mirrors `arklight.packer` — the polyglot byte layout,
sealed-by-default encryption (HMAC-SHA256/PBKDF2, hand-implemented,
no crypto dependency per the proposal's non-goals), `--plain`/
`--passphrase` handling, and real filesystem I/O for the first time
(reading a build directory, writing a bundle file).

**Test fixtures:** `tests/test_pack.py`.

**Explicitly deferred:** nothing pipeline-related — this stage only
ever reads already-built output, mirroring `arklight.packer`'s own
"never imports the parser/ir/backend internals" boundary.

**Why deliberately late:** this is the first stage combining two new
risk categories at once — real I/O and hand-rolled crypto — and it
specifically wants Stages 0–5 already solid underneath it, so any bug
here is legible as a Stage-6 bug rather than tangled up with pipeline
correctness questions from earlier stages.

---

## Stage 7 — IR (de)serialization + public ABI wiring

**Scope:** the seam every other language eventually depends on.
Implements `ark_load_ir` — deserializing the canonical IR byte format
(`PROPOSAL.md` §3.2/§3.4) into the Stage-0 struct model —
and finalizes `ark_build`/`ark_result_*`/`ark_write_output` as the
actual, stable, documented public entry points. `ark_load_root` (the
lower-level, non-primary escape hatch per the proposal's corrected
§3.4) is also wired here, clearly marked as secondary to `ark_load_ir`.

**Test fixtures:** `tests/test_pipeline_end_to_end.py`, plus new
round-trip tests specific to this stage (serialize a tree to IR bytes,
deserialize it back, confirm it matches; feed genuinely malformed IR
bytes and confirm a clean error rather than a crash).

**Explicitly deferred:** any actual language binding (Python `ctypes`,
JS N-API/CLI-spawn, future Rust/Go bindings) — those consume this
stage's output but are separate projects with their own repos/
timelines, not part of core carklight.

**Why last:** every stage before this one is testable purely within
C, against fixed input trees. This stage is where the ABI boundary
itself — the thing every future language binding has to trust — gets
built and hardened. Doing it last means it's the only thing left to
debug once the compiler core underneath it is already known-correct,
rather than debugging the boundary and the internals simultaneously.

---

## Stage 8 — Schema/table generation tooling (optional, later)

**Scope:** `tools/sync.py` (per the proposal's §2 sync model) — reads
a live, `DONE`-and-soaked `arklight.ir.schema.SCHEMA` from a given
ARKlight version and mechanically regenerates the Stage 0/2 tables,
the Stage 7 IR (de)serialization code, and the C builder function
signatures, instead of those being hand-maintained.

**Why optional and explicitly last:** hand-written tables are
completely sufficient to get carklight v1 (the ARKlight v0.0431
baseline) fully working end-to-end through Stage 7. Generation
tooling only starts paying for itself once there's a *second* sync to
perform (carklight v2, per the proposal) — building it before that
point is speculative infrastructure for a problem that doesn't exist
yet.

---

## Summary table

| Stage | Mirrors (ARKlight-py) | Introduces | Fixture source |
|---|---|---|---|
| -1 | — (new to this repo) | doc cross-consistency | none — editorial |
| 0 | — | struct model, alloc/free discipline | manual + memory-checker only |
| 1 | `ir.normalize` | recursive tree transforms | `test_normalize.py` |
| 2 | `ir.validate` | schema table, error propagation | `test_validate.py` |
| 3 | `ir.build` | IRNode tree construction | `test_ir_build.py` |
| 4 | `backend.html.render` | string generation, escaping | `test_html_backend.py` |
| 5 | `backend.css/js.render` | closed-vocabulary rendering | `test_css_backend.py`, `test_js_backend.py` |
| 6 | `packer.*` | file I/O, hand-rolled crypto | `test_pack.py` |
| 7 | (new) | public ABI, IR (de)serialization | `test_pipeline_end_to_end.py` + new round-trip tests |
| 8 | (new, optional) | codegen from live `SCHEMA` | none — tooling, not pipeline |

Stages 0–7 are what "carklight v1, baseline ARKlight v0.0431" actually
consists of, per `PROPOSAL.md` §6. Stage 8 is infrastructure
for carklight v2 and beyond.
