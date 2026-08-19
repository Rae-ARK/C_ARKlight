# carklight — Design Notes: desktop & Android backends

Status: **forward-looking notes, not a spec.** Nothing in this
document is scheduled, committed, or built. It exists so a shape that
keeps coming up in discussion has one place to live, instead of being
re-explained from scratch every time it's mentioned in
[`ADDENDUM.md`](./ADDENDUM.md) or [`PROPOSAL.md`](./PROPOSAL.md).
Treat every claim below as "this may or may not happen," not as a
roadmap commitment. Contrast with `PROPOSAL.md`'s Milestones table,
which does track real, versioned, `PLANNED`/`IN PROGRESS`/`DONE`
work — this document is deliberately upstream of that table, for
ideas that haven't earned a milestone yet.

---

## 1. Where this fits

`ADDENDUM.md` §3 already places desktop (`v0.060`) and Android
(`v0.080`) as later carklight backends, dispatched from `.arklight`
the same way HTML/CSS/JS are today:

```
site.arklight
      │
      ▼
carklight (C)   ── loads the file, dispatches to ONE target backend
      │
      ├── HTML/CSS/JS     (today)
      ├── desktop app     (v0.060, eventually)
      ├── android app     (v0.080, eventually)
      └── ...
```

This note is about one specific, recurring idea for how those two
backends might actually be shaped underneath — not a new pipeline
stage, not a new file format, just a note on internal packaging.

---

## 2. One internal GUI library, shared by both

The recurring idea: rather than a separate desktop-rendering
implementation and a separate Android-rendering implementation,
both backends could be thin, platform-specific shells around one
shared internal library — informally, **`C_ARKlight_gui.so`** — that
does the actual GUI-tree construction and event wiring once, in C,
and gets linked into both a desktop build and an Android build.

```
                 site.arklight
                       │
                       ▼
              carklight (C core)
                       │
                       ▼
            C_ARKlight_gui.so  (shared, internal)
              /                        \
   desktop shell (v0.060)      android shell (v0.080)
   (window/event glue for      (JNI/NDK glue for
    the host OS)                 the Android runtime)
```

Why this shape, if it happens:

- **One GUI-tree implementation, not two.** The desktop and Android
  backends would otherwise duplicate a lot of the same
  component-to-widget mapping logic. A shared internal `.so` means
  that logic is written and tested once, matching the same discipline
  `ADDENDUM.md` §4 already applies to HTML/CSS/JS — "a backend
  rewrite stays contained," extended one level further to two
  backends that are structurally closer to each other than either is
  to HTML/CSS/JS.
- **Internal, not public.** `C_ARKlight_gui.so` would sit *behind*
  the desktop and Android backends, not beside `libcarklight` as
  another public artifact. Callers still only ever see
  `libcarklight.so`/`.dll`/`.dylib` + `carklight.h`, exactly as
  `README.md` describes today — this is purely an internal
  implementation detail of how those two future backends happen to
  be built, not a new public surface, and not something any language
  binding would link against directly.
- **Portability is the actual point.** Because `carklight` backends
  already dispatch off `.arklight` — the binary, machine-facing,
  self-describing encoding of a site's IR (`TERMINOLOGY.md`) — a
  shared GUI library consuming that same file means the desktop and
  Android shells don't need their own bespoke ingestion path. The
  same portability property that lets any language binding hand
  carklight a `.arklight` file today is what would let one GUI
  library serve two very different host platforms without forking
  its input format. IR itself never enters this picture directly —
  it's the human-facing shape used in docs and debugging, same as
  everywhere else in the project (`TERMINOLOGY.md`); only `.arklight`
  would ever reach `C_ARKlight_gui.so`, or any backend.

---

## 3. Explicitly not decided here

- **Whether `C_ARKlight_gui.so` gets built at all**, versus the
  desktop and Android backends simply being written separately if
  that turns out simpler. Nothing about `ADDENDUM.md` §3's dispatch
  model requires them to share an implementation — it's a plausible
  shape, not a constraint the architecture imposes.
- **Whether these backends consume `.arklight` directly**, or operate
  on already-built HTML/CSS/JS output the way `ADDENDUM.md` §3 leaves
  open as "a real architectural decision for later." This note leans
  toward the direct-`.arklight` framing because it's the more
  portable one, but `ADDENDUM.md` is explicit that this isn't decided
  — repeated here rather than resolved.
- **Naming, exact linkage, file layout.** `C_ARKlight_gui.so` is a
  working name for discussion, not a filename commitment — it may
  end up named, structured, or split differently than sketched here
  by the time `v0.060`/`v0.080` are anywhere near real.
- **Timeline.** Both backends remain `PLANNED`, not `IN PROGRESS`, on
  ARKlight's own Milestones table (`PROPOSAL.md` §1). This document
  doesn't change that status — it's a note for whenever that changes,
  not a signal that it's changing soon.

---

## 4. Why this note exists now, ahead of any of it being real

Two purely documentation-hygiene reasons, both about *this* repo's
docs, not about pulling any implementation forward:

- So `ADDENDUM.md` §3's existing forward-references to
  `docs/DESIGN-NOTES.md` point at something real instead of a dead
  link.
- So the IR-vs-`.arklight` distinction (`TERMINOLOGY.md`) — for
  humans, for machines, and specifically *why* the machine-facing
  side is the portable one — has an explicit worked example in a
  context (two very different host platforms, one shared internal
  library) where that portability is the entire reason the idea is
  worth writing down at all.

Nothing here changes `PROPOSAL.md`'s sync model, `ADDENDUM.md`'s
compile-time model, or `IMPLEMENTATION.md`'s staged build-out. This
is additive, sits entirely downstream of all three, and is meant to
be revised or discarded freely as `v0.060`/`v0.080` actually take
shape.
