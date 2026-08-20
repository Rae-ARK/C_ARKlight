# carklight Total Quality Management (TQM)

## 1. Purpose

This document defines the quality system for `carklight`.

The goal is not merely to make the project compile, pass tests, or look clean in review. The goal is to make **correctness, predictability, maintainability, portability, and observable quality properties emerge from every layer of the system**, down to individual statements and lines of C.

`carklight` is a small C compiler/backend library with a stable ABI, explicit ownership, deterministic output, no external runtime dependencies, and language bindings crossing a `.arklight` file boundary. Those properties make local implementation choices architectural decisions.

This document therefore treats quality as a property of:

- individual expressions and statements;
- functions and ownership boundaries;
- data structures and invariants;
- modules and backend interfaces;
- tests and fixtures;
- the build and packaging system;
- the public ABI;
- documentation;
- release and synchronization with ARKlight.

The repository already establishes staged implementation, sanitizer support, CTest, modular backends, and fixture-based parity testing. This TQM system makes those practices explicit and extends them to line-level engineering discipline. The current repository describes itself as a work in progress and not yet ready for release, so these rules are intended to govern development before release rather than decorate a finished product. 

---

## 2. Quality Definition

For carklight, **quality means conformance to explicit requirements plus fitness for use**.

A change is high quality when it is:

1. **Correct** - it produces the specified result and preserves invariants.
2. **Safe** - ownership, bounds, lifetime, error, and resource behavior are explicit.
3. **Deterministic** - identical valid inputs produce identical observable outputs wherever determinism is part of the contract.
4. **Testable** - important behavior can be exercised and its failure modes observed.
5. **Local** - a change affects the smallest sensible subsystem.
6. **Understandable** - another maintainer can recover the reasoning without reverse-engineering archaeology.
7. **Compatible** - public ABI/API and file-format expectations are not silently broken.
8. **Measurable** - important quality claims have a check, test, build condition, or other observable evidence.
9. **Maintainable** - the implementation does not create unnecessary future coupling.
10. **Traceable** - behavior can be related to a documented requirement, fixture, invariant, or architectural decision.

Quality is therefore not synonymous with:

- "the compiler accepted it";
- "the test suite happened to pass";
- "the code is short";
- "the code is clever";
- "the code resembles another language";
- "the code is fast on one machine";
- "the reviewer did not notice a problem."

---

## 3. Quality Policy

### 3.1 Core policy

Every change MUST preserve or improve the following properties unless the change explicitly changes a documented requirement:

- memory safety;
- ownership clarity;
- API/ABI correctness;
- deterministic behavior;
- error propagation;
- testability;
- modular boundaries;
- documented synchronization assumptions;
- build reproducibility;
- portability within the supported C environment.

### 3.2 Prevention over detection

Defects should be prevented at the lowest layer that can reasonably prevent them.

Examples:

- Prefer a type or invariant that makes an invalid state unrepresentable over a later validation branch.
- Prefer ownership rules that make double-free impossible over relying on reviewers to notice ownership ambiguity.
- Prefer a bounded helper with an explicit capacity over an unbounded string operation followed by a test.
- Prefer a compile-time interface boundary over convention.
- Prefer deterministic data structures and serialization rules over normalizing output after generation.

The hierarchy is:

```text
Prevent invalid state
        ↓
Detect invalid state at the boundary
        ↓
Propagate the error explicitly
        ↓
Test the rejection path
        ↓
Diagnose the failure
```

A test is not permission to introduce a defect and hope the test catches it.

---

## 4. Customer Focus

For this project, "customer" includes:

- applications linking against `libcarklight`;
- Python, JavaScript, and other FFI bindings;
- developers consuming `carklight.h`;
- maintainers implementing backends;
- users relying on generated HTML/CSS/JS;
- tooling consuming `.arklight` and `.ark` artifacts;
- future maintainers who must understand the code.

Customer requirements therefore include:

- stable and explicit public interfaces;
- predictable ownership;
- useful error messages;
- deterministic output;
- reproducible builds;
- no unexplained global state;
- no accidental dependency requirements;
- compatibility with the documented ARKlight baseline;
- clear failure behavior.

A local optimization that makes a public interface less predictable is not a quality improvement merely because a benchmark got faster.

---

