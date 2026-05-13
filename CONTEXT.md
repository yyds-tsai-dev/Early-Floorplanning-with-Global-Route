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

## Relationships

- A **Bundle Route** uses one or more **Channels** and may use one or more **Feedthroughs**.
- A **Feedthrough** is legal only through a soft block.
- A **Version 1.0 SA Baseline** must keep a legal deterministic seed path available, then use **B*-Tree SA Placement** to improve placement while preserving parser-valid output.
- **B*-Tree SA Placement** handles edge blocks through **Boundary-Constrained Edge Move** rather than pure penalty.
- **Channel-First Fallback Routing** may introduce **Feedthroughs** only after channel-only routing fails for a bundle.
- **Periodic Exact Routing Evaluation** keeps **B*-Tree SA Placement** routing-aware without running full routing on every move.
- **B*-Tree CA-SA Periodic Router** is the Version 1.0 main approach.

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

## Flagged Ambiguities

- "version1.0" was first resolved as **Version 1.0 Legal Baseline**, then changed to **Version 1.0 SA Baseline** at the user's request.
- "routing baseline" was resolved to mean **Channel-First Fallback Routing**, not aggressive feedthrough optimization.
- "SA placement" changed from **Sequence-Pair SA Placement** back to **B*-Tree SA Placement** at the user's request after comparing boundary-constrained representations.
- "edge-block handling" was resolved to mean **Boundary-Constrained Edge Move**, not fixed-only placement or soft penalty.
- "routing-aware SA" was resolved to mean **Periodic Exact Routing Evaluation**, not full routing on every SA move.
- "v1.0 main approach" was resolved to mean **B*-Tree CA-SA Periodic Router**.
