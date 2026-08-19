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
ArkSite* site = ark_load_ir(ir_bytes, ir_len, &err);
ArkBuildResult* result = ark_build(site, 0, &err);
ark_write_output(result, "ARK", &err);
```

That's the whole surface most callers ever touch.

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
make              # libcarklight.a, libcarklight.so
make test          # runs against ARKlight's own test fixtures
make install        # installs the header, the libraries, a .pc file
```

Zero external dependencies. No crypto library, no build system beyond
`make`. If it compiles, it links.

---

## The API

```c
#include "carklight.h"

char* err = NULL;

ArkSite* site = ark_load_ir(ir_bytes, ir_len, &err);
ArkBuildResult* result = ark_build(site, /* default backends */ 0, &err);

ark_write_output(result, "ARK", &err);
ark_pack("ARK", "site.ark", /* passphrase */ NULL, /* plain */ 0, &err);

ark_free_result(result);
ark_free_site(site);
```

One boundary in, one function to build, one to pack. Every language
binding — Python, JavaScript, anything with C FFI — talks to carklight
through the same serialized IR, described in full in
[`CARKLIGHT-PROPOSAL.md`](./CARKLIGHT-PROPOSAL.md).

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
intentional — see [`CARKLIGHT-PROPOSAL.md`](./CARKLIGHT-PROPOSAL.md)
for the sync model, and
[`CARKLIGHT-IMPLEMENTATION.md`](./CARKLIGHT-IMPLEMENTATION.md) for how
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

## License

Tracks upstream ARKlight's license — see
[Rae-ARK/ARKlight `LICENSE`](https://github.com/Rae-ARK/ARKlight/blob/alpha/LICENSE).