# 5. TQM Principles Applied at Line Level

The following rules apply even when the change is only one line.

## 5.1 Every line has a reason

A non-trivial line MUST have an identifiable purpose:

- establish an invariant;
- transform data;
- validate an input;
- propagate an error;
- manage ownership;
- produce deterministic output;
- enforce an interface;
- improve a measured property;
- support diagnostics;
- satisfy a documented compatibility requirement.

If a line cannot be explained in terms of the system's behavior, question whether it belongs.

Dead code, speculative code, compatibility code without a compatibility requirement, and "just in case" branches increase the quality surface.

## 5.2 Expressions must make invalid behavior difficult

Prefer code whose structure exposes the invariant.

Good:

```c
if (count > capacity) {
    return ARK_ERR_CAPACITY;
}
```

Less desirable:

```c
/* We assume callers never exceed capacity. */
memcpy(dst, src, count);
```

The first establishes an executable boundary. The second turns an assumption into a memory-safety gamble.

## 5.3 No hidden ownership transfer

Every allocation and ownership transfer MUST be locally understandable.

For every pointer, maintainers should be able to answer:

- Who allocated it?
- Who owns it now?
- Who frees it?
- Can ownership be transferred?
- Can the pointer be borrowed?
- How long is a borrowed pointer valid?
- What happens on an error path?

If the answer requires reading five unrelated files, the ownership design is too implicit.

## 5.4 Every resource acquisition needs a failure path

For resources such as:

- heap allocations;
- files;
- buffers;
- strings;
- backend results;
- temporary structures;

the implementation MUST define what happens when acquisition fails.

Do not add success-path code first and invent failure cleanup later.

## 5.5 Error paths are first-class behavior

A fallible function is not correct merely because its happy path works.

Tests and implementation must cover:

- invalid input;
- `NULL` where prohibited;
- empty input where prohibited;
- allocation failure where practical;
- malformed structures;
- missing required properties;
- unsupported types;
- backend failures;
- output/write failures;
- cleanup after partial construction.

Errors MUST be propagated according to the project's established error convention rather than silently swallowed.

An error message should identify the failed operation and enough context to diagnose it.

---

# 6. C-Level Coding Rules

## 6.1 Warnings are defects

The project enables:

```text
-Wall -Wextra
```

Warnings MUST be treated as defects, not background noise.

A change MUST NOT intentionally introduce a new warning.

If a warning exposes a genuine ambiguity or unsafe construct, fix the underlying problem rather than silencing the warning without justification.

Compiler-specific suppression MAY be used only when:

1. the warning is demonstrably a false positive or unavoidable;
2. the suppression is as narrow as practical;
3. the reason is documented.

## 6.2 Undefined behavior is never an optimization technique

Do not rely on:

- signed integer overflow;
- out-of-bounds pointer arithmetic;
- invalid aliasing assumptions;
- reading uninitialized objects;
- invalid shifts;
- lifetime violations;
- use-after-free;
- double-free;
- null dereference;
- implementation-defined behavior when portability matters.

If performance requires a low-level technique, its legality MUST be established first.

## 6.3 Integer conversions must be intentional

When sizes, indexes, lengths, and allocation quantities cross integer types:

- choose types deliberately;
- avoid silent narrowing;
- validate external values before conversion;
- consider overflow before multiplication/addition;
- use `size_t` for object sizes and array indexing where appropriate.

A cast is not validation.

```c
size_t n = (size_t)external_value;
```

does not prove that `external_value` fits `size_t`.

## 6.4 Pointer arithmetic must have a proven bound

Before dereferencing or indexing through a pointer, the code must establish that the target lies within the object's valid range.

Prefer:

```c
if (index >= count) {
    return ARK_ERR_BOUNDS;
}
item = items[index];
```

over relying on a caller contract that is not enforced at a trust boundary.

## 6.5 String operations must be explicit about length

Prefer length-aware operations and explicit capacity tracking.

Every dynamically built string should have an invariant equivalent to:

```text
0 <= length <= capacity
```

and, for C strings:

```text
length < capacity
```

when a terminating `'\0'` is required.

Do not confuse:

- allocated bytes;
- initialized bytes;
- string length;
- remaining capacity.

They are different quantities.

## 6.6 `NULL` is a state, not an error message

