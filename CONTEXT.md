# Early Floorplanning With Global Route

This context captures the contest-specific language for the ICCAD Problem E solver so design and implementation choices stay aligned with the official problem.

## Language

**Version 1.0 SA Baseline**:
The first solver milestone that produces a parser-valid, non-overlapping, in-outline, fully routed `.cfg` while also running a basic simulated annealing search over placement choices.
_Avoid_: legal-only baseline, mature contest solver

**Bundle Route**:
A routed path representing one nonzero block-pair entry in the connection matrix and carrying that many nets.
_Avoid_: pin-level net route

**Channel**:
A routing rectangle produced by vertical slicing of the outline using block left and right edges, then taking the uncovered y-intervals in each strip.
_Avoid_: uniform gcell, arbitrary routing grid

**Feedthrough**:
A route segment that passes through a soft block and increases that soft block's required area according to the conversion table.
_Avoid_: free over-block routing

**Channel-First Fallback Routing**:
The routing policy that attempts to connect each bundle through channels first and uses soft-block feedthrough only when channel-only routing cannot connect the endpoints.
_Avoid_: feedthrough-first routing, unrestricted soft-block routing

**B*-Tree SA Placement**:
The Version 1.0 placement search representation that uses B*-tree topology moves inside simulated annealing, then decodes candidate floorplans for legality and routing evaluation.
_Avoid_: coordinate-only SA, sequence-pair baseline

**Boundary-Constrained Edge Move**:
The edge-block policy that enforces B*-tree boundary feasibility for the required side while allowing limited moves among legal regions on that side.
_Avoid_: edge penalty only, fully fixed edge block

**Periodic Exact Routing Evaluation**:
The SA evaluation policy that scores every candidate with cheap geometry and routing proxies, then runs full channel extraction plus bundled shortest-path routing only periodically or for elite candidates.
_Avoid_: route every move, route only at the end

**B*-Tree CA-SA Periodic Router**:
The approved Version 1.0 main approach combining boundary-aware B*-tree simulated annealing, constraints-aware moves, cheap per-step proxies, and periodic exact routing evaluation.
_Avoid_: full negotiated router first, placement-only SA

**Long-Term Placement/Search Objective**:
The current strategic objective after the 2026-06-14 discussion. The user explicitly prefers long-term optimizer quality over short implementation time. Large rewrites, longer implementation cycles, and multiple data structures are acceptable when they improve placement/search quality.
_Avoid_: quick-only optimization, conservative router-only tuning

**Placement Engine Interface**:
The planned abstraction that lets the solver run different placement representations behind the same scoring, routing, validation, and output pipeline.
_Avoid_: hard-wiring SA to one data structure in `main.cpp`

**Sequence-Pair Engine**:
A planned placement backend using two block permutations and constraint-graph/longest-path decode. It is intended for broad global topology exploration and comparison against B*-tree search.
_Avoid_: treating sequence-pair as a quick replacement that prevents B*-tree work

**B*-Tree Engine**:
A planned placement backend using real B*-tree topology and contour decode. It is intended for compact local refinement and direct comparison against sequence-pair search.
_Avoid_: the current order-only first-open-slot placement being called real B*-tree

**Hybrid Placement Search**:
The planned long-term mode that runs Sequence-Pair global exploration first, converts the best placement into a B*-tree-compatible seed, then runs B*-tree local refinement.
_Avoid_: choosing one representation permanently before measuring both

**Checker Cost**:
The official per-case validator cost reported in the alpha-test `report.json`/`checker.log`: outline area + α·wirelength + overflow/FT penalty, with α read from the testcase CSV and no runtime term.
_Avoid_: treating this as the scoreboard ranking number

**Ranking Cost**:
The official scoreboard cost reported in the alpha-test `case_result.json`/`result.html`: outline area + α·wirelength + overflow/FT penalty + edge penalty + runtime penalty, where the harness applied per-case α values (0.3/0.15/0.1/0.1/0.1 for case0–4) that differ from the CSV α.
_Avoid_: assuming the CSV α is what ranking used at alpha

