# carklight — ARKVM: one name, two modes

Status: design draft, not yet implemented. Written to resolve a naming
collision across this project before it causes real confusion, and to
record the `--minimal`/`--full` split as the current best answer —
not to claim either mode is built.

---

## 1. The collision this resolves

"ARKVM" has been used to mean three different things across this
project's own repos:

1. **carklight's own docs** (`ADDENDUM.md` §3) — a *compile-time
   multi-target dispatcher*. `.arklight` in, one backend runs, output
   comes out, done. Explicitly **not** a runtime: "no on-device
   execution model, no bytecode interpreter shipped inside an
   installed app."
2. **The streaming prototype** (`State-Driven-UI-Streaming-Prototype`,
   `client/src/arkvm/ARKVM.js`) — a small **browser-side JS runtime**
   that watches per-field update latency over SSE and promotes fields
   from a server-rendered path to a direct-JSON path at runtime.
3. **This document's subject** — a proposed **on-device native
   runtime**: boots, loads `.arklight`, binds to a live state source
   through a `StateProvider`-style abstraction, renders, and re-renders
   on state updates.

Three definitions under one name is the actual problem. This doc
doesn't invent a fourth — it picks a shape where (1) and (3) are the
same tool at different flags, and treats (2) as a separate, open
question (§5).

---

## 2. One binary, two modes

`ARKVM` stays one tool. What changes is scope, selected explicitly —
same instinct as `qemu-user` vs. `qemu-system`: one project, one
mental model, two honest scopes, not two competing tools.

```
ARKVM --minimal   site.arklight
      load → dispatch to one backend → emit output → exit
      (this is exactly ADDENDUM.md §3's existing model, unchanged)

ARKVM --full      site.arklight
      load → bind to a configured StateProvider → render →
      hold the process open → re-render on state updates
      (new — the on-device runtime reading from §4)
```

**`--minimal` is the default.** A bare `ARKVM site.arklight` behaves
exactly as `ADDENDUM.md` already documents today: compile-time only,
process exits when the build finishes. `--full` is opt-in, never
implied, never silently upgraded into.

This changes one existing claim and should be edited deliberately, not
left to sit alongside a contradiction: `ADDENDUM.md` §3's "no
on-device execution model... ever" is accurate for `--minimal` (the
default, and the only mode that exists today) but not for a
hypothetical `--full`. If `--full` is ever built, that sentence needs
to become "no execution model in default/minimal mode; `--full` is a
separately-scoped, opt-in exception" — rather than the absolute
statement it is now.

---

## 3. Why a flag isn't enough on its own

On a Pi-class target (Pi Zero 2 W and up), `--minimal`/`--full` as a
pure runtime flag is fine — both modes comfortably fit in flash/RAM,
and choosing between them at launch costs nothing.

On MCU-class targets (ESP32, Pi Pico), it can't stay runtime-only.
There's no RAM to spare for `--full`'s code sitting linked-in and
unused just because the flag defaults to off. This needs to be the
same kind of **build-time** modularity `ADDENDUM.md` §4 already set up
for backends (`-DCARKLIGHT_BACKEND_ANDROID=OFF` at CMake configure
time) — a target either has `--full` compiled in or it doesn't, and
the CLI flag is a convenience layer on top of that for the targets
where both modes genuinely coexist in one binary.

```
Pi-class targets:      one binary, --minimal/--full chosen at launch
MCU-class targets:     --full support is a build-time inclusion
                        decision, not just an unused runtime path
```

---

## 4. `--full` mode: `StateProvider` and capabilities

This is the part that's genuinely new relative to `ADDENDUM.md` —
recorded here as the current best shape, not as something decided.

**`StateProvider`** is the dependency-injected abstraction the UI is
written against, never a concrete transport:

```
ARKlight UI  ←  StateProvider  ←  one of:
                                    HTTP/SSE server
                                    local IPC
                                    sensor/device input
                                    filesystem
                                    native application process
                                    embedded platform API
                                    mock data (development)
```

The UI doesn't know or care which concrete provider it's bound to.
Same conceptual app, different `StateProvider`, different deployment:
digital signage (`→ HTTP/SSE`), an industrial panel (`→ local
device/sensor API`), a desktop app (`→ local process`), a dev build
(`→ mock data`).

**Targets advertise capabilities; apps ask for them.** `ARKVM --full`
should not promise "every target supports every ARKlight feature" —
it should expose what a given target actually has (display, touch,
network, filesystem, timers, GPIO, audio, camera, ...) and let an
application adapt or fail cleanly when something isn't there, rather
than assuming uniformity across a Pi Zero, an ESP32, and a Pico that
manifestly don't have it.

**Capability negotiation likely needs to go deeper than the
peripheral list**, specifically for MCU-class targets. A Pi-class
target can plausibly render the same component set `--minimal`
already targets for HTML/CSS/JS. An ESP32 or Pico cannot run anything
resembling that rendering model at all — a realistic MCU backend is
closer to an immediate-mode widget renderer (LVGL-shaped) than to a
constrained version of the desktop/Android GUI backend
(`DESIGN-NOTES.md` §2's `C_ARKlight_gui.so`). What's actually shared
across every target in that case is the `.arklight` schema and the
state-shape — not the rendering code. So capability advertisement for
MCU targets probably needs to say *which component types are even
representable*, not just which peripherals exist, or "the same
`.arklight` app runs everywhere" stops being true the moment it's
tested on real hardware.

---

## 5. Explicitly open

- **Whether `--full` gets built at all.** Nothing here commits to it —
  same status as `DESIGN-NOTES.md`'s `C_ARKlight_gui.so`: a shape
  worth having written down, not a scheduled milestone.
- **Whether the streaming prototype's `ARKVM.js` is a prefiguring
  prototype of `--full` mode**, reimplemented natively later, or a
  permanently separate JS-specific thing that happens to share a
  name. Not resolved here — flagged so it doesn't quietly become a
  fourth definition once `--full` is real.
- **Exact flag names, config format for `StateProvider` selection,
  and where the capability manifest lives** (compiled into the
  binary vs. a target-description file `ARKVM` reads at startup) —
  all open, all secondary to the mode split itself.
- **Per-target minimum viable `--full` support.** Pi-class is
  plausible today; ESP32/Pico plausibility depends on how far the
  renderer has to shrink before "the same conceptual application"
  stops being an honest description (§4).

---

## 6. Relationship to the rest of the docs

This document sits alongside, not above, the existing set:

- [`ADDENDUM.md`](./ADDENDUM.md) §3 defines `--minimal`'s existing
  behavior and is the one place whose wording needs to change if
  `--full` is ever built (§2, above).
- [`DESIGN-NOTES.md`](./DESIGN-NOTES.md) covers the desktop/Android
  GUI backend shape (`C_ARKlight_gui.so`); `--full` mode's Pi-class
  story and that document's desktop/Android story are related but
  not identical — one is about a shared renderer, this one is about a
  shared runtime-mode split.
- [`TERMINOLOGY.md`](./TERMINOLOGY.md)'s IR-vs-`.arklight` distinction
  is unaffected either way: `--full` mode still only ever loads
  `.arklight`, same as `--minimal`; live state updates are a separate
  channel (`StateProvider`), not a second encoding of the UI tree
  itself.
- [`PROPOSAL.md`](./PROPOSAL.md)'s sync model and non-goals are
  unaffected by this document directly — but §5's "re-executing user
  code in C" non-goal is worth re-reading against `--full` mode
  specifically, since a live `StateProvider` binding is closer to
  that boundary than anything `--minimal` does today, even though it
  isn't re-executing *user* code in the sense that non-goal was
  written to rule out.