If `NULL` has semantic meaning, document the meaning.

Examples:

- absent optional child;
- no error;
- borrowed pointer absent;
- uninitialized field;
- allocation failure.

Do not use `NULL` interchangeably for unrelated states.

## 6.7 Const-correctness

Use `const` whenever a function does not modify the referenced object.

Const-correctness is part of the contract. It prevents accidental mutation and communicates intent to callers and reviewers.

## 6.8 Narrow scope

Functions should have one coherent responsibility.

Avoid functions that simultaneously:

- validate;
- mutate;
- allocate;
- render;
- perform I/O;
- update global state;
- format diagnostics.

Separate stages make behavior testable and failures attributable.

---

# 7. Ownership and Memory Quality

The Stage 0 design explicitly treats ownership as a foundational decision. This TQM policy makes that decision operational.

## 7.1 Ownership table

Every heap-owned type MUST have a documented ownership model.

At minimum:

| Object | Created by | Freed by | Borrowing allowed | Transfer allowed |
|---|---|---|---|---|
| `ArkNode` | constructor / builder | matching free path | yes, where documented | only by documented API |
| `ArkSite` | site constructor / loader | `ark_free_site` | yes | documented API only |
| `ArkBuildResult` | builder/backend | `ark_free_result` | yes | documented API only |
| backend-owned buffer | backend | backend/result owner | only while owner lives | documented API only |

The exact table must be updated when ownership semantics change.

## 7.2 Constructor/free symmetry

For every owning constructor, there must be a corresponding destruction path.

A type's destructor must be safe for every state the constructor can legitimately produce, including partially initialized state when failure cleanup requires it.

## 7.3 Partial construction

If construction can fail halfway through:

```text
allocate A
allocate B
allocate C
```

then every prefix of that sequence must have a valid cleanup path.

The cleanup path must not assume that later allocations succeeded.

## 7.4 Double-free prevention

Do not solve double-free risk by making cleanup "probably happen only once."

Prefer ownership structures and state transitions that make the legal lifecycle obvious.

---

# 8. Data Structure Quality

## 8.1 Invariants belong near the type

For each important structure, document:

- valid ranges;
- ownership;
- nullability;
- count/capacity relationship;
- initialization requirements;
- mutation rules;
- lifetime.

Example:

```text
children == NULL iff child_count == 0
child_count <= child_capacity
every children[i] for 0 <= i < child_count is a valid owned/borrrowed ArkNode according to the container contract
```

If an invariant is important enough to debug, it is important enough to document.

## 8.2 Make invalid states unrepresentable where practical

Prefer:

```text
typed enum + validated payload
```

over:

```text
integer + convention + comments
```

when the type system can express the distinction without unnecessary complexity.

## 8.3 Arrays

For every dynamic array:

```text
count <= capacity
```

MUST hold after every successful mutation.

After removal or compaction:

```text
logical elements are contiguous in [0, count)
```

MUST hold unless the API explicitly documents another representation.

---

# 9. Normalization Quality

`ark_normalize` is a pure tree transformation.

Its quality contract is:

- preserve semantic content;
- prune represented absent children;
- maintain tree validity;
- preserve ownership;
- not perform unrelated schema validation;
- not render;
- not perform I/O;
- be idempotent.

Therefore:

```text
normalize(normalize(x)) == normalize(x)
```

should hold for every valid input.

A normalization change MUST include a test of this property when the affected behavior is relevant.

The repository already documents that normalization mutates and returns the same pointer and does not allocate or free in its current design. That is a quality invariant, not merely an implementation detail.

---

# 10. Validation Quality

Validation is the trust boundary between structural input and later assumptions.

A validator MUST:

- reject invalid component types;
- reject missing required properties;
- reject invalid property values;
- recurse through the complete relevant tree;
- stop or collect errors according to the documented contract;
- never silently accept malformed state;
- avoid mutating input unless explicitly specified.

A successful validator should establish a documented postcondition that later stages are allowed to rely upon.

A later stage MUST NOT duplicate validation merely because the earlier validator was unclear. Strengthen the boundary contract instead.

---

# 11. IR Quality

The IR represents **intent**, not backend-specific markup.

Therefore:

