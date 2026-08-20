# ARKlight — Evolution Tracking: could libgit2 earn a place in the pipeline?

Status: **exploratory design note, not committed.** Nothing here is
scheduled, implemented, or agreed to. It exists to write down a real
case for embedding a history/diff engine into the compile pipeline —
and, just as importantly, the costs that case has to clear before
it's worth building. Treat every claim below as "worth weighing," not
"decided."

---

## 1. The idea, stated precisely

Not: "use git to version-control the project" — that's just normal
git usage, orthogonal to ARKlight itself, and not what this doc is
about.

The actual proposal: **give the compiler internal, structural memory
of how a project's IR has evolved across compiles**, so diagnostics
can be phrased in terms of *what changed and when*, not just *what's
wrong right now*. Concretely, a component of the pipeline that:

1. Snapshots the normalized IR after each successful build.
2. Can diff the current (possibly broken) IR against the last known-
   good snapshot.
3. Feeds that diff into error reporting — so a validation failure
   doesn't just say *what* invariant broke, but *what changed since
   it last held*.

libgit2 is one candidate engine for step 1 and 2 — content-addressed
storage plus a diff API, already written, already battle-tested. It
is not the only candidate (§5), but it's a reasonable one to evaluate
on its merits.

---

## 2. Why this is a real gap, not a nice-to-have

Standard compilers are stateless across invocations: each run starts
from nothing but the source in front of it. When a change ripples
into a cascade of downstream errors, the compiler reports the
symptoms it can see *now* — it has no notion of "this used to work,
here's what's different." The developer is left doing the historical
reasoning by hand: scrolling back through edits, or their own memory,
to reconstruct what they changed that broke it.

ARKlight's own architecture makes this gap more expensive than usual
to leave unaddressed, for two project-specific reasons:

- **The IR is a tree of intent, not a flat token stream.** A single
  edit — a layout class, a responsive breakpoint, a component's
  nesting — can invalidate many descendants at once. A stateless
  validator can only report the invalidations, not their common
  ancestor. Structural history is exactly the information needed to
  find that ancestor.
- **`normalize.c`/`validate.c`-style invariants (`ADDENDUM.md`) are
  enforced at IR-build time, project-wide.** A violation is a
  property of the *whole tree*, not a single line — so "which single
  line is wrong" is often the wrong question to even be answering.
  "Which edit, relative to the last valid tree, produced this" is
  the more honest question, and it's a historical one by nature.

This is a genuine instance of Poka-Yoke thinking extended one level:
most mistake-proofing stops changes from being *representable* at
all (type systems, schema validation). This is the next layer down —
catching a change that *is* representable, and *is* individually
valid syntax, but breaks an invariant that held across the project's
history. That's a class of error a stateless compiler structurally
cannot explain well, no matter how good its parser is.

---

## 3. What this would let ARKlight say

Illustrative, not a committed error-format spec:

```
[ARK_ERR] Layout validation failed — site.hero.width

  This construct validated cleanly as of your last successful build.

  Since then:
    hero.width       : static → responsive     (site.py:42)
    hero.children[3]  : unchanged
    hero.children[*]  : 14 descendants inherit width from this node

  The responsive change is the only edit touching this subtree.
  That's the most likely cause — check the breakpoint values you
  passed, not the children.
```

The value isn't the diff by itself — a plain `git diff` on the
author's own source already gives them that, for free, no engine
required. The value is the compiler doing the *correlation*: tying a
specific structural change to a specific downstream validation
failure, automatically, at the moment the failure is reported. That
correlation is not something an author's own git history does for
them; it requires the compiler to understand its own IR well enough
to walk the diff and the failure together.

---

## 4. What actually has to be built — and what doesn't

This is the part worth being precise about, because it's easy to
credit libgit2 with more than it does.

**What libgit2 (or an equivalent) provides:**
- Content-addressed, deduplicated storage of tree snapshots.
- A tested, correct diff algorithm over that storage.
- Pack/delta compression, so keeping many historical snapshots stays
  cheap on disk.