**Runtime Penalty**:
The ranking-cost term δ · runtime_seconds · √(outline area), with δ = 0.5 at alpha test. Negligible at seconds-scale runtimes; grows linearly with wall time.
_Avoid_: treating runtime as free, treating the 2-hour budget as the only time cost

**Official Overflow Penalty**:
β_lin · outline area · R plus a β_tail term once R exceeds the 5% threshold, where R = total channel overflow / total channel capacity. Constants at alpha test: β_lin = 20, β_tail = 1.5, β_ft = 3, γ (edge) = 1.
_Avoid_: per-net flat penalties, per-channel local penalties

**Integer Output Rule**:
All output coordinates and dimensions must be integers (QA A43). A soft block whose integer W×H cannot equal AREA must choose W×H strictly below AREA. The checker auto-ceils fractional output without penalty today.
_Avoid_: relying on auto-ceil, fractional soft-block dimensions

**Guiding-Point Chain (official wirelength)**:
The checker measures a path's wirelength as the manhattan chain of center-anchored guiding points: for each hop, the guiding point is the midpoint of the two nodes' facing-edge midpoints, with the shared border chosen by minimum gap (pinned 2026-07-23 from the problem statement's "guiding points → HPWL" definition plus the alpha viz polylines; reproduces official WL to ≤1e-6 on all 5 alpha cases). QA A41's "midpoint of the shared border" phrasing describes the same chain loosely — the center-anchored form is the byte-verified one.
_Avoid_: straight-through transitions, overlap-segment midpoints as the anchor

**Per-Edge Channel Demand (official overflow)**:
The checker charges channel demand per traversal edge: each entry edge and each exit edge of a channel traversal charges the full net count to that edge's axis (left/right → horizontal, top/bottom → vertical), so a straight pass-through charges its axis twice. Byte-verified against all alpha per-channel demand tables.
_Avoid_: per-segment dx/dy charging, single-charge pass-throughs

**Alpha-Test Package**:
The official per-team feedback bundle under `docs/official/alpha_test/<team>/`: per-case `run.log` (harness invocation + our stderr), `checker.log`, `report.json` (checker detail), `output.cfg`, `viz/`, plus top-level `case_result.json` and `result.html` (scoreboard).
_Avoid_: mixing up which file carries checker cost versus ranking cost

**Diagonal Connection Erratum**:
Self-connections present in the case3 matrix (BLK09→BLK09 = 63, BLK13→BLK13 = 123). Organizers confirmed these are data errors (QA A44) but the 20260712 testcase upload still contains them; the checker treats them as informational only.
_Avoid_: routing self-bundles, failing the parse on diagonal entries

## Relationships

- A **Bundle Route** uses one or more **Channels** and may use one or more **Feedthroughs**.
- A **Feedthrough** is legal only through a soft block.
- A **Version 1.0 SA Baseline** must keep a legal deterministic seed path available, then use **B*-Tree SA Placement** to improve placement while preserving parser-valid output.
- **B*-Tree SA Placement** handles edge blocks through **Boundary-Constrained Edge Move** rather than pure penalty.
- **Channel-First Fallback Routing** may introduce **Feedthroughs** only after channel-only routing fails for a bundle.
- **Periodic Exact Routing Evaluation** keeps **B*-Tree SA Placement** routing-aware without running full routing on every move.
- **B*-Tree CA-SA Periodic Router** is the Version 1.0 main approach.
- **Long-Term Placement/Search Objective** supersedes conservative v1-only optimization as the active strategic direction.
- **Placement Engine Interface** owns the boundary between search representations and shared evaluation.
- **Sequence-Pair Engine** and **B*-Tree Engine** should both feed the same `cheapScore`, `exactScore`, `validateSolution`, and `.cfg` writer.
- **Hybrid Placement Search** combines **Sequence-Pair Engine** global exploration with **B*-Tree Engine** local refinement.

## Example Dialogue

> **Dev:** "Should version1.0 include parallel simulated annealing?"
> **Domain expert:** "Yes, but keep it basic. **Version 1.0 SA Baseline** should include SA search while retaining a deterministic legal fallback."
>
> **Dev:** "Should a bundle cross a soft block if that shortens the path?"
> **Domain expert:** "Only as fallback. **Channel-First Fallback Routing** keeps v1.0 easier to validate."
>
> **Dev:** "Should placement SA mutate coordinates directly?"
> **Domain expert:** "No. Use **B*-Tree SA Placement** for version1.0 so boundary-aware topology moves can evolve into a stronger contest solver."
>
> **Dev:** "Should edge blocks be fixed or just penalized?"
> **Domain expert:** "Use **Boundary-Constrained Edge Move**: preserve B*-tree boundary feasibility and allow limited legal-region moves."
>
> **Dev:** "Should every SA candidate run the exact router?"
> **Domain expert:** "No. Use **Periodic Exact Routing Evaluation** so cheap proxies guide most moves and exact routing validates promising candidates."
>
> **Dev:** "What is the approved v1.0 architecture?"
> **Domain expert:** "**B*-Tree CA-SA Periodic Router**: boundary-aware B*-tree SA plus periodic exact routing."
>
> **Dev:** "Should we keep making conservative router-only improvements?"
> **Domain expert:** "No. The active direction is the **Long-Term Placement/Search Objective**: build a shared placement engine interface, implement both **Sequence-Pair Engine** and **B*-Tree Engine**, and compare them under the same evaluator."
>
> **Dev:** "Do we need to choose sequence-pair or B*-tree now?"
> **Domain expert:** "No. Implement both. Use **Hybrid Placement Search** later to combine sequence-pair global exploration with B*-tree local refinement."

## Flagged Ambiguities

- "version1.0" was first resolved as **Version 1.0 Legal Baseline**, then changed to **Version 1.0 SA Baseline** at the user's request.
- "routing baseline" was resolved to mean **Channel-First Fallback Routing**, not aggressive feedthrough optimization.
- "SA placement" changed from **Sequence-Pair SA Placement** back to **B*-Tree SA Placement** at the user's request after comparing boundary-constrained representations.
- "edge-block handling" was resolved to mean **Boundary-Constrained Edge Move**, not fixed-only placement or soft penalty.
- "routing-aware SA" was resolved to mean **Periodic Exact Routing Evaluation**, not full routing on every SA move.
- "v1.0 main approach" was resolved to mean **B*-Tree CA-SA Periodic Router**.
- "placement/search direction after 2026-06-14" was resolved to mean **Long-Term Placement/Search Objective**, not quick conservative tuning.
- "Sequence-pair versus B*-tree" was resolved to mean implement and compare both through a **Placement Engine Interface**, not pick only one.
- "hybrid optimizer" was resolved to mean sequence-pair global exploration followed by B*-tree local refinement.
- "which α is authoritative for ranking" (QA A35.5 says testcase-defined = 0.2, but the alpha scoreboard applied per-case 0.3/0.15/0.1/0.1/0.1) — **team decision 2026-07-22**: the solver optimizes the CSV α; benchmarks report both **Checker Cost** and **Ranking Cost** side by side; the contradiction goes back to the organizers as a QA question. Official answer still pending.

## Current Strategic Design

The active long-term design is documented in `docs/superpowers/specs/2026-06-14-long-term-placement-engine-design.md`.

Future implementation should move toward:

- `--engine sp`
- `--engine bstar`
- `--engine hybrid`
- deterministic engine comparison under the same evaluator
- replacing the current order-only first-open-slot placement with real representation-backed decode

Short-term routing or evaluator patches are still allowed, but they should support this placement/search architecture rather than distract from it.