- IR code MUST NOT depend on HTML tags merely because HTML is currently a backend.
- Backend-specific behavior belongs in backend modules.
- IR construction must preserve semantic information needed by all supported backends.
- Backend limitations must not silently redefine the IR.

If an IR change is necessary because a backend cannot represent required semantics, the design decision must be documented before implementation.

---

# 12. Backend Quality

Each backend MUST obey the `ArkBackend` interface and remain replaceable.

A backend should be understandable as:

```text
validated IR
      ↓
backend
      ↓
BuildResult
```

not:

```text
backend
  ↓
secretly modifies IR
  ↓
writes unrelated global state
  ↓
depends on another backend's private internals
```

## 12.1 Backend isolation

A backend MUST NOT reach into another backend's private implementation.

A backend MUST NOT depend on `core/internal.h` when the public backend interface is the intended boundary.

## 12.2 Deterministic serialization

Where deterministic output is part of the contract:

```text
same validated IR + same backend configuration
        ==
same output bytes
```

Determinism MUST include:

- stable ordering;
- stable escaping;
- stable attribute/property emission;
- stable path generation;
- no accidental pointer-address output;
- no uninitialized bytes;
- no dependency on hash iteration order unless explicitly fixed.

---

# 13. HTML/CSS/JS Output Quality

Generated output is a customer-facing artifact.

Quality requires:

### HTML

- valid escaping;
- deterministic serialization;
- correct tag mapping;
- correct attribute generation;
- correct relative paths;
- no accidental raw injection from untrusted text.

### CSS

- deterministic rule ordering;
- valid escaping/serialization;
- stable class generation;
- no accidental dependency on backend traversal order.

### JavaScript

- deterministic generation;
- correct action/behavior vocabulary;
- no accidental executable injection through data values;
- explicit handling of unsupported behavior.

Output tests SHOULD compare generated bytes or normalized output against known-good fixtures.

Where exact byte parity is promised, tests MUST compare exact bytes.

---

# 14. Public ABI Quality

The public header is a product.

Changes to `include/carklight.h` MUST be treated as API/ABI changes, not ordinary refactoring.

Before changing a public symbol, ask:

1. Is the change required?
2. Does it break source compatibility?
3. Does it break binary compatibility?
4. Does ownership change?
5. Does error behavior change?
6. Does struct opacity change?
7. Does the public header still describe the implementation accurately?
8. Do bindings need changes?
9. Do tests need ABI coverage?

Opaque structures should remain opaque unless exposing layout is an explicit requirement.

A convenience function that weakens ABI stability is not automatically a quality improvement.

---

# 15. Error Handling Standard

Every public fallible function MUST define:

- success result;
- failure result;
- error ownership;
- error string ownership, if applicable;
- whether output objects exist on failure;
- cleanup responsibility.

For the project's `char **err_out` convention:

```text
success:
    return normal result
    err_out is NULL or points to the documented success state

failure:
    return documented failure value
    err_out identifies the failure when the API promises an error
```

The exact convention MUST remain consistent across the API.

Never return a partially valid object while reporting failure unless the API explicitly defines partial results.

---

# 16. Testing as a Quality System

Tests are evidence, not decoration.

## 16.1 Every behavior has a test boundary

A change should have the narrowest test that proves its correctness:

```text
line/invariant
    ↓
function test
    ↓
module test
    ↓
integration/parity test
```

Not every line needs a test. Every **important behavior or invariant** does.

## 16.2 Test both acceptance and rejection

For a validator:

```text
valid input  → accepted
invalid input → rejected
```

For a serializer:

```text
valid IR → expected bytes
malformed/unrepresentable state → defined failure
```

For ownership:

```text
construct → use → free
construct partially → fail → cleanup
```

## 16.3 Regression rule

Every confirmed defect MUST produce a regression test unless the defect is impossible to test meaningfully.

The test should fail for the old behavior and pass for the corrected behavior.

## 16.4 Fixture parity

Where carklight mirrors ARKlight behavior, the Python implementation and its fixtures are the reference unless the C port intentionally documents a divergence.

A parity change MUST identify:

- reference behavior;
- C behavior;
- intentional divergence, if any;
- fixture/test proving the behavior.

---

# 17. Build Quality

The normal build must remain clean.

Required baseline:

