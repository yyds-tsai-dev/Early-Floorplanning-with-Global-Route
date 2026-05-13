# Version 1.0 B*-Tree CA-SA Periodic Router Design

## Goal

Version 1.0 builds a CPU-only contest solver that can parse the official CSV format, search placements with B*-tree simulated annealing, periodically evaluate exact-ish routing, and emit a parser-valid `.cfg` for every testcase. It is an optimization baseline, not only a legality baseline, but it deliberately keeps the first router simple enough to debug and extend.

## Non-Goals

- No GPU dependency in the final solver.
- No full SCB-tree or corner-stitching implementation in v1.0.
- No full FastRoute-style negotiated rip-up/reroute history system in v1.0.
- No direct integration of PARSAC, OpenROAD, CUGR, or other external solvers.

## Architecture

The implementation will live under `src/` and be split into focused modules:

- `io`: parse contest CSV files and write `.cfg` output.
- `model`: shared structs for blocks, outline, connection bundles, placements, channels, routes, and scores.
- `floorplan`: B*-tree representation, contour decoder, soft block aspect-ratio assignment, and boundary-aware edge block handling.
- `sa`: constraints-aware simulated annealing loop, legal seed generation, B*-tree moves, edge legal-location moves, and top-K elite archive.
- `routing`: official-style vertical channel extraction and bundled shortest-path routing.
- `eval`: cheap per-step proxy scoring and periodic exact routing score.
- `validate`: internal checks for parser safety, overlap, outline, routing open, channel overflow, and feedthrough area mismatch.

## Placement Search

The placement representation is B*-tree SA. The deterministic seed creates a legal starting point when possible, then SA mutates it with:

- swap two nodes,
- move a subtree,
- rotate hard macro only if the input permits orientation changes,
- change soft block aspect ratio,
- change an edge block among legal locations on its required side.

Edge blocks use boundary-constrained B*-tree rules plus limited legal-location moves. They are not handled by penalty alone. TL/TM/TR/BL/BM/BR/LT/LM/LB/RT/RM/RB are treated as legal regions on a required boundary side.

Soft blocks start with an aspect-ratio-compliant shape near square area and can change shape during SA. Feedthrough-driven inflation is tracked in exact routing evaluation and penalized if the final dimensions do not match required area.

## Routing Evaluation

Every SA candidate receives a cheap score:

- outline area estimate,
- overlap, outline, and edge legality penalties,
- weighted block-pair distance from connection bundles,
- coarse congestion proxy,
- coarse feedthrough risk proxy.

Periodic or elite candidates receive exact-ish routing evaluation:

1. Decode placement.
2. Extract official vertical-strip channels from block left and right edges plus outline boundaries.
3. Build a rectangle adjacency graph from blocks and channels.
4. Route every nonzero connection matrix entry as one bundle with its net count.
5. Prefer channel-only shortest paths.
6. If channel-only routing cannot connect endpoints, allow soft blocks as fallback transit nodes and count feedthrough.
7. Compute route wirelength from adjacent rectangle overlap guiding points.
8. Track channel capacity usage and soft block feedthrough counts.

Hard and edge blocks may be sources or sinks but cannot be transit resources. Soft blocks may be fallback transit resources.

## Scoring

The internal score is:

```text
score =
  outline_area
  + alpha * total_wirelength
  + P_overlap
  + P_outline
  + P_route_open
  + P_edge_constraint
  + P_channel_overflow
  + P_feedthrough_mismatch
  + P_parser_safety
```

`P_overlap`, `P_outline`, `P_route_open`, `P_edge_constraint`, and `P_parser_safety` are huge because they correspond to fail risks. `P_channel_overflow` and `P_feedthrough_mismatch` are tunable because the official Q&A says they are proportional penalties, not fail conditions.

The final `.cfg` is written from the best exact-evaluated candidate, not merely the best cheap-proxy candidate.

## Output

The `.cfg` writer emits:

- outline section,
- block section with final lower-left coordinates and dimensions,
- channel section from extracted vertical-strip channels,
- path section for each routed bundle.

The writer validates all block/channel/path references before output. Rectangle edge ids follow the official convention: left 1, top 2, right 3, bottom 4.

## Error Handling

Parsing errors include file location and the section being parsed. Candidate evaluation never crashes the whole run; invalid candidates receive a large score penalty and SA continues. Final output requires at least one exact-evaluated candidate that passes internal parser-safety and no-open-route checks. If no candidate passes, the solver falls back to the deterministic seed and reports validation failures in diagnostics.

## Testing

Initial tests target `test/E_testcase_20260414.xlsx - case00.csv`:

- parse blocks, outline, alpha, conversion table, and connection matrix,
- generate a deterministic seed,
- decode B*-tree without block overlap,
- enforce edge block boundary legality,
- extract vertical-strip channels,
- route every bundle or report a route-open validation error,
- write `.cfg` with valid section structure and edge ids,
- run internal validation on the written solution.

Regression tests should keep small hand-authored cases for parser and geometry edge cases, including zero connections, one soft block, edge-only endpoints, disconnected channel-only paths, and soft fallback feedthrough.

## Later Upgrades

After v1.0 is stable, likely upgrades are:

- negotiated rip-up/reroute with congestion history,
- SCB-tree or slack-aware moves,
- stronger feedthrough inflation repair,
- parallel multi-seed execution,
- alpha sweep and top-K replay,
- visualizer for placements, channels, and routes.
