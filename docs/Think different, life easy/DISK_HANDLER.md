# disk_handler — proposed disk I/O abstraction (deferred)

**Status:** draft / reference notes, not an official carklight repo
document. Written against `C_ARKlight` as of commit `800968b`
("Stage 5b - JS Backend done.") for the user's own reference.

Companion to `CORE_HANDLER.md`. Unlike that proposal, **this one is
explicitly a "decide later, not now" recommendation** — see §4.

---

## 1. Current state

Zero disk I/O exists anywhere in the codebase as of Stage 5b:

```
$ grep -rn "fopen\|fwrite\|fread\|FILE \*" core/ backends/
(no results)
```

This is expected, not a gap — per `docs/IMPLEMENTATION.md`, Stage 6
(`.ark` pack/unpack) is explicitly the first stage introducing real
filesystem I/O, and it hasn't started yet. There is currently nothing
to extract into a `disk_handler`; any such module today would be new,
speculative code, not a refactor.

## 2. What Stage 6 is documented to actually need

Per `docs/IMPLEMENTATION.md`'s Stage 6 section, the scope is narrow
and specific:

- Read an already-built output directory (the result of Stages 4–5).
- Write one polyglot bundle file (`.ark` format).
- Hand-rolled HMAC-SHA256 + PBKDF2 for sealed-by-default encryption
  (no crypto dependency, per the proposal's stated non-goals).
- `--plain` / `--passphrase` CLI handling.
- Explicitly deferred: nothing pipeline-related — this stage only
  ever reads already-built output, mirroring `arklight.packer`'s own
  "never imports the parser/ir/backend internals" boundary.

That's it — no generalized filesystem abstraction is called for by
the stage's own documented scope.

## 3. Where an abstraction WOULD have a real (non-speculative) justification

`docs/ARKVM.md` discusses MCU-class targets (ESP32, Pico) for a future
on-device runtime. Those targets may not have a POSIX filesystem at
all. **If** the `.ark` packer built in Stage 6 is ever expected to run
in that environment too (not just desktop/server), a thin I/O
injection seam would matter for real:

```c
typedef struct ArkDiskBackend {
    int  (*read_file)(struct ArkDiskBackend* self, const char* path,
                       uint8_t** data_out, size_t* len_out, char** err_out);
    int  (*write_file)(struct ArkDiskBackend* self, const char* path,
                        const uint8_t* data, size_t len, char** err_out);
} ArkDiskBackend;
```

Same vtable shape as the existing `ArkBackend` interface used by
HTML/CSS/JS — consistent with the project's established pattern for
this kind of swappable dependency.

## 4. Why this should be decided at Stage 6, not now

- Building this ahead of Stage 6 having any real I/O logic to
  abstract is exactly the kind of thing the project has already
  explicitly avoided elsewhere: Stage 8 (schema codegen tooling) is
  deliberately deferred with the stated reasoning **"speculative
  infrastructure for a problem that doesn't exist yet."** The same
  reasoning applies here.
- Whether MCU/no-filesystem targets are actually in scope for the
  `.ark` packer (as opposed to only the separate, still-hypothetical
  `--full` ARKVM runtime mode) is an open question the project hasn't
  answered. Building the abstraction commits to an assumption that
  hasn't been confirmed.
- A vtable-based I/O seam adds real complexity (an extra indirection
  on every read/write) for zero benefit if every actual target turns
  out to have a normal filesystem — decide once real requirements are
  known, not preemptively.

## 5. Recommendation

Do not create a `disk_handler` directory now. Instead:

1. Let Stage 6 land using plain `fopen`/`fwrite`/`fread` (or POSIX
   equivalents) directly, matching the stage's actual documented
   scope.
2. Revisit the `ArkDiskBackend`-style abstraction only if/when
   embedded, no-filesystem targets become a confirmed requirement for
   the packer specifically (not just the separate, still-draft
   on-device runtime work in `docs/ARKVM.md`).
3. If it does become justified, extracting it at that point costs
   exactly one file's worth of `fopen`/`fwrite` calls (Stage 6 is
   scoped to be small and self-contained) — far cheaper to retrofit
   than the memory-allocation case in `CORE_HANDLER.md`, where 46
   call sites already exist across 9 files.

## 6. Open questions to raise with the project

- Is MCU/embedded execution actually planned to include the `.ark`
  packer, or only the separate `--full` ARKVM on-device runtime mode?
- If the packer never needs to run outside a normal filesystem
  environment, is any I/O abstraction here worth the indirection cost
  at all, even post-Stage-6?