**What it does not provide, and would have to be built regardless of
storage engine:**
- **Structural (AST/IR-aware) diffing.** libgit2's diff is
  byte/line-oriented. Turning "these bytes changed" into "the
  `width` field on this node changed from `static` to `responsive`"
  requires an IR-aware diff pass written against ARKlight's own node
  types (`arklight/ir/`, `arklight/ast/`) — libgit2 supplies the
  storage and the byte-diff underneath it, not the semantic layer on
  top.
- **The correlation step.** Deciding *which* diffed change is the
  likely cause of *which* validation failure is a piece of compiler
  logic specific to ARKlight's invariants (`arklight/compiler/`,
  `tests/test_validate.py`), independent of what's storing the
  snapshots.

Put plainly: the storage/diff substrate is maybe a third of this
feature. The other two-thirds — semantic diffing and correlation —
get built either way, on top of whatever engine is chosen.

---

## 5. Costs and open questions this has to clear

None of these are disqualifying on their own. They're the honest
list of what "embed libgit2" would actually commit the project to,
and should be weighed against a purpose-built alternative before
either is chosen.

- **Dependency surface.** libgit2 pulls in zlib and typically a
  TLS/crypto backend transitively, even with no remote/network
  operations ever touched — SHA hashing and pack support are
  load-bearing, not optional extras. For `carklight` specifically,
  this directly contradicts an explicit, stated design property —
  *"Zero external dependencies. No crypto library..."*
  (`C_ARKlight/README.md`). If evolution tracking is meant to live in
  carklight rather than ARKlight, that claim would need to be
  retracted, not just re-scoped — an internal module boundary hides
  the *API*, not the fact that the dependency tree changed.
- **Where it belongs.** ARKlight (this repo) is where projects
  actually evolve build-to-build — carklight deliberately tracks
  behind it, frozen per release, and is explicitly "not ready to
  sortie yet." An evolution-tracking feature aimed at helping authors
  understand their own edits belongs, if anywhere, in the fast-moving
  authoring layer where those edits happen — not retrofitted into the
  slow, minimal, frozen C backend whose whole design premise is
  staying small and stable.
- **Compile-time cost.** Snapshotting after every successful build,
  and diffing on every failed one, sits in the hot path of an
  edit-compile-edit loop — exactly the workflow where added latency
  is most noticeable. This needs a real budget and a real
  benchmark, not an assumption that content-addressed storage is
  "basically free."
- **Snapshot granularity is unresolved.** Every successful build?
  Every explicit save point? User-triggered only? This determines
  both the storage growth curve and how much signal the diffs
  actually carry — too fine-grained and diffs are mostly noise, too
  coarse and the "last known-good" reference point is stale.
- **A full git object model may be more than this needs.** Refs,
  branches, and merge resolution exist to reconcile *concurrent,
  divergent* histories — collaborative editing, distributed clones.
  Evolution tracking as scoped here is a single linear sequence of
  snapshots per project, authored by one pipeline. A smaller,
  purpose-built append-only content-addressed log (snapshot + diff,
  nothing else) may deliver the same capability in §3 without
  either the dependency weight of §5's first point or a surface area
  (branches, merges, remotes) this feature will never use.

---

## 6. What this doc is not claiming

- That libgit2 specifically is chosen, or ruled out. §5's last point
  in particular argues the opposite might be simpler — that's
  flagged as an open question, not resolved here.
- That this lives in carklight. If built, ARKlight (Python, where
  authoring and evolution actually happen) is the more defensible
  home; carklight's frozen, dependency-free design is a reason to
  keep this feature out of it, not a reason to route around that
  design.
- That this is scheduled. Same status as `C_ARKlight/docs/DESIGN-
  NOTES.md`'s `C_ARKlight_gui.so` — a shape worth having on record,
  not a milestone.

---

## 7. If this moves forward, the next real question

Not "which library" — "what does a snapshot actually contain, and at
what granularity." That question is prior to any engine choice, and
answering it (not picking libgit2 vs. a custom log) is the actual
next design step, whenever this is picked back up.
