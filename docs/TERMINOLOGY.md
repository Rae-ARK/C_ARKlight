# carklight — Terminology: IR vs. `.arklight`

Status: canonical, effective as of Stage -1 (see
[`IMPLEMENTATION.md`](./IMPLEMENTATION.md)).
Supersedes any place in `PROPOSAL.md`, `ADDENDUM.md`,
or `README.md` that still uses "IR" and "`.arklight`" interchangeably.
This is a short, standalone doc on purpose — it's the one page every
other doc should agree with, not a place to re-litigate the pipeline.

---

## The one-line version

**IR is for humans. `.arklight` is for machines.**

These are not two names for the same artifact. They're two different
intermediary forms of the same content, each shaped for a different
reader — and both genuinely exist as intermediaries between the
source-level tree and whatever a backend eventually produces.

---

## IR — the human-facing intermediary

"IR" (Intermediate Representation) names the *conceptual*,
backend-independent tree — type/props/children, modeling intent
rather than markup — that sits between validation and codegen in the
pipeline (`PROPOSAL.md` §3.1's "Website IR" stage). IR is
what:

- shows up in docs and diagrams describing the pipeline
- a person reads when a test fixture says "the IR for this input
  looks like ___"
- a developer reads when debugging why a build produced the wrong
  output — a legible dump of the tree, not a byte layout
- schema documentation and `arklight search <name>` describe

IR is a *shape*, not a wire format. It doesn't need magic bytes, a
version tag, or a string table, because it never has to survive being
written to disk and read back by a different process, possibly days
later, possibly by a different language's binding. It exists to be
read by a person, in the moment, in whatever representation is most
legible — a pretty-printed tree, a JSON-ish debug dump, prose in a
doc. There is no single canonical human-readable serialization of IR
mandated by this doc; that's a tooling choice (e.g. `arklight inspect
--ir`), not a spec.

## `.arklight` — the machine-facing intermediary

`.arklight` (`ADDENDUM.md` §2) is the *binary encoding* of
that same tree — magic bytes, format version, schema-generation tag,
string table — built specifically to cross a process boundary, outlive
one build, and be read by a different program than the one that wrote
it. Nothing about `.arklight` is meant for a person to read directly;
it exists to be read by carklight, and only by carklight.

## What this changes going forward

Earlier docs described a language frontend "serializing to canonical
IR bytes" and calling `ark_load_ir`. That phrasing conflated the two.
Corrected:

> A language frontend builds an IR tree — its own in-memory,
> human-inspectable form. It then *encodes* that IR into a `.arklight`
> file. carklight reads the `.arklight` file. "IR" describes what's
> being encoded; `.arklight` describes what's actually on disk and
> what actually crosses the boundary.

The public loading entry point on the C ABI is renamed to match, so
the name itself stops implying that IR is the wire format:

```c
// was (PROPOSAL.md §3.4, pre-addendum):
ArkSite* ark_load_ir(const uint8_t* ir_bytes, size_t len, char** err_out);

// now:
ArkSite* ark_load_arklight(const uint8_t* bytes, size_t len, char** err_out);
```

Signature shape is unchanged — bytes in, length, an out-param for
errors. Only the name changes, because it was the name doing the
conflating, not the mechanism.

`ark_load_root` (`PROPOSAL.md` §3.4/§4) is unaffected by any
of this — it was never IR-shaped or `.arklight`-shaped to begin with.
It ingests an already-built native `ArkNode*` tree in-process, which
is a third, narrower thing: C's own escape hatch, not an intermediary
at all.

## Quick reference

| | IR | `.arklight` |
|---|---|---|
| Audience | humans | machines |
| Form | conceptual tree / debug dump | binary file on disk |
| Lifespan | exists for the moment it's read | outlives a process, versioned, portable |
| Needs a spec / version / magic bytes? | no | yes — `ADDENDUM.md` §2 |
| Crosses the C ABI boundary? | no, not directly | yes — the only thing that does |
| Loaded via | — (read/discussed, not "loaded") | `ark_load_arklight()` |
| Produced by | every stage of the pipeline, conceptually | one specific encode step, per language frontend |

This document is the tie-breaker anywhere `PROPOSAL.md`,
`ADDENDUM.md`, or `README.md` still show the old conflated
phrasing. Bringing those into line with it is exactly what Stage -1
(docs-only) is for — see `IMPLEMENTATION.md`.
