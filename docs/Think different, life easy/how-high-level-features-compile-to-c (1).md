# How High-Level Language Features Compile Down to C

A reference for understanding what's actually happening underneath Python,
Java, JS, Rust, Zig, C++, and friends — and, specifically, what `C_ARKlight`
(ARKlight's native/GTK compilation target) needs to implement by hand.

---

### Evidence legend (applies from §5 onward)

Sections that reason from the `tinycc`/`C_ARKlight` source rather than
general CS knowledge are tagged per-claim:

- **[A] Directly observed** — read out of the actual cloned source, with
  a file/function/line pointer you can go re-check yourself.
- **[B] Reasonable architectural inference** — not literally stated in
  either codebase, but follows from what *is* there plus how C/linkers/
  compilers work.
- **[C] Proposed for C_ARKlight** — a design recommendation, not a fact
  about either codebase. Disagreeing with a [C] claim doesn't mean
  disagreeing with the [A] evidence it's built on.

This distinction matters specifically to guard against "TinyCC does X"
quietly turning into "TinyCC proves X is the correct architecture" —
source archaeology is evidence, not a verdict.

---

## 0. Premise check: it's not *all* C underneath

- **C**: CPython (reference Python interpreter), GCC's backend
- **C++**: V8 (Chrome/Node's JS engine), JVM HotSpot (Java), SpiderMonkey
  (Firefox's JS engine), LLVM/Clang
- **Rust**: `rustc` is self-hosted (written in Rust, originally bootstrapped
  via OCaml), uses LLVM (C++) as its backend
- **Zig**: self-hosted (compiler written in Zig), also uses LLVM optionally

C and C++ dominate the substrate, but several "high-level" languages
self-host their own compilers once bootstrapped.

**Correction to an earlier draft of this doc:** "everything ultimately
compiles down to C" is not true, and the examples above already
contradict it — `rustc` and Zig's compiler lower through **LLVM IR**, not
C; V8 and SpiderMonkey generate **machine code directly** from their JITs
(no textual C pass at any point, even though the JIT engines themselves
are *written* in C++); JVM HotSpot's C2 JIT does the same; WASM, JVM
bytecode, and CLR IL are all real, C-free compilation targets in their
own right. The accurate claim is narrower:

> C and C++ are extremely common **implementation languages** for
> language runtimes and compiler infrastructure (CPython, V8, HotSpot,
> LLVM), and a lowering-to-C backend is one legitimate compilation
> strategy among several (others being direct machine-code emission, a
> bytecode VM, or an existing IR like LLVM's). High-level languages do
> not inherently route through C — but the mental exercise "what would
> this look like if it *did* lower through C" is still a genuinely useful
> way to see what a feature costs, which is what the rest of this
> document is for.

That reframing matters specifically for `C_ARKlight`, since it *is*
choosing C-as-target deliberately (§4/§5's GTK backend) rather than
because "that's what everyone does" — worth being precise about which of
those two things is actually true.

---

## 1. Core OOP / FP constructs

### Classes / objects → struct + function-pointer table

```c
typedef struct {
    int x, y;
    void (*draw)(void* self);   // vtable slot
} Shape;

void circle_draw(void* self) { /* ... */ }

Shape make_circle(int x, int y) {
    return (Shape){ x, y, circle_draw };
}
```

"Methods" are functions taking an explicit `self` pointer as the first
argument — literally what C++/Java/Python do under the hood (`this`/`self`
is a hidden first parameter). A **vtable** is a struct of function pointers;
**virtual dispatch** is an indirect call through that pointer instead of a
direct call — the extra pointer chase defeats inlining and hurts branch
prediction/I-cache locality, which is why hot polymorphic loops are often
slower than monomorphic ones.

### Interfaces → same idea, structural not nominal

```c
typedef struct {
    size_t (*read)(void* self, char* buf, size_t n);
    void   (*close)(void* self);
} Reader;
```

Any struct that fills in a matching `Reader` "implements" it — this is
literally Go's structural interfaces, and how the Linux kernel does
polymorphism everywhere (`struct file_operations`, `struct net_device_ops`).

### Inheritance → struct embedding + first-member trick

```c
typedef struct { int x, y; } Base;
typedef struct { Base base; int radius; } Circle;

Circle c;
Base* b = (Base*)&c;   // valid: base is the FIRST member, same address
```

Because `base` is the first field, `&c == &c.base`. This is how C++
compilers implement single inheritance (the base subobject sits at offset
0), and it's the pattern behind `container_of()` in the Linux kernel for
the reverse direction.

### Generics → three real strategies, real tradeoffs

1. **`void*` + function pointers** (type erasure, like pre-reification Java
   generics): `qsort(base, n, size, cmp)`. Cost: indirect call per
   comparison, no compiler specialization.
2. **Macros** (textual monomorphization, closer to C++ templates):
   ```c
   #define DEFINE_VEC(T) \
       typedef struct { T* data; size_t len, cap; } Vec_##T; \
       void Vec_##T##_push(Vec_##T* v, T val) { /* ... */ }
   DEFINE_VEC(int)   // generates Vec_int, fully typed, inlinable
   ```
   Exactly what C++ templates and Rust generics do at the machine-code
   level: **monomorphization** — a fully specialized copy per concrete
   type, so the compiler can inline and optimize as if hand-written. Cost:
   code bloat. Benefit: zero runtime dispatch overhead.
3. **`_Generic`** (C11 compile-time type dispatch): `#define print(x) _Generic((x), int: print_int, float: print_float)(x)`.

### Closures → explicit environment struct + function pointer

```c
typedef struct { int captured; } Env;
int add_captured(Env* env, int x) { return env->captured + x; }
```

What a closure compiles to under the hood everywhere else — a heap- (or
stack-, if it doesn't escape) allocated struct holding captured variables,
plus a function pointer, bundled together.

### Tagged unions → algebraic data types (Rust enums, Haskell ADTs)

```c
typedef enum { INT, STR } Tag;
typedef struct {
    Tag tag;
    union { int i; char* s; };
} Value;
```

The straightforward representation Rust's `enum` (with data) *can*
compile to — a discriminant tag plus a union of payloads, sized to the
largest variant. **Pattern matching** compiles to a `switch` on that tag
field, when the tag is actually laid out this way.

**Correction:** this is not a guarantee, only the naive baseline. An
optimizing compiler is free to exploit **niche optimization** — encode
the discriminant into bit patterns the payload type can never legally
hold, and drop the separate tag field entirely. The canonical example:
`Option<&T>` is a single machine word on `rustc`, because a null pointer
is not a valid `&T` value, so `None` *is* the null-pointer bit pattern and
`Some(ptr)` *is* the pointer itself — no tag byte anywhere. `NonZeroU32`,
enums over `bool`, and enums over other niche-bearing types get the same
treatment. The correct general principle:

> An algebraic data type only requires a representation *capable of
> distinguishing its variants* — a tagged union is the straightforward
> way to guarantee that, but an optimizing compiler may use a strictly
> more compact encoding whenever the payload types themselves already
> carry unused bit patterns.

### Exceptions → `setjmp`/`longjmp`

```c
jmp_buf env;
if (setjmp(env) == 0) {
    // try
    longjmp(env, 1);   // throw
} else {
    // catch
}
```

`setjmp` snapshots register state (incl. stack pointer, program counter);
`longjmp` restores it. C++ exceptions do something more elaborate (stack
unwinding with destructor calls via unwind tables — DWARF CFI on Linux),
but the primitive idea is the same.

### Async/await, generators, coroutines → explicit state machines

```c
typedef struct {
    int state;
    int i;          // hoisted "local" that survives across yields
} Generator;

int gen_next(Generator* g) {
    switch (g->state) {
        case 0: g->i = 0; g->state = 1; return g->i;
        case 1: g->i++; if (g->i < 10) return g->i; g->state = 2; /* fallthrough */
        case 2: return -1; // done
    }
}
```

The compiler for `async fn`/JS generators transforms linear-looking code
into a **state machine struct** — each `await`/`yield` becomes a numbered
state, and any local *whose lifetime crosses a suspension point* needs
storage that survives the suspension (no OS-level "pause a stack frame"
without extra machinery). This is **CPS-style state-machine lowering**.

**Correction:** "gets hoisted into struct fields, heap-resident" is the
naive/unoptimized version of this transform, not a guarantee. The
required invariant is only "storage that outlives the suspension point";
*where* that storage actually lives is an optimizer decision, same as any
other value:
- Rust's `async fn` generates an anonymous, compiler-sized `Future`
  struct holding exactly the cross-await locals — but that struct itself
  is frequently stack-allocated by the caller (`Box::pin` isn't always
  required), and locals that *don't* cross an `.await` stay ordinary
  stack/register values the same as in synchronous code.
- A sufficiently smart optimizer can prove a "surviving" local is dead
  after a point and eliminate the field entirely, same as any other
  optimization.
- Some coroutine implementations use a real second stack (stackful
  coroutines, e.g. many userspace fiber/green-thread runtimes) instead of
  a struct-shaped frame at all.

The honest framing: async lowering *introduces* a real question — "what
storage does this value need once it survives a suspension?" — and a
heap/struct-resident answer is the common, safe default, not the only
answer a real compiler gives.

### Garbage collection → you implement it explicitly

- **Reference counting**: increment/decrement a count field, free at zero
  — literally CPython's `Py_INCREF`/`Py_DECREF`.
- **Mark-and-sweep**: walk a root set, mark reachable, sweep unmarked.
- **Generational**: segregate by object age (most objects die young — the
  "generational hypothesis," empirically true across most workloads).

### Dynamic arrays / growable strings → manual capacity-doubling

```c
typedef struct { char* data; size_t len, cap; } String;
void push(String* s, char c) {
    if (s->len == s->cap) {
        s->cap *= 2;               // amortized O(1) growth
        s->data = realloc(s->data, s->cap);
    }
    s->data[s->len++] = c;
}
```

What `std::vector`, Python `list`, JS arrays, Rust `Vec` all are under the
hood. The 2x growth factor keeps total realloc cost linear in total
pushes, not quadratic (amortized analysis).

---

## 2. Second tier: less obvious, still load-bearing

### Fat pointers / trait objects (Rust `dyn Trait`, Go interfaces)

```c
typedef struct {
    void* data;         // pointer to the concrete object
    const VTable* vt;   // pointer to its method table
} FatPtr;
```

Instead of embedding the vtable pointer *inside* the object, carry it
*alongside* a data pointer as a two-word pair at the call site. Advantage
over embedded vtables: any type can implement a trait after the fact,
without modifying its layout. Cost: two pointer chases and two machine
words instead of one — concretely 16 bytes vs. 8 on a typical LP64 64-bit
target, but that's an artifact of pointer width on that target, not a
language-level guarantee; it's 8 bytes vs. 4 on a 32-bit target, and the
exact layout (which word is data vs. vtable, any packing) is an ABI/
compiler detail, not part of Rust's or Go's language spec.

### Hash maps → open addressing / chaining, by hand

```c
typedef struct Entry { char* key; void* value; struct Entry* next; } Entry;
typedef struct { Entry** buckets; size_t nbuckets; } HashMap;

void put(HashMap* m, char* key, void* val) {
    size_t idx = hash(key) % m->nbuckets;
    Entry* e = malloc(sizeof(Entry));
    e->key = key; e->value = val; e->next = m->buckets[idx];
    m->buckets[idx] = e;   // chaining: prepend to bucket's linked list
}
```

**Correction:** this chained-bucket layout is the simplest illustrative
baseline, not literally what any mainstream implementation does today —
they've each diverged in their own sophisticated direction. Python's
`dict` (3.6+) is an **open-addressing, insertion-ordered** table: a
compact dense array of entries plus a separate sparse index array, so
iteration order matches insertion order for free. Rust's `std::HashMap`
(via `hashbrown`) is a **SwissTable**: open addressing with SIMD-scanned
1-byte control bytes per slot, so probing checks 16 slots per vector
instruction instead of one pointer-chase per candidate. V8 doesn't hash
plain JS objects with known property names at all in the hot path — it
uses **hidden classes** ("shapes"), turning `obj.x` into a fixed struct-
offset load once the object's shape is known (see the inline-caching
section below), falling back to a real dictionary only for objects used
as associative maps. What *is* still accurate: TCC's own identifier table
(`tccpp.c`'s `tok_alloc`) is a genuine textbook **chained hash table** —
see §6 below for the exact code — so the simple version above is a real,
current design, just not a universal one. **Symbol tables** in every
compiler frontend are some form of hash map from identifier names to
declarations/types — the single most load-bearing data structure in any
language implementation — but "hash map" doesn't imply one specific
internal layout.

### String interning → makes `==` fast for symbols

```c
char* intern(char* s) {
    char* existing = hashmap_get(intern_table, s);
    if (existing) return existing;   // same content -> same pointer
    char* copy = strdup(s);
    hashmap_put(intern_table, copy, copy);
    return copy;
}
```

Once interned, equal-content strings share a pointer, so equality becomes
pointer comparison (O(1)) instead of `strcmp` (O(n)). Why Python
identifier lookups are fast — and exactly what a compiler's symbol table
should do for identifiers.

### Bytecode interpreter dispatch → three strategies, real perf gap

```c
// 1. switch dispatch - branch-predictor-unfriendly (one entry for ALL opcodes)
for (;;) {
    switch (*pc++) {
        case OP_ADD: /* ... */ break;
        case OP_SUB: /* ... */ break;
    }
}

// 2. computed goto (GCC/Clang ext) - each opcode gets its OWN predictor entry
static void* dispatch[] = { &&op_add, &&op_sub, /* ... */ };
op_add: /* ... */ goto *dispatch[*pc++];
op_sub: /* ... */ goto *dispatch[*pc++];
```

Switch-dispatch interpreters suffer because the CPU's branch predictor
sees one `jmp` jumping to wildly different targets depending on program
data. Computed-goto ("threaded code") gives each opcode's dispatch its own
call site, letting the predictor specialize per-opcode-transition. A
documented, measurable (10–20%+) speedup — why CPython's main loop, Lua's
VM, and most serious bytecode interpreters use it where supported.

### Inline caching / JIT — how dynamic languages get fast

V8/PyPy, on first seeing `obj.foo`, does a slow generic lookup, then
**caches the result at the call site**, keyed on the object's observed
"shape" (its set of field names/types — a "hidden class"). Subsequent
calls with the same shape skip straight to a direct memory offset. This
is why "warm" JS/Python code speeds up over time, and why polymorphic call
sites (many shapes hitting one call point) are a classic performance
cliff (monomorphic → polymorphic → megamorphic degrade path).

### RAII / destructors → compiler-inserted cleanup calls

```c
int f() {
    void* a = malloc(...);
    if (!a) goto cleanup;
    void* b = malloc(...);
    if (!b) goto cleanup_a;
    // ... use a, b ...
cleanup_b: free(b);
cleanup_a: free(a);
cleanup:  return -1;
}
```

C++'s destructors / Rust's `Drop` compile to the compiler statically
inserting cleanup calls at every scope-exit point (normal return, early
return, and every point an exception could propagate through, tracked via
unwind tables). This `goto`-chain is exactly that shape, hand-rolled.

### Struct layout / alignment / padding

```c
struct Bad  { char a; int b; char c; };   // likely 12 bytes: padding
struct Good { int b; char a; char c; };   // likely 8 bytes: packed
```

**Correction:** the hardware story is more varied than "wants alignment,
period." Behavior is genuinely architecture-dependent: x86/x86_64
tolerates unaligned access for most instructions with a modest
throughput penalty (extra micro-ops, possible cache-line-split cost) and
no fault; classic 32-bit ARM and SPARC would hard-fault (`SIGBUS`) on
misaligned access to multi-byte types; AArch64 tolerates it in the common
case, with exceptions for certain atomic/exclusive instructions. Even
where hardware tolerates misalignment, C's *type system* still promises
`_Alignof`/natural alignment, and compilers exploit that promise for
things hardware-tolerance doesn't grant for free — auto-vectorized SIMD
loops, for instance, commonly assume aligned array starts and will either
peel a scalar prologue or simply refuse to vectorize otherwise. So the
real principle is: *alignment matters for a mix of hardware-capability
reasons (fault-avoidance on some ISAs, throughput on others) and
compiler-assumption reasons (what the optimizer is allowed to assume),
and the two don't always agree with each other.* Field reordering for
cache-line packing is still a real, measurable optimization in
Rust/C++/Go structs regardless of which of those reasons dominates on a
given target. Also the root of the **SoA vs AoS** decision
(Structure-of-Arrays keeps same-typed data contiguous for SIMD/cache
efficiency; Array-of-Structs keeps per-record data together for
whole-record access locality).

### Tail-call optimization → literal jump-instead-of-call

```asm
; normal call: push return address, jump, later ret pops it
call foo
; tail call: overwrite current frame, jump WITHOUT pushing a return address
jmp foo
```

A tail call ("the last thing I do is call you, then immediately return
your result") can reuse the current stack frame instead of a `call`/`ret`
pair, giving constant-stack-space execution for tail-recursive code.

**Correction:** the guarantee level varies a lot by language, and "can
recurse infinitely" overstates what most of these actually promise.
**Scheme** is the one on this list with a *language-level* mandate —
R5RS/R7RS require proper tail calls, so a conforming Scheme implementation
*must* make this work, always. **C has no such guarantee at all**: GCC/
Clang perform tail-call elimination as an optimization at `-O2`+ when
they can prove it's safe, silently, and just as silently *don't* when
they can't (e.g. differing calling conventions, address-taken locals,
certain ABI constraints) — code that depends on it for correctness (not
just speed) is relying on unspecified compiler behavior. **Rust makes no
tail-call guarantee whatsoever** — LLVM will sometimes perform the same
optimization C compilers do, under the same "silently, when provable"
conditions, but nothing in Rust's language semantics promises it, and
relying on it for stack-boundedness is a known footgun in the Rust
community specifically because it isn't guaranteed. Haskell (lazy,
graph-reduction evaluation) gets *effectively* unbounded tail recursion
for a different structural reason — space is governed by the lazy
evaluator's thunk graph, not a C-style call stack — which is its own
topic, not the same mechanism as Scheme's mandate or C/Rust's best-effort
optimization.

---

## 3. Third tier: memory-lifetime patterns (compiler/runtime-specific)

### Arena / bump allocators

```c
// A minimal but real arena (Chris Wellons' "arena.h" pattern)
typedef struct { char *beg, *end; } arena;
void *alloc(arena *a, ptrdiff_t size, ptrdiff_t align, ptrdiff_t count) {
    ptrdiff_t padding = -(uintptr_t)a->beg & (align - 1);
    ptrdiff_t available = a->end - a->beg - padding;
    if (available < 0 || count > available/size) abort();
    void *p = a->beg + padding;
    a->beg += padding + count*size;
    return p;
}
```

Instead of `malloc`/`free` per object, pre-allocate a big contiguous
block and satisfy allocations by **bumping a pointer forward** — O(1)
alloc, no per-object bookkeeping. Deallocation is bulk: reset the pointer
(or discard the whole block) once every object in it shares a lifetime —
a compiler pass, a request, a game-engine frame. This is *the* standard
technique in real compiler internals: **the Rust compiler allocates most
of its internal types (`ty::TyKind`, AST nodes) from long-lived arenas**,
and Ruby's own `prism` parser (`pm_arena_t`) uses exactly this pattern in
its C source. The tradeoff: you lose the ability to free any individual
object early — it's a phase/scope-shaped lifetime tool, not a general
allocator replacement. Directly relevant to `C_ARKlight`'s frontend/IR
construction: AST and IR nodes built during one `arklight build` pass are
a textbook arena use case (allocate freely during compilation, discard
the whole arena when the pass ends), and it turns "did I `free()`
everything" bookkeeping into a non-question rather than something you
audit by hand.

### Escape analysis — deciding stack vs. heap automatically

A modern compiler doesn't allocate every object on the heap by default.
**Escape analysis** is a static-analysis pass that determines whether a
value's lifetime provably stays within the function that created it (a
local temporary never returned, never stored into a longer-lived
structure). If it doesn't escape, the compiler can allocate it on the
*stack* instead of the heap — free, scope-bound deallocation, no GC/refcount
traffic at all. This is why Go and modern JVMs can sometimes make heap
allocation "disappear" for short-lived objects, and it's the same
underlying question an arena-allocation strategy answers manually: "does
this value's lifetime match a scope I already have, or does it need to
outlive that scope?" When it does need to outlive the scope, the value
gets *promoted* — to a longer-lived arena, or the heap — and that promotion
point is exactly where a manual C codegen backend has to make the same
decision explicitly, since C has no compiler doing escape analysis for
your generated code's semantics.

### Reference-counting cycle detection

Plain refcounting (Python's `Py_INCREF`/`Py_DECREF`, Rust's `Rc`) has one
structural hole: a **reference cycle** (A points to B, B points back to A)
never hits a zero count, so it leaks forever under pure refcounting.
Real systems solve this one of two ways: (1) a periodic **cycle-detecting
GC pass** layered on top of refcounting — CPython does exactly this, a
secondary mark-and-sweep-style collector that only runs to find and break
cycles refcounting alone can't reach; or (2) structurally prevent cycles
by using **weak references** for one direction of any back-pointer (a
child holding a `Weak<Parent>` instead of a strong `Rc<Parent>`), which is
Rust's idiomatic answer, since a weak reference doesn't keep the count
alive. Worth knowing if `C_ARKlight`'s runtime ever needs refcounted
shared state (e.g. widgets referencing a shared `ArkState*`): a hand-rolled
refcounting scheme inherits this exact gap and needs the same explicit
answer (periodic cycle sweep, or documented "never form a strong cycle").

---

## 4. GTK / GObject — the "hipster garbage" of C itself

A real GTK backend forces contact with an OOP system built by someone
else, in C, at scale — everything above, but as an interop boundary
rather than something you design from scratch.

### GObject — the vtable/inheritance patterns above, formalized

```c
struct _GObject {
    GTypeInstance g_type_instance;
    guint ref_count;
    GData* qdata;
};
```

Every `GtkWidget` is a `GObject` at its base, found via the same
"first-member-is-base" trick from §1. `G_DEFINE_TYPE` macros generate a
class struct (vtable) + instance struct pair, registered into a runtime
type registry (`GType`), so `GTK_IS_WIDGET(ptr)` does a runtime type
check (`dynamic_cast`, done by hand). Codegen implication: `GTK_WIDGET(x)`
/`GTK_CONTAINER(x)`-style casts aren't free — they check a type tag at
runtime, not compile time.

### Floating references — a real, silent ownership landmine

A newly created `GtkWidget` starts with a **floating reference** —
owned-by-nobody-yet. The first container that adds it (`gtk_container_add`)
"sinks" the float into a real reference. Generated code that creates a row
widget but never adds it to a container, or adds it twice, leaks or
double-frees *silently* — no crash at creation time. Since list-rendering
codegen is precisely "create N row widgets, add each to a `GtkListBox`,
later destroy some," this has to be correct by construction in generated
code, not something the app author debugs.

### Keyed list diffing — the real algorithm behind `Action.append`/`remove`

```c
typedef struct { char* key; GtkWidget* row; } RowEntry;
// hash map: item key -> currently-rendered row widget
```

Per-item list rendering needs real **keyed reconciliation**: given an old
rendered list and a new one (post-mutation), compute minimal add/remove/
move operations instead of rebuilding every row. `append` is trivial (one
new row at the end). `remove(index)` is the real subtlety: index-based
removal shifts every subsequent item's index, so a naive `GtkWidget*
rows[]` array only works if you also shift the array *and* haven't cached
a stale index anywhere else (e.g. in a closure bound to that row's own
delete button — a classic dangling-index bug). Real frameworks key rows by
stable identity, not position, specifically to avoid this class of bug.

### Callback closures with `user_data` — the closure pattern, concretely

```c
g_signal_connect(button, "clicked", G_CALLBACK(on_increment_click), &app_state);

void on_increment_click(GtkWidget* widget, gpointer user_data) {
    AppState* state = (AppState*)user_data;
    state->count += 1;
    update_count_label(state);   // your generated "renderBindings" equivalent
}
```

`user_data` is exactly the captured-environment struct pointer from §1's
closures, in a real API. **Where AOT-compiled C can beat the JS backend
outright**: the JS backend resolves `data-ark-on-click="action:increment"`
via a *string-keyed runtime lookup* (`actions[actionName]`), because the
DOM only offers string attributes at load time. In C, the exact action
each button fires is known at *compile time* — generated code can call
`Action_increment(&state, "count", 1)` directly. No dispatch table, no
string comparison, no runtime registry lookup at all. A genuine codegen
win for the native target, not just a different flavor of the same thing.

### Opaque pointers — versioning a shipped C runtime safely

```c
// arklight_runtime.h -- generated code sees only this
typedef struct ArkState ArkState;   // opaque -- no member access allowed
ArkState* ark_state_new(void);
void ark_state_set_int(ArkState*, const char* key, int value);
```
```c
// arklight_runtime.c -- real definition, hidden from generated code
struct ArkState { GHashTable* fields; /* ... */ };
```

Standard C library-boundary pattern (same idea as `FILE*`). Lets the
runtime's internals be fixed/optimized later without a compiler-breaking
change rippling into every previously-generated `.c` file.

### The GTK main-loop constraint — correctness, not perf

GTK is **not thread-safe** for widget mutation — every
`gtk_label_set_text`/`gtk_list_box_insert` must happen on the thread
running `gtk_main()`. Synchronous, click-driven signal handlers are
naturally fine. Any future async behavior (a fetch-equivalent, a
timer-driven update) can't mutate the widget tree from a worker thread —
it needs `g_idle_add()` to marshal the update back onto the main loop.
Worth designing into the runtime's API shape upfront rather than
retrofitting later.

---

## 5. "Service-oriented, separation-of-concerns, minimal boilerplate" — what TinyCC's own source actually does

This section is grounded directly in `tinycc`'s source (cloned mirror,
`TinyCC/tinycc`, `mob` branch) and cross-referenced against `C_ARKlight`'s
current `core/` + `backends/` layout. The interesting thing about TCC as a
case study: it is a ~34K-line C compiler with **zero runtime plugin
framework, zero DI container, zero reflection** — and yet its modules are
more cleanly decoupled than most codebases that reach for those tools. It
gets there with three plain-C mechanisms, each with a real cost/benefit
tradeoff, not a "best practice" applied uniformly.

### 5.1 The pipeline-stage split — separation of concerns by phase, not by "layer"

```
tccpp.c   (3,961 lines)  preprocessor / tokenizer
tccgen.c  (9,001 lines)  parser + target-agnostic codegen driver
tccasm.c  (1,525 lines)  inline-asm parsing
tccelf.c  (4,201 lines)  ELF section/symbol management, linking
tccrun.c  (1,580 lines)  in-memory JIT-style execution ("tcc -run")
tccdbg.c                 DWARF debug-info generation
i386-gen.c / x86_64-gen.c / arm-gen.c / arm64-gen.c / riscv64-gen.c
                          per-target instruction selection & register allocation
i386-link.c / ...-link.c per-target relocation application
i386-asm.c / ...-asm.c   per-target inline-assembler
```

Each file owns exactly one concern and is named after that concern, not
after a generic architectural layer ("service," "manager," "controller").
This is the same shape `C_ARKlight` already has —
`core/normalize.c` → `core/validate.c` → `core/ir_build.c` →
`backends/{html,css,js}/render.c` is TCC's `tccpp.c` → `tccgen.c` →
target-`gen.c` pipeline with the names changed. Worth stating explicitly
because it confirms the current layout isn't just "reasonably organized" —
it's the same proven shape a 25-year-old, still-actively-maintained C
compiler converged on independently.

### 5.2 Three real "interface" mechanisms, not one — and TCC uses each where its tradeoff is actually worth paying for

**(a) Runtime vtable (struct of function pointers).** [A: `ArkBackend`,
`include/carklight.h`] This is §1's vtable pattern applied to a *module*
boundary instead of an object. `C_ARKlight` already does this for backend
dispatch:

```c
// include/carklight.h
typedef struct ArkBackend {
    int  (*init)(struct ArkBackend* self, char** err_out);
    int  (*render)(struct ArkBackend* self, const ArkSite* site,
                    ArkBuildResult* out, char** err_out);
    int  (*postprocess)(struct ArkBackend* self, ArkBuildResult* out,
                         char** err_out);
    void (*shutdown)(struct ArkBackend* self);
} ArkBackend;
```

Cost: one indirect call per stage, no cross-module inlining. Benefit: **N
implementations coexist in the same binary, chosen at runtime** — HTML,
CSS, and JS backends all run in the same `ark_build` pass, in the same
process, over the same `ArkSite`. This is the *only* one of the three
mechanisms that gives you coexistence, which is exactly why it's the right
call here — `arklight build` genuinely needs all three backends active at
once, not a choice of one.

**(b) Link-time fixed-name contract — zero-cost, but exactly one
implementation per binary.** [A: `*-gen.c` × 5 targets, `libtcc.c:21-65`]
TCC's per-target backends do **not** use a vtable. Every target's
`-gen.c` file defines a function with the *same bare name* as every other
target's:

```
$ grep -n "^ST_FUNC void gfunc_prolog" *-gen.c
i386-gen.c:655:ST_FUNC void gfunc_prolog(Sym *func_sym)
x86_64-gen.c:920:void gfunc_prolog(Sym *func_sym)
arm-gen.c:1397:void gfunc_prolog(Sym *func_sym)
arm64-gen.c:1244:ST_FUNC void gfunc_prolog(Sym *func_sym)
riscv64-gen.c:807:ST_FUNC void gfunc_prolog(Sym *func_sym)
```

Same story for `load()`, `store()`, `gen_opi()`, `gen_opf()`,
`gfunc_call()`, `gfunc_epilog()` — every target implements the identical
name set. `tccgen.c` (the target-agnostic frontend, 9K lines) calls these
~47 times and **never `#include`s any backend file itself, never checks
which target it's talking to, never declares a function-pointer struct for
them**. Which implementation gets linked in is decided once, at compile
time, in `libtcc.c`:

```c
#ifdef TCC_TARGET_I386
#include "i386-gen.c"
#include "i386-link.c"
#include "i386-asm.c"
#elif defined(TCC_TARGET_ARM)
#include "arm-gen.c"
...
#elif defined(TCC_TARGET_X86_64)
#include "x86_64-gen.c"
...
#endif
```

The C preprocessor picks exactly one branch; the linker sees exactly one
definition of `gfunc_prolog` in the whole program. The "interface" is a
**naming convention enforced by the fact that a second definition would be
a link error**, not a struct anyone declares or implements-against. Cost:
zero — every call is direct and fully inlinable, same as if you'd written
one specialized compiler by hand. Benefit forfeited: no coexistence — you
cannot have an x86_64 and an arm64 codegen both live in one `tcc` binary
this way (and indeed, TCC doesn't: it's built once per target, producing
`i386-tcc`, `x86_64-tcc`, `arm-tcc` as separate binaries).

**[C] The design rule this exposes for `C_ARKlight`:** don't reach for a
vtable by default "for future flexibility." Ask first whether the
component genuinely needs *N implementations active at once* (→ vtable,
pay the indirect-call cost, as `ArkBackend` correctly does) or whether it
is always *exactly one implementation per build/run* (→ plain function,
fixed name, zero cost). [A] The pipeline stages upstream of the backends
— `core/normalize.c`, `core/validate.c`, `core/ir_build.c` — are each
plain functions today, not function-pointer structs. [C] Whether that's
"already the TCC-correct choice" is a judgment call this doc is making,
not something either codebase asserts about itself — but it's the right
call *if* those stages stay single-implementation-per-build, which is
worth treating as an explicit, revisitable assumption rather than a
settled fact as the codebase grows. The lesson to take from TCC either
way: don't "regularize" every module boundary to look like `ArkBackend`
for architectural symmetry — that symmetry would be purely cosmetic and
would cost real indirect-call/inlining overhead on stages that never need
runtime pluggability.

**(c) The unity-build toggle — one macro, two build shapes, same
source.** [A: `tcc.h:344-355`, `libtcc.c:21-65`] This is TCC's actual
"minimal boilerplate" trick, and it's almost invisible unless you go
looking for it. `tcc.h`:

```c
#ifndef ONE_SOURCE
# define ONE_SOURCE 0
#endif
#if ONE_SOURCE
#define ST_INLN static inline
#define ST_FUNC static
#define ST_DATA static
#else
#define ST_INLN
#define ST_FUNC
#define ST_DATA extern
#endif
```

Every "module-private" function and global in `tccpp.c`, `tccgen.c`,
`tccasm.c`, `tccelf.c`, and every `-gen.c`/`-link.c`/`-asm.c` file is
declared `ST_FUNC`/`ST_DATA` instead of bare `static`/nothing. Flip
`ONE_SOURCE`, and the exact same source tree becomes either:

- **One translation unit** (`libtcc.c` `#include`s all the `.c` files
  directly — `ST_FUNC` expands to `static`): every "module" call is a
  same-TU call the compiler can freely inline across, cross-module
  constant-propagate through, etc. — an LTO-grade optimization win without
  running an LTO pass, because there was never more than one TU to begin
  with.
- **Genuinely separate object files** (`ST_FUNC` expands to nothing,
  `ST_DATA` to `extern`): real link-time module boundaries, needed when
  building `libtcc.so` as a shared library with a real public/private ABI
  split (`PUB_FUNC` separately expands to a visibility-exported symbol for
  the handful of functions meant to cross that boundary, e.g.
  `tcc_new`/`tcc_compile`/`tcc_output_file`).

No second copy of any header, no separate "internal.h vs public.h" per
module, no build-system duplication — **one naming convention** answers
"is this symbol visible outside its file" for every symbol in the
project, and a single macro decides, at build time, whether "outside its
file" even means anything (unity build) or is a real linker boundary
(library build). `C_ARKlight`'s `core/internal.h` vs `include/carklight.h`
split is already doing the *public/private* half of this by hand (private
struct layouts in `core/internal.h`, which — per its own header comment —
"nothing outside `core/` includes"); what it doesn't yet have is TCC's
other half, the *build-shape* toggle. Concretely: `core/*.c` is currently
always compiled as separate `.o`s into `carklight_core` (needed because
`tests/test_normalize.c`, `test_validate.c`, `test_ir_build.c` etc. each
link against one stage in isolation — real separate compilation is
load-bearing for testability, not incidental). A TCC-style toggle would
let a **release** build additionally offer a unity-build path (one TU,
`static`-linkage internals, full cross-stage inlining through
normalize→validate→ir_build) while the **test** build keeps today's
per-stage `.o` separation — same source, zero duplicated declarations,
selected by one build-time flag, exactly as `ONE_SOURCE` does for TCC's
`libtcc.c` (unity) vs `tcc.c` (its CLI driver, linked against the
separately-built `libtcc.a`).

### 5.3 The context struct as dependency injection, minus the container

[A: `struct TCCState`, `tcc.h:735` onward] TCC threads a single ~280-line
`struct TCCState` (`tcc.h`) by pointer
through the whole pipeline — `tcc_new()` allocates one, `tcc_add_file()`,
`tcc_compile()`, `tcc_output_file()` all take a `TCCState*` and read/write
fields on it (include paths, defined macros, output type, per-file state).
No service locator, no dependency-injection framework — just one mutable
struct passed explicitly at every call site, so every function's real
dependencies are visible in its signature (`TCCState* s1` appears
literally everywhere) instead of being pulled from ambient global state or
a container. `C_ARKlight`'s `ArkSite`/`ArkBuildResult`, threaded the same
way through `normalize → validate → ir_build → render`, are the same
pattern already, just split across two smaller opaque structs instead of
one large one — worth keeping split rather than merging into a single
"AppState mega-struct" as more stages land, since TCC's own `TCCState`
(284 lines and still described in its own comments as something to
eventually trim) is itself a cautionary example of how far one
shared-context struct can sprawl once every subsystem starts bolting
fields onto it.

### 5.4 Summary: the actual rule, not the buzzwords

"Service-oriented, separation of concerns, minimal boilerplate" cashes out
in TCC's source as three concrete, cheap mechanisms, applied selectively:

| Mechanism | Cost | When TCC uses it | `C_ARKlight` equivalent |
|---|---|---|---|
| Vtable (struct of fn ptrs) | indirect call, no inlining | never, internally — TCC has no runtime-pluggable subsystem | `ArkBackend` (correctly — HTML/CSS/JS coexist per build) |
| Fixed-name link-time contract | zero | every target backend (`load`, `store`, `gen_opi`, ...) | pipeline stages (`ark_normalize`, `ark_validate`, `ark_ir_build`) — already correct |
| Unity-build toggle (`ONE_SOURCE`/`ST_FUNC`) | zero, build-time choice | `libtcc.c` (unity) vs. separately-linked `tcc.c` CLI + `libtcc.a` | not yet present — `core/*.c` today is always separately compiled, for testability |
| Opaque struct + private header | zero | `TCCState` fields are only ever touched via functions taking it by pointer; target-specific state lives in each `-gen.c`, invisible to `tccgen.c` | `core/internal.h` vs `include/carklight.h` — already correct |

The one real gap against TCC's own architecture is 5.2(c)/5.3's unity-build
toggle — everything else `C_ARKlight` has already independently converged
on the same answer TCC did.

---

## 6. A granular scan of TCC's actual codegen internals (not the module-boundary level — inside one module)

§5 stayed at the level of "how do files/modules talk to each other." This
section goes one level deeper: how `tccgen.c` itself represents an
expression while it's mid-compile, and what that reveals about
predictability and generated-code quality — the axis this document's
whole framing cares about most.

### 6.1 There is no AST. Expressions live on a fixed-size value stack.

[A: `SValue`, `tcc.h:482-496`; `vstack`/`vtop`, `tcc.h:1437`,
`tccgen.c:49`]

```c
typedef struct SValue {
    CType type;
    unsigned short r;   /* register + flags — WHERE this value currently lives */
    unsigned short r2;  /* second register, for 64-bit values on a 32-bit target */
    union { struct { int jtrue, jfalse; }; CValue c; };       /* forward jumps, or a constant */
    union { struct { unsigned short cmp_op, cmp_r; }; Sym *sym; };
} SValue;

static SValue _vstack[1 + VSTACK_SIZE];   /* tccgen.c:49 — fixed-size, not a growable structure */
ST_DATA SValue *vtop;                     /* current top-of-stack pointer */
```

TCC does not build a parse tree and then walk it. Parsing an expression
*directly* pushes/pops `SValue`s on this fixed array as it goes — `vtop`
is quite literally "the value the parser is currently holding," and every
grammar production (`unary()`, `expr_prod()`, `expr_sum()`, ...) is
simultaneously the parser *and* the code generator for that production.
There is no separate "build IR, then lower IR" phase for expressions at
all. **This is the mechanism behind TCC's famous compile speed**: one
pass, no tree allocation, no tree-walk — the entire "IR" for an
in-progress expression is whatever's currently sitting between `vstack`
and `vtop`, and it's gone the instant the expression is done. The
tradeoff is exactly what you'd predict: no separate IR means no place to
run a classic optimization pass (constant folding gets done ad hoc at
each operator, not by a generic pass over a tree), which is precisely why
`tcc -O2` still means something much weaker than GCC's `-O2`.

### 6.2 Register allocation is a linear scan of the live vstack — not graph coloring

[A: `get_reg`, `tccgen.c:1487-1524`; `save_reg`/`save_reg_upstack`,
`tccgen.c:1399` onward]

```c
ST_FUNC int get_reg(int rc)
{
    /* find a free register */
    for (r = 0; r < NB_REGS; r++) {
        if (reg_classes[r] & rc) {
            for (p = vstack; p <= vtop; p++)          /* scan every LIVE value */
                if ((p->r & VT_VALMASK) == r || p->r2 == r)
                    goto notfound;                     /* r is in use, try next */
            return r;                                  /* r is free */
        }
    notfound: ;
    }
    /* no register left: spill the OLDEST value on the stack first
       (VERY IMPORTANT to start from the bottom — comment in the source) */
    for (p = vstack; p <= vtop; p++) {
        r = p->r2;
        if (r < VT_CONST && (reg_classes[r] & rc)) goto save_found;
        r = p->r & VT_VALMASK;
        if (r < VT_CONST && (reg_classes[r] & rc)) {
        save_found:
            save_reg(r);   /* store it to the stack frame, mark it free */
            return r;
        }
    }
}
```

This is not a graph-coloring allocator (what GCC/LLVM use — build an
interference graph over the whole function, color it, spill the
minimum-cost node on conflict). It's a **linear scan over whatever's
currently on the expression stack**, and when registers run out it spills
the *oldest* live value — chosen deliberately from the *bottom* of the
stack, per the source's own comment, specifically to avoid spilling a
register that a subroutine like `gen_opi()` is mid-use with. This is
`O(register count × live values)` per allocation request, essentially
free compared to graph coloring, and fully predictable: the same
expression shape always spills the same way, every time, with no
whole-function analysis needed. The direct cost: no live-range
coalescing, no accounting for a register's use *later* in the function —
a value gets spilled purely because it's the oldest thing still sitting
around, not because a global analysis judged it least valuable to keep.
This single function is arguably *the* concrete reason TCC output is
"correct but not fast" while GCC's is "slow to produce but fast to run" —
a textbook compile-time-vs.-runtime-performance tradeoff made visible in
about 35 lines of C.

### 6.3 Comparisons are a deferred, unmaterialized value — `VT_CMP`

[A: `VT_CMP`, `tcc.h:1030`; `VT_JMP`, `tcc.h:1031`]

```c
#define VT_CMP  0x0033  /* the value is stored in processor flags (in vc) */
#define VT_JMP  0x0034  /* value is the consequence of jmp true (even) */
```

An `SValue` doesn't have to represent something already sitting in a
register or in memory — it can represent **"the result of a comparison
that hasn't been materialized into a 0/1 value yet, and is currently just
sitting in the CPU flags register."** Concretely: compiling `if (a < b)`
never has to emit "compare, `setl` into a byte register, test that byte,
jump" — it can go straight from `cmp a, b` to the correct conditional
jump (`jl`), because the comparison's *result* was represented all along
as "pending flags state," not as a materialized boolean. The 0/1 value
only actually gets generated if something downstream needs it as a real
value (assigned to a variable, say) rather than just branched on. This is
a genuine, deliberate instruction-count optimization achieved purely
through *what the value representation is allowed to mean* — no
optimization pass required, because the "wasteful" instructions were
never generated in the first place. Exactly the kind of thing a
systems-level read of a compiler should be looking for: the win isn't in
a pass that deletes redundant instructions, it's in a representation that
never produces them.

### 6.4 Symbol allocation: a slab/freelist pool, not a pure bump arena

[A: `__sym_malloc`/`sym_malloc`/`sym_free`, `tccgen.c:623-664`;
`SYM_POOL_NB`, `tcc.h:1427`]

```c
#define SYM_POOL_NB (8192 / sizeof(Sym))

static Sym *__sym_malloc(void) {           /* called only when the freelist is empty */
    Sym *sym_pool = tcc_malloc(SYM_POOL_NB * sizeof(Sym));   /* one 8KB batch */
    /* ...link all SYM_POOL_NB entries onto sym_free_first as a singly linked list... */
}

static inline Sym *sym_malloc(void) {
    Sym *sym = sym_free_first;
    if (!sym) sym = __sym_malloc();        /* only touches real malloc on freelist exhaustion */
    sym_free_first = sym->next;
    return sym;
}

ST_INLN void sym_free(Sym *sym) {
    sym->next = sym_free_first;            /* push back onto the SAME freelist */
    sym_free_first = sym;
}
```

Worth distinguishing precisely from §3's arena/bump-allocator pattern
elsewhere in this doc, because it's a genuinely different design with a
different tradeoff: this is a **slab allocator** (fixed-size-object pool,
`malloc`'d in 8KB batches, individual entries returned to a freelist on
`sym_free`) rather than a pure bump arena (§3's `arena.h` pattern, which
has *no* individual free — only bulk discard). TCC needs individual
`Sym` reuse because symbols genuinely go in and out of scope throughout a
single compilation (a local variable's `Sym` becomes reusable the moment
its block exits, long before the whole compilation ends) — a pure bump
arena would be wrong here, since "discard everything at once" doesn't
match a symbol's actual lifetime. The shared goal with a bump arena is
the same one (avoid `malloc`/`free` traffic per object, this time by
avoiding it for all but 1-in-`SYM_POOL_NB` allocations), but the
mechanism has to differ because the *lifetime shape* differs: an AST/IR
arena discarded once per compiler pass fits per-pass bulk lifetime, while
a symbol table with block-scoped entries needs true individual reuse.

### 6.5 What generalizes to `C_ARKlight`, and what's target-specific to a compiler

[C] §6.1 and §6.2 are specific to TCC's *goal* (fastest possible single-
pass compilation, weak optimization, and that's an intentional,
celebrated tradeoff, not an oversight) — they don't straightforwardly
transfer to `C_ARKlight`, whose IR (`ArkIRNode`, per `core/ir_build.c` and
`core/internal.h`) is a real persistent tree rather than a stack-machine
fiction, because `C_ARKlight`'s job (normalize → validate → multi-backend
render) genuinely needs a structure that survives past a single
expression's lifetime — unlike TCC's `vstack`, which is *deliberately*
throwaway. What *does* transfer directly:

- **§6.3's "defer materialization" idea** generalizes past comparisons:
  anywhere `C_ARKlight`'s IR→backend lowering is tempted to eagerly
  compute a final string/byte representation of something (e.g. an
  attribute value that might get further transformed before final
  render), consider whether it can stay a "pending" representation
  (closer to TCC's `VT_CMP`) until the point it's actually consumed,
  rather than materializing early and possibly redoing work.
- **§6.4's slab-vs-arena distinction** is a direct, concrete design
  question for `core/alloc.c`'s `ArkBuf`/allocator strategy going
  forward: `ArkNode`/`ArkIRNode` trees, built once per `ark_build` call
  and walked read-only by every backend, are TCC-arena-shaped (bulk
  lifetime, §3's pattern is the right fit, matching `core/alloc.c`'s
  current `ArkBuf` design). But if a future stage introduces anything
  with sub-tree lifetime — nodes created and discarded *during* one
  `ark_build` pass rather than all at once (e.g. incremental
  re-normalization on a watch-mode rebuild) — that's TCC's `Sym`-pool
  shape, not the bulk-arena shape, and reaching for the wrong one of the
  two is exactly the kind of mismatch §6.4 flags in TCC's own symbol
  table.

---

## 7. Second pass: line-by-line rereading turns up six more things

§6 stayed inside `tccgen.c`. This pass went line-by-line through files only
skimmed at the function-signature level before — `libtcc.c`'s generic
utilities, `tccpp.c`'s macro engine, `tccrun.c`'s in-memory execution path,
`x86_64-gen.c`'s ABI classifier — specifically looking for things not yet
in this document. All six are [A] direct reads.

### 7.1 `dynarray_add` has no `cap` field — capacity is implicit in a power-of-two check

[A: `dynarray_add`, `libtcc.c:516-534`]

```c
nb = *nb_ptr;
if ((nb & (nb - 1)) == 0) {           /* nb is 0 or a power of two */
    nb_alloc = nb ? nb * 2 : 1;
    pp = tcc_realloc(pp, nb_alloc * sizeof(void *));
}
pp[nb++] = data;
```

`nb & (nb-1)` is the classic power-of-two test (zero only when `nb` is a
power of two or zero). Reallocation fires exactly when `nb` *is* a power
of two — precisely the moment the previous doubling's capacity is about
to be exceeded. The usual growable-array shape is three fields (`data,
len, cap`); TCC's `dynarray` is two (`void** pp`, `int nb`) — capacity is
*recomputed from `nb` alone*, every call, instead of stored and kept in
sync. One field fewer to get out of sync, paid for with one cheap bit-test
per insert — a genuinely minimal-boilerplate tradeoff, not just small
code for its own sake. This is the single generic-array utility every
`dynarray_add(&s1->include_paths, ...)`-style call in the codebase reuses,
so the trick pays for itself across dozens of call sites, not once.

### 7.2 Macro recursion is prevented by a hide-set, not a depth counter

[A: `macro_subst`, `tccpp.c:3415-3466`]

```c
if (sym_find2(*nested_list, t)) {
    /* and mark so it doesn't get subst'd again */
    t |= SYM_FIELD;
    goto no_subst;
}
```

`nested_list` is the chain of macro names currently being expanded on the
C call stack — `macro_subst` calls itself recursively per nested macro
use, and `nested_list` grows with each level. Hitting a name already in
that chain doesn't error or infinite-loop: it tags *that specific token
occurrence* — by repurposing the `SYM_FIELD` bit, which means something
unrelated (struct/union field symbol space) in symbol-table contexts, a
deliberate bit-namespace reuse — as "already tried in this chain," and
emits it as a literal token instead of substituting it again. This is the
textbook "blue paint" algorithm (Prosser's C99 macro-expansion algorithm):
it's what makes `#define A B` / `#define B A` terminate correctly per the
standard's self-referential-macro rule, and critically it's a
per-expansion-chain set threaded through the recursion, not a global flag
— the same macro name can still be legitimately re-expanded in a sibling
expansion that isn't nested inside the first one.

### 7.3 `tcc -run`'s W^X-safe JIT execution: two virtual mappings, one physical page

[A: `rt_mem`, `tccrun.c:113-131`, `CONFIG_SELINUX` path]

```c
int fd = mkstemp(tmpfname); unlink(tmpfname);   /* anonymous shared backing */
ftruncate(fd, size);
ptr = mmap(NULL, size*2, PROT_READ|PROT_EXEC, MAP_SHARED, fd, 0);
prw = mmap((char*)ptr+size, size, PROT_READ|PROT_WRITE, MAP_SHARED|MAP_FIXED, fd, 0);
```

`mkstemp`+`unlink` creates a file descriptor for shared memory with no
surviving filesystem name (an anonymous shared mapping's backing store).
That single fd gets `mmap`'d **twice**, at two different virtual
addresses, backed by the same physical pages: once execute-only
(`PROT_READ|PROT_EXEC`, at `ptr`) and once write-only
(`PROT_READ|PROT_WRITE`, at `ptr+size`, `MAP_FIXED`). Generated machine
code is written through the RW address; the CPU executes it through the
separate RX address — no page is ever simultaneously writable and
executable, satisfying a strict W^X (write-XOR-execute) hardening policy
that would otherwise flatly forbid `tcc -run` from working at all. This
is the same double-mapping trick production JIT engines use under
SELinux/hardened-runtime constraints, appearing here in about ten lines
because a "-run" C interpreter has the identical write-then-execute
requirement any JIT does.

### 7.4 Explicit instruction-cache flush — present on ARM/RISC-V, absent on x86

[A: `protect_pages`, `tccrun.c:487-490`]

```c
# if (defined TCC_TARGET_ARM && !TARGETOS_BSD) || defined TCC_TARGET_ARM64 || defined TCC_TARGET_RISCV64
    if (mode == 0 || mode == 3) {
        void __clear_cache(void *beginning, void *end);
        __clear_cache(ptr, (char *)ptr + length);
    }
# endif
```

x86/x86_64 has hardware-coherent instruction and data caches: newly
written bytes are architecturally guaranteed visible to the fetch unit
without any explicit action, so self-modifying code "just works." ARM,
ARM64, and RISC-V make no such guarantee — after `mprotect`-ing
freshly-written bytes executable, TCC has to explicitly call
`__clear_cache` to synchronize the I-cache with what was just written
through the data path, or the core may fetch stale, pre-write
instructions. The `#ifdef` boundary here isn't stylistic — it *is* each
ISA's cache-coherence model, made visible as a one-line conditional in a
compiler's own runtime-execution code.

### 7.5 64-byte code/data alignment — a false-sharing workaround at cache-line granularity

[A: `tccrun.c:388-396`]

```c
#if defined TCC_TARGET_I386 || defined TCC_TARGET_X86_64
    /* To avoid that x86 processors would reload cached instructions
       each time when data is written in the near, we need to make
       sure that code and data do not share the same 64 byte unit */
    if (align < 64) align = 64;
#endif
```

The sharpest find of this pass. Some x86 microarchitectures snoop writes
against cache lines that also hold decoded/cached instructions (an
internal self-modifying-code detector) and will flush the relevant part
of the pipeline if a write lands in the *same 64-byte line* as code —
even when the write itself never touches an instruction byte, purely
because it shares the cache line with one. TCC forces 64-byte alignment
between its generated code and data sections specifically to keep them
off the same cache line and avoid that false-positive SMC penalty. This
is a microarchitectural detail living inside a compiler's own
memory-layout logic, not a textbook abstraction — the same
false-sharing concern that shows up in concurrent data-structure design
(padding hot fields to a cache line to stop unrelated writes from
invalidating each other's cached copies), here applied to code vs. data
instead of two threads' data.

### 7.6 The SysV x86_64 ABI's eightbyte classification, hand-implemented

[A: `classify_x86_64_arg`, `x86_64-gen.c:1109-1170`]

```c
if (size > 16) {
    mode = x86_64_mode_memory;                 /* too big for registers: passed on the stack */
} else {
    mode = classify_x86_64_inner(ty);          /* INTEGER / SSE / X87, per 8-byte chunk */
    switch (mode) {
    case x86_64_mode_integer:
        *reg_count = (size > 8) ? 2 : 1;        /* up to two integer registers */
        ...
    case x86_64_mode_sse:
        *reg_count = (size > 8) ? 2 : 1;        /* up to two SSE (xmm) registers */
        ...
    }
}
```

Confirms TCC directly hand-implements the System V AMD64 ABI's struct-
passing rule straight from the spec text: a struct larger than 16 bytes
is always passed in memory (on the stack); one 16 bytes or smaller gets
split into up to two 8-byte ("eightbyte") chunks, each independently
classified as INTEGER (goes in a general-purpose register) or SSE (goes
in an `xmm` register) based on its field types, and passed in up to two
registers accordingly. There's no shared ABI library backing this — per
§5.2(b), every target backend (`arm-gen.c`, `arm64-gen.c`, `riscv64-gen.c`,
...) re-derives its own calling convention's classification rules
independently, by hand, in that target's own file.

### 7.7 What's new for `C_ARKlight` here

[C] Most of §7 is compiler/runtime-execution-specific and doesn't
transfer directly — `C_ARKlight` emits and later compiles C source with a
separate, standard toolchain, so it never needs `tcc -run`'s W^X/JIT
machinery (§7.3-7.5) itself. What *does* generalize: §7.1's "recompute
capacity instead of storing it" instinct is worth applying to
`core/alloc.c`'s `ArkBuf` if it isn't already doing the equivalent — one
fewer field to keep consistent is one fewer bug class, for free, on a
buffer that's already the shared allocation primitive used across
multiple backends. §7.2's hide-set pattern is the more interesting
transfer: any future `C_ARKlight` stage that does its own textual/
template substitution (e.g. a component-to-C-source template expansion
step, if one lands per the fragment-file architecture in §5) faces the
exact same self-reference hazard C macros do, and TCC's answer —a
per-expansion-chain visited-set, not a global recursion-depth cutoff — is
the correct shape to copy, not a novel problem to re-solve from scratch.

---

## 8. Third pass: reading by "what service does this cluster of files perform" — four more findings

TCC's tree is physically flat (no subdirectories), but the files group
into real service clusters by what they do, not by where they sit:
frontend (`tccpp.c`, `tccgen.c`), per-target codegen (`*-gen.c`),
per-target linking (`*-link.c`), object-format emission
(`tccelf.c`/`tccpe.c`/`tcccoff.c`/`tccmacho.c`), and a CLI/tooling layer
(`tcc.c`, `tcctools.c`). This pass went through the two clusters not yet
touched — object-format emission and the CLI/tooling layer — plus one
cross-cutting check (does the inline-assembler reuse the codegen's
existing machinery, or does it duplicate it). All four are [A] direct
reads.

### 8.1 One binary, three services, dispatched by flag — a different tradeoff from §5.2(b)'s per-target build

[A: `tcc.c:295-325`; `tcc_tool_ar`/`tcc_tool_impdef`, `tcctools.c:57,363`]

```c
if (opt == OPT_AR)
    ret = tcc_tool_ar(argc, argv);       /* tcc doubles as `ar` */
#ifdef TCC_TARGET_PE
if (opt == OPT_IMPDEF)
    ret = tcc_tool_impdef(argc, argv);   /* tcc doubles as a Windows .def generator */
#endif
```

The same `tcc` binary that compiles C also implements a working subset of
`ar` (`tcc_tool_ar`, `tcctools.c` — described in its own header comment as
"for making libtcc1.a without ar") and, on Windows, an import-library
`.def` generator (`tcc_tool_impdef`). Dispatch is a CLI flag parsed
through the exact same `tcc_parse_args`/`TCCState` entry point as normal
compilation — not a separate binary, not a separate `main()`.

This is worth contrasting directly with §5.2(b)'s per-target backend
selection, because it's the *opposite* tradeoff for a superficially
similar-looking problem ("TCC needs to do several different jobs"): the
target backends are link-time-selected into *separate* binaries
(`i386-tcc`, `arm-tcc`, ...) because they never need to coexist and
zero-cost direct calls matter more. The `ar`/`impdef` tools are bundled
into the *same* binary, dispatched at runtime by a parsed flag, because
here the actual goal is different: avoid an external `ar` dependency
when TCC needs to build its own runtime library (`libtcc1.a`) as part of
its own build process — coupling them into one binary is a distribution/
bootstrapping decision, not a performance one. Same author, same
codebase, two different multi-implementation problems, two different
correct answers — reinforcing §5.2's actual rule (ask what the tradeoff
is *for*, don't default to one pattern) rather than contradicting it.

### 8.2 Two hash functions, two different constraint sources

[A: `elf_hash`, `tccelf.c:390-399`; `tok_alloc`, `tccpp.c:498` — see §6]

```c
static ElfW(Word) elf_hash(const unsigned char *name)
{
    ElfW(Word) h = 0, g;
    while (*name) {
        h = (h << 4) + *name++;
        g = h & 0xf0000000;
        if (g) h ^= g >> 24;
        h &= ~g;
    }
    return h;
}
```

This is the PJW/ELF hash — not TCC's own design, but the exact function
mandated by the System V ELF ABI's gABI spec for the `.hash` section.
TCC has no freedom to pick a different, better hash here: a dynamic
linker (`ld.so`) doing runtime symbol lookup against a binary TCC
produced has to compute the *identical* hash to find the right bucket,
so this function's exact bit-shuffle is an ABI/interoperability contract
with the rest of the OS, not a design choice. Contrast directly with
§6.1/§7.2's `tok_alloc` identifier hash (`tccpp.c`) covered earlier in
this document — that one is entirely TCC's own internal invention, free
to change between TCC versions with zero external compatibility
consequence, because nothing outside TCC's own process ever looks at it.
Same category of code (string → hash table), two completely different
constraint sources — one externally mandated by a published binary format
spec, one purely an internal implementation choice — and the difference
is invisible unless you go looking for *why* each one is shaped the way
it is.

### 8.3 The PLT/GOT lazy-binding trampoline, hand-assembled as literal opcode bytes

[A: `create_plt_entry`, `x86_64-link.c:114-150`; `build_got_entries`,
`tccelf.c:1417`]

```c
/* empty PLT: create PLT0 entry that pushes the library identifier
   (GOT + PTR_SIZE) and jumps to ld.so resolution routine
   (GOT + 2 * PTR_SIZE) */
if (plt->data_offset == 0) {
    p = section_ptr_add(plt, 16);
    p[0] = 0xff; p[1] = modrm + 0x10; write32le(p+2, PTR_SIZE);   /* pushl got+PTR_SIZE */
    p[6] = 0xff; p[7] = modrm;        write32le(p+8, PTR_SIZE*2);/* jmp *(got+2*PTR_SIZE) */
}
...
p[0] = 0xff; p[1] = modrm;  write32le(p+2, got_offset);  /* jmp *(got+x) */
p[6] = 0x68; write32le(p+7, relofs / sizeof(ElfW_Rel) - 1);  /* push $reloc_index */
p[11] = 0xe9;                                              /* jmp plt_start */
```

TCC hand-writes the literal x86-64 machine-code bytes for the ELF **lazy
PLT-binding trampoline** — the mechanism every dynamically linked ELF
binary uses to defer resolving a shared-library function's real address
until its first call. `PLT0` is a fixed stub (pushes the module ID, jumps
into `ld.so`'s resolver); every subsequent function gets its own 16-byte
`PLTn` stub that jumps through its GOT slot (which `ld.so` initially
points *back into the PLT itself*), and — on that first, unresolved call
— falls through to push its own relocation-table index and jump to
`PLT0`, which invokes the dynamic linker to resolve the real address and
patch the GOT slot in place, so every call after the first jumps straight
there with no resolver overhead. `build_got_entries` (`tccelf.c:1417`)
notes it deliberately runs in **two passes**, because "some targets (arm,
arm64) do not allow mixing `R_JMP_SLOT` and `R_GLOB_DAT`" relocation
types in the same table region — another externally mandated constraint
(like §8.2's `elf_hash`), this time coming from the ARM/ARM64 ELF psABI
rather than the generic ELF gABI. This is about as concrete as "hardware
control" gets inside a compiler: the actual bytes the CPU executes on a
symbol's first call, assembled one opcode at a time by hand, to implement
a dynamic-linking mechanism most C programmers only ever encounter as an
invisible line item in `ldd` output.

### 8.4 Inline assembly doesn't get its own register allocator — it reuses `gv()`

[A: `tccasm.c:1217,1311`]

```c
SValue sv;
...
gv(RC_INT);
```

TCC's inline-`asm` operand handling (`tccasm.c`) binds `asm` operands
through the exact same `SValue`/`gv()` machinery covered in §6.1-6.2 —
the same value-stack representation and the same linear-scan/spill
register allocator ordinary C expressions use, not a separate allocator
built specifically for inline-asm constraint solving. A less disciplined
codebase would be tempted to special-case inline asm with its own
register-tracking logic (it has real extra structure to handle — named
operand constraints, clobber lists — that ordinary expressions don't);
TCC instead routes the "get this value into a register" step through the
one function that already knows how, and layers only the
constraint-matching logic on top. One more instance of §5's actual rule
in practice: don't build a second version of a mechanism you already have
just because the calling context looks different.

### 8.5 What's new for `C_ARKlight` here

[C] §8.1 is the most directly transferable finding of this pass, and it
cuts against reflexively treating "coexistence needs a vtable" (§5.2's
main rule) as the only axis: if `C_ARKlight` ever wants a bundled
utility — e.g. a schema-validator or fragment-linter invoked as
`arklight lint` alongside `arklight build` — the TCC precedent is to
dispatch it as a flag-selected mode through the *same* CLI entry point
and `ArkSite`-equivalent state, not spin up a second binary, provided the
reason is "avoid a second distributed artifact" rather than "these need
to run simultaneously" (that second case is still `ArkBackend`'s job).
§8.3/§8.4 don't transfer directly — `C_ARKlight` never emits its own
relocations or does its own register allocation — but §8.4's underlying
instinct does: `backends/js/render.c` and `backends/html/render.c`
already share `ArkBuf` (per §5.1's file-header comments) rather than each
maintaining a private buffer type; the same discipline — route a new
requirement through an existing shared mechanism before building a
parallel one — is the actual, general lesson, independent of which
specific mechanism (register allocator, string buffer, whatever) it gets
applied to next.

---

## 9. Memory-safety failure modes: what every C_ARKlight-emitted `.c` file must avoid

Every mechanism in §§1-8 — vtables, closures, tagged unions, arenas,
refcounting — is bookkeeping a high-level language's compiler or runtime
does *for* you. The flip side, and the reason this document can't stop at
"here's how the feature lowers": once `C_ARKlight` is the thing emitting
raw C on someone else's behalf, it inherits every classic C memory bug as
a **codegen correctness obligation**, not just a hand-written-code risk.
A human forgetting a bounds check is a bug in one file; a codegen
template with the same gap is a bug replicated into every fragment it
ever emits. The five failure modes below are the canonical set — worth
being concrete about each, because "be careful with memory" is not
actionable and "never emit an unchecked `strcpy`" is.

### 9.1 Missing bounds checks → buffer overflow

```c
char buf[512];
memcpy(buf, user_data, user_data_len);   /* no check that user_data_len <= 512 */
```

C arrays don't carry their own length at runtime and indexing past the
end is not a checked operation — it's **undefined behavior**, which in
practice means "whatever byte pattern happens to sit past the buffer gets
overwritten," including saved return addresses on the stack. This is the
class of bug behind the 1988 Morris worm's spread through Unix network
daemons, and it's fixed by the boring, unglamorous fix: check the length
against the destination's capacity before the copy, every time, with no
exceptions carved out for "this input is trusted."

```c
if (user_data_len > sizeof(buf)) { /* reject or truncate */ }
memcpy(buf, user_data, user_data_len);
```

### 9.2 Trusting a length field instead of the buffer that backs it

```c
/* attacker-controlled: claims a 64000-byte payload, buffer is actually 3 bytes */
memcpy(response, request->payload, request->claimed_length);
```

A specific, more insidious variant of §9.1: the *length* comes from the
same untrusted input as the *data*, so validating "is this a well-formed
message" isn't enough — the claimed length has to be checked against the
size of the buffer that actually backs the payload, independently. This
is the shape of the 2014 OpenSSL Heartbleed bug: a length field taken at
face value let a small request read arbitrary adjacent memory back to the
caller. The general rule for any `C_ARKlight`-emitted parser/serializer:
never let one field from an untrusted source dictate how many bytes get
read from or written into a buffer sized by a *different* source.

### 9.3 Use-after-free

```c
Node *n = malloc(sizeof(Node));
...
free(n);
...
n->next = other;   /* n's memory may be reused by something else by now */
```

Freeing memory doesn't erase the pointer that pointed to it — it only
tells the allocator the memory is available for reuse. Any code path that
still holds and dereferences that stale pointer is reading or writing
through memory that may now belong to a completely unrelated object; this
was the shape of a well-known 2013 Internet Explorer vulnerability, where
a freed DOM object was still reachable and dereferenceable from elsewhere
in the engine's internal state. The structural fix, not just a habit: set
freed pointers to `NULL` immediately after `free()`, and — more
robustly — never let more than one owner hold a raw pointer to the same
heap object across a `free()` boundary in the first place (this is
exactly what §3's arena/refcounting discussion is *for*).

### 9.4 Double free

```c
free(p);
...
free(p);   /* undefined behavior: corrupts the allocator's internal bookkeeping */
```

Freeing the same pointer twice doesn't just risk a crash — it corrupts
the heap allocator's own internal free-list metadata, which downstream
can be steered into writing attacker-chosen values to attacker-chosen
addresses. It's a narrower case of §9.3's underlying problem (an object
with more than one path to its own destruction), and the same fix
applies: a single, unambiguous owner responsible for the one `free()`
call, enforced by construction rather than by convention.

### 9.5 Off-by-one errors, especially around string termination

```c
char name[16];
strncpy(name, input, 16);   /* if input is exactly 16 bytes, no NUL is ever written */
strlen(name);               /* reads past the buffer looking for a terminator that isn't there */
```

C strings are NUL-terminated by convention, not by the type system — a
16-byte buffer holding a 16-byte string has room for the string but not
the terminator, and every function that expects `char*` to mean
"NUL-terminated" will walk straight off the end looking for one. This is
the single most common source of a one-byte-short buffer overflow, and
it compounds with §9.1: the fix isn't "use a bigger buffer," it's
budgeting the terminator as part of the required capacity everywhere a
string's length is computed, including in generated code that stitches
fragments together (exactly the kind of string assembly `backends/html`
and `backends/js` already do — see §5.1).

### 9.6 What this means for `C_ARKlight`'s codegen, specifically

[C] None of §§9.1-9.5 are new information — they're the standard "C is
sharp" list. What's specific to `C_ARKlight` is that a codegen backend
turns each of these from a per-instance human mistake into a
*template-level* one: get a fragment-emission helper wrong once in
`backends/html` or `backends/js`, and the bug isn't in one `.c` file, it's
in every site `C_ARKlight` ever builds with that helper. That argues for
a narrower, more mechanical policy than "write careful C":

- Every buffer the codegen emits should carry its capacity alongside the
  pointer at the call site (the `ArkBuf`-style pattern §8.5 already notes
  is shared between the HTML/JS backends), so §9.1/§9.2-style checks are
  a property of the shared buffer helper, not something each fragment
  template has to remember to do itself.
- Ownership of any heap-allocated IR/AST node built during a
  `arklight build` pass should default to the arena pattern from §3
  rather than manual `malloc`/`free` pairing — it doesn't just simplify
  cleanup, it structurally removes §9.3/§9.4 as a *possible* bug class
  for anything allocated through it, rather than relying on discipline.
- Any place `C_ARKlight` generates code that assembles or copies strings
  (fragment concatenation in `backends/html`/`backends/js`, per §5.1)
  should compute required capacity as `content_length + 1` as a fixed
  rule, not as a case-by-case judgment call, closing off §9.5 the same
  structural way.

The common thread across all three: each of §§9.1-9.5 is avoidable by an
individual careful C programmer, but "be careful" doesn't scale to a code
generator producing many fragments from many templates. The actual
mitigation is architectural — push the check into the one shared helper
every emitted fragment routes through — which is the same lesson §8.5
already drew from TCC's own internals, just applied to memory safety
specifically instead of code-reuse generally.

---

## 10. Net takeaway

Every high-level abstraction is a compiler doing bookkeeping you'd
otherwise do by hand in C — vtables for dispatch, hoisted locals for
closures/coroutines, monomorphized copies for generics, refcount/GC calls
inserted automatically, arena lifetimes tracked instead of leaked. None of
it is magic at the machine level; it's mechanical. C is "the abstraction
level right before the compiler starts doing that bookkeeping for you."

**For `C_ARKlight` specifically**, the registry-driven architecture already
built for the JS backend (`BEHAVIOR_REGISTRY`/`ACTION_REGISTRY` — closed
vocabulary, per-entry fragment files) generalizes cleanly: the *data*
stays identical, only the fragment language changes from JS strings to C
source templates, and compile-time action dispatch is a strict win over
the JS backend's runtime string lookup. The two genuinely new, non-
mechanical unknowns — worth their own design pass before writing the
backend — are **keyed-list reconciliation** (a real algorithms problem)
and **getting GObject floating-reference ownership right in generated
code** (a correctness landmine specific to this particular object system).