```bash
cmake -S . -B build
cmake --build build
ctest --test-dir build
```

The project already provides an optional ASan/LeakSanitizer configuration:

```bash
cmake -S . -B build-asan -DCARKLIGHT_ENABLE_ASAN=ON
cmake --build build-asan
ctest --test-dir build-asan
```

Memory-sensitive changes SHOULD run the sanitizer configuration before review.

A release candidate MUST run:

1. normal build;
2. full test suite;
3. sanitizer build/test;
4. packaging;
5. relevant parity fixtures.

---

# 18. Performance Quality

Performance matters, but optimization is subordinate to correctness and architecture.

The order is:

```text
correct algorithm
    ↓
correct data model
    ↓
correct ownership/lifetime
    ↓
correct memory locality
    ↓
measured hot path
    ↓
targeted optimization
```

Do not optimize based on intuition alone.

## 18.1 Allocation quality

Avoid unnecessary allocations in hot paths.

Before removing an allocation, establish:

- why it is hot;
- what lifetime it currently represents;
- whether ownership becomes harder to understand;
- whether the optimization changes API semantics.

## 18.2 Cache/locality quality

Prefer data layouts that match access patterns.

But do not destroy semantic clarity merely to save a cache miss that has not been measured.

## 18.3 Branch quality

Branches should reflect actual semantic cases.

Do not introduce branchless tricks merely because assembly looks interesting.

If a branch is performance-critical, benchmark the real workload and inspect generated code when justified.

## 18.4 Benchmark rule

A performance claim MUST have:

- a benchmark or measurement;
- a defined workload;
- a baseline;
- a comparison;
- enough context to reproduce the result.

"Seems faster" is not a metric.

---

# 19. Documentation Quality

Documentation is part of the implementation.

The repository's staged plan explicitly treats documentation consistency as a quality condition. Cross-references MUST remain valid, and terminology MUST not drift between documents.

When changing a design:

1. update the implementation;
2. update the relevant API comments;
3. update architecture documents;
4. update terminology if needed;
5. update tests;
6. check cross-references.

Do not allow code and documentation to describe two different systems.

---

# 20. Change Isolation

A change should be as small as possible while still being complete.

Prefer:

```text
one invariant
→ one implementation change
→ one focused test change
```

over mixing:

```text
feature
+ refactor
+ formatting
+ renamed symbols
+ unrelated optimization
+ documentation rewrite
```

Small changes make failures attributable.

A change that modifies many unrelated lines has a larger defect surface and requires stronger justification.

---

# 21. Review Standard

A reviewer should be able to answer "yes" to the following.

### Correctness

- [ ] Does the implementation satisfy the stated behavior?
- [ ] Are boundary conditions handled?
- [ ] Are invalid inputs handled?
- [ ] Are failure paths correct?

### Memory

- [ ] Is ownership explicit?
- [ ] Is every allocation paired with cleanup?
- [ ] Are partial failures safe?
- [ ] Are bounds proven?
- [ ] Is lifetime valid at every dereference?

### API

- [ ] Is the public interface unchanged unless intentionally modified?
- [ ] If changed, is ABI/API impact documented?
- [ ] Are nullability and ownership clear?
- [ ] Are error semantics stable?

### Determinism

- [ ] Is output ordering stable?
- [ ] Are all serialized values initialized?
- [ ] Is behavior independent of pointer addresses or incidental iteration order?

### Architecture

- [ ] Does the change remain inside the correct module?
- [ ] Does it preserve backend isolation?
- [ ] Does it preserve IR/backend separation?
- [ ] Does it avoid unnecessary coupling?

### Testing

- [ ] Is there a focused test?
- [ ] Are rejection cases covered?
- [ ] Is there a regression test where appropriate?
- [ ] Does CTest pass?
- [ ] Does sanitizer testing pass when relevant?

### Maintainability

- [ ] Can the next maintainer understand the invariant?
- [ ] Are comments explaining "why" rather than restating "what"?
- [ ] Is the code simpler than the problem requires, rather than merely shorter?

---

# 22. Definition of Done

A change is **Done** only when all applicable conditions hold:

- [ ] Requirement is stated.
- [ ] Correct architectural location is identified.
- [ ] Invariants are documented.
- [ ] Ownership is explicit.
- [ ] Failure behavior is defined.
- [ ] Implementation is complete.
- [ ] Tests cover normal behavior.
- [ ] Tests cover relevant failure behavior.
- [ ] Regression test exists for a fixed defect.
- [ ] `-Wall -Wextra` remains clean.
- [ ] Normal build succeeds.
- [ ] CTest succeeds.
- [ ] Sanitizer testing succeeds when memory behavior changed.
- [ ] Public API/ABI impact is reviewed.
- [ ] Documentation is synchronized.
- [ ] Cross-references remain valid.
- [ ] Generated output remains deterministic where required.
- [ ] No unrelated behavior was changed.
- [ ] Performance claims, if any, are measured.
- [ ] The change can be explained in terms of a concrete quality property.

---

# 23. Quality Gates

## Gate 0: Design

Before implementation:

```text
Requirement
    ↓
Invariant
    ↓
Data/API boundary
    ↓
Failure behavior
    ↓
Test strategy
```

If these are unclear, implementation should not begin.

## Gate 1: Local correctness

The changed module:

- compiles;
- has no new warnings;
- passes focused tests;
- preserves local invariants.

## Gate 2: System correctness

The complete test suite passes.

Relevant parity fixtures pass.

## Gate 3: Memory/resource correctness

Sanitizers pass for memory-sensitive changes.

No leaks, double-frees, use-after-free, or invalid accesses are accepted.

## Gate 4: Interface correctness

Public API/ABI behavior and documentation are checked.

## Gate 5: Release correctness

Build, test, sanitizer, packaging, and required compatibility checks pass.

---

# 24. Cost of Quality

carklight's quality cost can be understood using the classic four categories.

## Prevention

Examples:

- architecture documents;
- ownership rules;
- type-safe APIs;
- code review;
- invariant design;
- training;
- tests written before or alongside risky changes.

## Appraisal

Examples:

- compiler warnings;
- CTest;
- sanitizer runs;
- fixture comparisons;
- ABI review;
- deterministic-output checks.

## Internal Failure

Examples:

- failed builds;
- failing tests;
- memory leaks;
- incorrect IR;
- malformed generated output;
- backend regressions found before release.

## External Failure

Examples:

- broken bindings;
- ABI breakage;
- incorrect generated websites;
- corrupted `.ark` output;
- customer-visible incompatibility;
- release regressions.

The TQM objective is not to eliminate appraisal. It is to move defects **leftward** so that prevention catches them before they become expensive failures.

---

# 25. Quality Metrics

Metrics should measure engineering reality, not vanity.

Useful project metrics include:

| Metric | Meaning |
|---|---|
| Test pass rate | Current behavioral correctness |
| Regression count | Defects escaping earlier gates |
| Sanitizer failures | Memory/resource safety |
| Warning count | Compiler-detected issues |
| Fixture parity failures | Behavioral divergence |
| ABI changes | Public compatibility risk |
| External dependencies | Supply-chain/build complexity |
| Mean defect resolution time | Feedback-loop efficiency |
| Uncovered failure paths | Test-system weakness |
| Determinism failures | Reproducibility risk |

Metrics must trigger investigation rather than become targets to game.

For example, "zero tests failing" is useful. "Write fewer tests so fewer can fail" is not quality management. It is accounting fraud with a compiler.

---

# 26. Continuous Improvement

TQM requires the project to improve its own development process.

After a significant defect:

```text
Defect
  ↓
Immediate fix
  ↓
Regression test
  ↓
Root-cause analysis
  ↓
Process/invariant improvement
  ↓
Document the lesson if reusable
```

Ask:

1. Why was the defect possible?
2. Why did the existing test not catch it?
3. Why did the review/build process not catch it?
4. Can the design make this class of defect impossible?
5. Should an invariant, helper, test fixture, or tool be added?

Do not stop at "fixed line 143."

The objective is:

```text
one defect
    ↓
one fix
    ↓
one less entire class of defects
```

---

# 27. Root-Cause Analysis

When a defect occurs, classify the root cause.

### Typical classes

- incorrect requirement;
- ambiguous contract;
- invalid assumption;
- ownership error;
- bounds error;
- lifetime error;
- error propagation error;
- serialization error;
- API/ABI mismatch;
- test gap;
- fixture mismatch;
- documentation drift;
- build-system defect;
- synchronization defect;
- performance regression.

