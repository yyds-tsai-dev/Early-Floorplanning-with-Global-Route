# Long-Term Placement Engine Design

## Goal

The solver's long-term optimization goal is to build a strong contest-grade placement/search framework, not a quick incremental patch. We explicitly accept large implementation scope and long development time when that produces a better optimizer. The target direction is a shared placement engine interface with both Sequence-Pair and B*-Tree backends, plus a hybrid comparison flow that can use each representation where it is strongest.

This design supersedes the earlier assumption that a single B*-Tree baseline is the only main placement representation. The existing router, parser, evaluator, and validator remain valuable shared infrastructure, but placement/search should become a pluggable optimization layer.

## Non-Goals

- Do not optimize only for the fastest short-term score improvement.
- Do not choose Sequence-Pair or B*-Tree by preference before measuring them under the same evaluator.
- Do not replace the official-spec parser, channel extraction, routing evaluator, or validator unless the placement engine needs a clean interface to them.
- Do not add GPU dependencies; the official Q&A says the contest environment does not support GPU acceleration.

## Strategic Direction

Implement and compare three placement modes:

1. `sp`: Sequence-Pair simulated annealing for global exploration.
2. `bstar`: B*-Tree simulated annealing with contour decode for local compact refinement.
3. `hybrid`: Sequence-Pair global search followed by B*-Tree local refinement.

All modes must use the same `Problem`, `Placement`, `cheapScore`, `exactScore`, `validateSolution`, and `.cfg` writer so comparisons are meaningful.

## Shared Placement Engine Interface

Introduce a placement-engine abstraction that decouples SA/search orchestration from the representation:

```text
PlacementEngine
  name()
  initialState(problem, rng)
  mutate(problem, state, rng)
  decode(problem, state)
  clone(state)
```

The concrete C++ implementation may use templates, tagged variants, or separate engine classes. The important boundary is behavioral: `main.cpp` should not know whether the current candidate came from a Sequence-Pair or B*-Tree representation.

## Sequence-Pair Backend

Sequence-Pair represents non-edge blocks with two permutations. Pairwise order in the permutations determines whether one block is left/right or below/above another. Decode builds horizontal and vertical constraint graphs and solves longest paths to produce compact coordinates.

Expected strengths:

- Broad topology coverage for global exploration.
- Easy to express relative placement constraints.
- Good fit for block count under 50, where `O(n^2)` decode is acceptable.
- Natural comparison baseline against B*-Tree.

Expected costs:

- More global move effects; SA temperature and move weights matter.
- Decode is slower than contour-based B*-Tree.
- Extra care is needed to keep edge blocks anchored and non-edge blocks inside the outline.

Required moves:

- swap two entries in the first sequence,
- swap two entries in the second sequence,
- swap the same pair in both sequences,
- perturb one soft block aspect ratio,
- optional macro/edge rotation remains disabled because Q&A says Macro/Edge rotation is not allowed.

## B*-Tree Backend

B*-Tree represents compact packings with tree topology. Decode uses a contour structure to place each non-edge block relative to its parent, while edge blocks remain anchored by official LOCATION constraints.

Expected strengths:

- Fast decode and local refinement.
- Mature SA move set for floorplanning.
- Good at compacting area once global topology is reasonable.

Expected costs:

- More representation bias than Sequence-Pair.
- Harder to make edge anchors, routing whitespace, and feedthrough-driven area changes clean.
- Requires careful tree validity checks after every move.

Required moves:

- swap node labels,
- delete and insert a node,
- move a subtree when validity can be preserved,
- perturb one soft block aspect ratio,
- preserve fixed Macro/Edge orientation.

## Hybrid Mode

Hybrid mode is the long-term recommended optimizer:

1. Run Sequence-Pair SA for global topology exploration.
2. Convert the best Sequence-Pair placement to a B*-Tree-compatible seed.
3. Run B*-Tree SA for compact local refinement.
4. Periodically exact-route elite candidates using the existing routing evaluator.

The conversion does not need to be mathematically perfect at first. A practical conversion can sort blocks by `(x, y)` and build a legal initial B*-Tree, then let B*-Tree SA refine. Later upgrades can infer stronger parent/child relationships from adjacency.

## Routing-Aware Search

Placement search must optimize more than outline area and center HPWL. Cheap scoring should include:

- block overlap and outline fail penalties,
- edge LOCATION and port-edge fail penalties,
- weighted connection HPWL,
- channel-space proxy based on vertical-strip capacity,
- soft feedthrough risk proxy,
- optional exact-score replay for elite candidates.

Exact routing remains too expensive for every move. Use periodic exact routing, elite-candidate exact routing, and placement-key caching.

## Comparison Framework

Add CLI support:

```text
--engine sp|bstar|hybrid
--iters N
--seed S
--exact-interval K
```

For each run, print:

- engine name,
- iterations,
- exact evaluations,
- final score,
- outline area,
- wirelength,
- channel overflow,
- feedthrough mismatch,
- open routes,
- validation status.

Regression scripts should run at least `sp` and `bstar` on the same seed and testcase. Hybrid can be tested once both individual engines are stable.

## Testing Strategy

Add deterministic tests for:

- Sequence-Pair decode produces non-overlapping placement for small cases.
- Sequence-Pair relative order constraints are respected.
- B*-Tree contour decode produces compact non-overlapping placement.
- B*-Tree mutation preserves a valid tree.
- Edge blocks stay anchored under both engines.
- Macro/Edge dimensions are never rotated.
- The same solver run with the same engine and seed is deterministic.
- `--engine sp`, `--engine bstar`, and `--engine hybrid` all emit parser-valid `.cfg` files once implemented.

## Documentation Rule

Future sessions should treat this file and `CONTEXT.md` as the active placement/search direction. Short-term performance patches are acceptable only when they support this long-term architecture. The solver should move toward representation-backed placement engines and measurable engine comparison, not more coordinate-only first-open-slot mutations.