The classification should describe **why the defect was possible**, not merely what line crashed.

---

# 28. ARKlight Synchronization Quality

carklight deliberately follows a released ARKlight baseline rather than tracking every in-progress change.

Therefore a synchronization change MUST record:

- ARKlight version/baseline;
- feature being imported;
- reference behavior;
- affected C stages/modules;
- affected fixtures;
- intentional differences;
- compatibility implications.

A feature is not considered synchronized merely because the C code compiles.

The required relationship is:

```text
ARKlight shipped behavior
        ↓
reference fixtures / documented contract
        ↓
carklight implementation
        ↓
C tests
        ↓
observable output
```

---

# 29. Line-Level Pre-Commit Checklist

Before committing even a small C change:

```text
[ ] What requirement does this line implement?
[ ] What invariant does it establish or preserve?
[ ] Does it introduce ownership?
[ ] Does it change ownership?
[ ] Can it fail?
[ ] What happens when it fails?
[ ] Can the value overflow, truncate, or become invalid?
[ ] Can the pointer be NULL?
[ ] Is the lifetime valid?
[ ] Is the access bounded?
[ ] Does this alter deterministic output?
[ ] Does this cross a module boundary?
[ ] Does this alter the public ABI?
[ ] Is a test needed?
[ ] Does an existing test actually prove the behavior?
[ ] Does documentation need to change?
[ ] Does the code remain warning-clean?
```

This is intentionally excessive for trivial code and exactly appropriate for a low-level library whose bugs can become memory corruption or ABI failures.

---

# 30. The carklight Quality Loop

The project should operate as a continuous loop:

```text
                    ┌──────────────────┐
                    │ Customer / Need  │
                    └────────┬─────────┘
                             ↓
                    ┌──────────────────┐
                    │ Requirement      │
                    └────────┬─────────┘
                             ↓
                    ┌──────────────────┐
                    │ Invariant        │
                    └────────┬─────────┘
                             ↓
                    ┌──────────────────┐
                    │ Implementation   │
                    └────────┬─────────┘
                             ↓
              ┌─────────────────────────────┐
              │ Build + Tests + Sanitizers │
              └──────────────┬──────────────┘
                             ↓
                    ┌──────────────────┐
                    │ Measured Result  │
                    └────────┬─────────┘
                             ↓
                    ┌──────────────────┐
                    │ Review / Release │
                    └────────┬─────────┘
                             ↓
                    ┌──────────────────┐
                    │ Feedback / Defect│
                    └────────┬─────────┘
                             │
                             └──────────→ Prevention / Improvement
```

---

# 31. Non-Negotiable Principles

These are the shortest possible version of the policy.

1. **Correctness before cleverness.**
2. **Make invalid states hard to represent.**
3. **Every allocation has an owner.**
4. **Every owner has a cleanup path.**
5. **Every fallible operation has defined failure behavior.**
6. **Every public contract is explicit.**
7. **Every important invariant is testable.**
8. **Warnings are defects.**
9. **Undefined behavior is never acceptable.**
10. **Deterministic output must stay deterministic.**
11. **Backends remain isolated.**
12. **IR remains backend-independent.**
13. **Documentation and implementation must agree.**
14. **A fixed defect gets a regression test.**
15. **Performance claims require measurement.**
16. **Small changes are preferred because they are easier to prove correct.**
17. **Quality is everyone's responsibility, including the person writing one line of C.**

---

# 32. Final Standard

A line of code is not "too small to matter."

A single unchecked index can violate memory safety.

A single missing `free` can violate resource correctness.

A single implicit ownership transfer can invalidate an ABI contract.

A single unordered traversal can destroy deterministic output.

A single stale comment can mislead the next implementation.

A single missing rejection test can turn a validator into a happy-path parser.

Therefore the carklight quality standard is:

> **Every layer must make the next layer safer, more predictable, and easier to verify.**

TQM for carklight is successful when quality stops being a final inspection step and becomes a property of the system's construction:

```text
line
  → function
    → module
      → pipeline
        → library
          → ABI
            → artifact
              → customer
```

The quality of the whole system is constrained by the weakest boundary in that chain.

That is the standard this repository should build toward.
