# Graph Report - .  (2026-07-06)

## Corpus Check
- 54 files · ~75,449 words
- Verdict: corpus is large enough that graph structure adds value.

## Summary
- 348 nodes · 556 edges · 27 communities detected
- Extraction: 82% EXTRACTED · 18% INFERRED · 0% AMBIGUOUS · INFERRED: 100 edges (avg confidence: 0.82)
- Token cost: 0 input · 0 output

## Community Hubs (Navigation)
- [[_COMMUNITY_Design Docs & Architecture|Design Docs & Architecture]]
- [[_COMMUNITY_Placement Engine (edge anchoring)|Placement Engine (edge anchoring)]]
- [[_COMMUNITY_Floorplanning Research Survey|Floorplanning Research Survey]]
- [[_COMMUNITY_Contest Spec Problem & Routing|Contest Spec: Problem & Routing]]
- [[_COMMUNITY_Algorithm Flow Diagrams|Algorithm Flow Diagrams]]
- [[_COMMUNITY_Legacy Floorplan & Geometry Model|Legacy Floorplan & Geometry Model]]
- [[_COMMUNITY_Validation & 0618 Regression|Validation & 0618 Regression]]
- [[_COMMUNITY_Scoring, Search & Main Driver|Scoring, Search & Main Driver]]
- [[_COMMUNITY_Routing & Hierarchy Research|Routing & Hierarchy Research]]
- [[_COMMUNITY_Routing & 0608 Regression|Routing & 0608 Regression]]
- [[_COMMUNITY_Contest Spec Blocks & Feedthrough|Contest Spec: Blocks & Feedthrough]]
- [[_COMMUNITY_Placement Engine Regression Tests|Placement Engine Regression Tests]]
- [[_COMMUNITY_CSV Parse & CFG IO|CSV Parse & CFG I/O]]
- [[_COMMUNITY_ML Placement Research|ML Placement Research]]
- [[_COMMUNITY_Hybrid Flow Diagram Script|Hybrid Flow Diagram Script]]
- [[_COMMUNITY_Algorithm Flow Diagram Script|Algorithm Flow Diagram Script]]
- [[_COMMUNITY_PlacementEngine Interface|PlacementEngine Interface]]
- [[_COMMUNITY_Feedthrough & Shift-Left Concepts|Feedthrough & Shift-Left Concepts]]
- [[_COMMUNITY_B-tree & Bus-Driven Floorplanning|B*-tree & Bus-Driven Floorplanning]]
- [[_COMMUNITY_Analytical Placers (DREAMPlace)|Analytical Placers (DREAMPlace)]]
- [[_COMMUNITY_Bundle Routing (FastRoute)|Bundle Routing (FastRoute)]]
- [[_COMMUNITY_Model Orchestration|Model Orchestration]]
- [[_COMMUNITY_Sequence-Pair Concept|Sequence-Pair Concept]]
- [[_COMMUNITY_Corblivar 3D Floorplanning|Corblivar 3D Floorplanning]]
- [[_COMMUNITY_GoodFloorplan RL|GoodFloorplan RL]]
- [[_COMMUNITY_Contest QA 0608|Contest QA 0608]]
- [[_COMMUNITY_Binary Naming Rule|Binary Naming Rule]]

## God Nodes (most connected - your core abstractions)
1. `routeProblem()` - 14 edges
2. `isSoft()` - 13 edges
3. `parseProblem()` - 12 edges
4. `placeEdge()` - 12 edges
5. `placeEdge()` - 12 edges
6. `main()` - 11 edges
7. `validateSolution()` - 11 edges
8. `isEdge()` - 11 edges
9. `firstLegalEdgeSlot()` - 10 edges
10. `Long-Term Placement Engine Design Spec` - 10 edges

## Surprising Connections (you probably didn't know these)
- `Multi-Stage Simulated Annealing` --semantically_similar_to--> `Two-Phase Simulated Annealing`  [INFERRED] [semantically similar]
  /home/yyds/Early-Floorplanning-with-Global-Route/docs/research/Integrated_floorplanning_and_interconnect_planning.pdf → /home/yyds/Early-Floorplanning-with-Global-Route/docs/research/deep_research_report_gpt_v2.md
- `Bin-Boundary Congestion/Overflow Estimation` --semantically_similar_to--> `Vertical-Slice Channel Model (25 nets/um)`  [INFERRED] [semantically similar]
  /home/yyds/Early-Floorplanning-with-Global-Route/docs/research/Integrated_floorplanning_and_interconnect_planning.pdf → /home/yyds/Early-Floorplanning-with-Global-Route/docs/research/deep_research_report_gemini.md
- `Simple-Geometry Routing (L-shaped / Z-shaped)` --semantically_similar_to--> `Guiding Point / Overlap Segment Wirelength`  [INFERRED] [semantically similar]
  /home/yyds/Early-Floorplanning-with-Global-Route/docs/research/Integrated_floorplanning_and_interconnect_planning.pdf → /home/yyds/Early-Floorplanning-with-Global-Route/docs/research/deep_research_report_gemini.md
- `testParsePortAndFtRates()` --calls--> `parseProblem()`  [INFERRED]
  test/regression_20260608.cpp → src/io.cpp
- `anyOverlap()` --calls--> `overlaps()`  [INFERRED]
  test/placement_engine_regression.cpp → src/model.hpp

## Hyperedges (group relationships)
- **Three Placement Engine Modes (sp/bstar/hybrid) under Shared Evaluator** — context_sequence_pair_engine, context_btree_engine, context_hybrid_placement_search, context_placement_engine_interface [EXTRACTED 1.00]
- **EFGR Solver Pipeline Stages** — claude_solver_pipeline, claude_two_tier_evaluation, claude_channel_first_fallback, claude_placement_engine_abstraction [EXTRACTED 0.75]
- **V1 B*-Tree CA-SA Design-to-Implementation Chain** — context_btree_ca_sa_periodic_router, docs_superpowers_specs_2026_05_14_v1_btree_ca_sa_periodic_router_design_design, docs_superpowers_plans_2026_05_14_v1_btree_ca_sa_periodic_router_plan [EXTRACTED 0.85]
- **FTAFP / Flora / Piano Feedthrough-Aware Floorplanner Trio** — deep_research_report_gemini_ftafp, deep_research_report_gemini_flora, deep_research_report_gemini_piano [EXTRACTED 0.95]
- **Three-Stage Heterogeneous Hybrid Flow** — deep_research_report_gemini_v2_heterogeneous_hybrid_flow, deep_research_report_gemini_flora, deep_research_report_gemini_ftafp, deep_research_report_gemini_piano [EXTRACTED 0.95]
- **Multi-Stage SA + Route-Proxy Cost Lineage** — integrated_floorplanning_and_interconnect_planning_multi_stage_sa, deep_research_report_gpt_v2_two_phase_sa, deep_research_report_gpt_ca_sa_route_proxy_verifier [INFERRED 0.75]
- **Global Routing / Channel / Feedthrough Mechanism** — e_20260331_global_route, e_20260331_channel, e_20260331_channel_wire_density_limit, e_20260331_feedthrough, e_20260331_ft_conversion, e_20260331_ft_area_increment [EXTRACTED 1.00]
- **Cost Function and Legality Scoring** — e_20260331_cost_function, e_20260331_hpwl, e_20260608_alpha_weight, e_20260331_penalty_conditions, e_20260331_fail_conditions [EXTRACTED 1.00]
- **Block Type Taxonomy and Constraints** — e_20260331_block, e_20260331_edge_block, e_20260331_hard_block, e_20260331_soft_block, e_20260608_port_edge, e_20260331_edge_location_12_regions [EXTRACTED 1.00]
- **Early Floorplanning Solver Pipeline** — docs_figures_algorithm_flow_problem_input, docs_figures_algorithm_flow_input_preparation, docs_figures_algorithm_flow_placement_mode, docs_figures_algorithm_flow_placement_start, docs_figures_algorithm_flow_placement_perturbation, docs_figures_algorithm_flow_placement_unpacking, docs_figures_algorithm_flow_fast_screening, docs_figures_algorithm_flow_evaluation_gate, docs_figures_algorithm_flow_channel_extraction, docs_figures_algorithm_flow_global_routing, docs_figures_algorithm_flow_route_aware_scoring, docs_figures_algorithm_flow_best_candidate_memory, docs_figures_algorithm_flow_solution_selection, docs_figures_algorithm_flow_legality_check, docs_figures_algorithm_flow_solution_output [INFERRED 0.85]
- **Hybrid Two-Phase Placement Flow** — docs_figures_hybrid_placement_flow_problem_input, docs_figures_hybrid_placement_flow_sequence_pair_start, docs_figures_hybrid_placement_flow_sequence_pair_perturbation, docs_figures_hybrid_placement_flow_sequence_pair_unpacking, docs_figures_hybrid_placement_flow_candidate_evaluation_phase_one, docs_figures_hybrid_placement_flow_best_sequence_pair_placement, docs_figures_hybrid_placement_flow_bstar_conversion, docs_figures_hybrid_placement_flow_bstar_start, docs_figures_hybrid_placement_flow_bstar_perturbation, docs_figures_hybrid_placement_flow_bstar_unpacking, docs_figures_hybrid_placement_flow_candidate_evaluation_phase_two, docs_figures_hybrid_placement_flow_placement_memory, docs_figures_hybrid_placement_flow_final_routed_output [INFERRED 0.85]

## Communities

### Community 0 - "Design Docs & Architecture"
Cohesion: 0.08
Nodes (40): Coding Style & Naming Conventions, Repository Guidelines (AGENTS.md), Channel-First Fallback Routing, Legacy floorplan.cpp Order-Only Placement, PlacementEngine Abstraction, EFGR Solver Project (ICCAD 2026 Problem E), Solver Pipeline (Parse-Search-Score-Route-Validate-Output), Two-Tier Evaluation (Cheap vs Exact) (+32 more)

### Community 1 - "Placement Engine (edge anchoring)"
Cohesion: 0.11
Nodes (33): addCandidate(), basePlacement(), baseState(), BStarEngine, chosenEdgeLocation(), collidesWithPlaced(), dimensionFor(), edgeRectLegal() (+25 more)

### Community 2 - "Floorplanning Research Survey"
Cohesion: 0.07
Nodes (30): Flora, Geometric Adjacency Graph Shortest Path, Guiding Point / Overlap Segment Wirelength, hMetis Hierarchical Graph Partitioning, Hierarchical Partitioning + MILP Boundary Optimizer, Multi-Stage Mask-Guided Network-Flow Method, MILP with SOS2 Piecewise Linearization, Multi-Commodity Network Flow Routing (+22 more)

### Community 3 - "Contest Spec: Problem & Routing"
Cohesion: 0.08
Nodes (30): CFG Output Format (OUTLINE/BLOCK/CHANNEL/PATH), Channel, Channel Wire Density Limit (25 nets/um), Chip Outline, Connection Flyline, On-Chip Interface Connection Matrix, Cost Function (OutlineArea + alpha*HPWL), Detour Route (+22 more)

### Community 4 - "Algorithm Flow Diagrams"
Cohesion: 0.08
Nodes (30): Algorithm Flow Diagram, Best Candidate Memory, Channel Extraction, Evaluation Gate, Fast Screening, Global Routing, Input Preparation, Legality Check (+22 more)

### Community 5 - "Legacy Floorplan & Geometry Model"
Cohesion: 0.19
Nodes (20): collidesWithPlaced(), decodeState(), dimensionFor(), initialState(), locHasBottom(), locHasLeft(), locHasRight(), locHasTop() (+12 more)

### Community 6 - "Validation & 0618 Regression"
Cohesion: 0.2
Nodes (21): locSegment(), locSide(), near(), portAllows(), positiveOverlap(), satisfiesEdgeLocation(), satisfiesEdgeLocations(), validateSolution() (+13 more)

### Community 7 - "Scoring, Search & Main Driver"
Cohesion: 0.18
Nodes (17): cheapScore(), exactScore(), ftRateForProxy(), intersectsOpen(), routeAwareProxyPenalty(), usedOutline(), argDouble(), argInt() (+9 more)

### Community 8 - "Routing & Hierarchy Research"
Cohesion: 0.11
Nodes (20): Corner Stitching, FTAFP, Slack-Driven SCB-Tree + Concurrent A* Router, SCB-Tree (Slack Computation + Corner Stitching), Slack Computation Perturbation, Boundary Shrinking Phase, Constraints-Aware Parallel SA + Route Proxy + Verifier, CUGR (+12 more)

### Community 9 - "Routing & 0608 Regression"
Cohesion: 0.27
Nodes (14): edgeFromTo(), extractChannels(), ftRateFor(), guidePoint(), portAllows(), positiveTouch(), routeProblem(), fail() (+6 more)

### Community 10 - "Contest Spec: Blocks & Feedthrough"
Cohesion: 0.14
Nodes (16): Aspect Ratio Range, Block (IP Block), Edge Block, Edge Block Location (12 Regions), Feedthrough (FT), Feedthrough Count (ports/2), FT Area Increment Formula, FT Conversion Efficiency Table (+8 more)

### Community 11 - "Placement Engine Regression Tests"
Cohesion: 0.48
Nodes (11): makePlacementEngine(), anyOverlap(), fail(), main(), makeEdgeAnchoredProblem(), makeThreeSoftProblem(), testBStarInitialDecodeAndMutationValidity(), testEdgeBlocksStayAnchored() (+3 more)

### Community 12 - "CSV Parse & CFG I/O"
Cohesion: 0.38
Nodes (11): hasPercent(), parseAspect(), parseDouble(), parseProblem(), parseType(), readCsv(), splitEdges(), splitLocations() (+3 more)

### Community 13 - "ML Placement Research"
Cohesion: 0.33
Nodes (6): AlphaChip / Circuit Training, CNN-Based Routability Prediction in SA, FlexPlanner, FloorSet Dataset, Learning-Augmented Warm-Start / Surrogate, Routability-Driven Macro Placement

### Community 14 - "Hybrid Flow Diagram Script"
Cohesion: 0.7
Nodes (4): arrow(), box(), dashed_group(), main()

### Community 15 - "Algorithm Flow Diagram Script"
Cohesion: 0.7
Nodes (4): arrow(), box(), group(), main()

### Community 16 - "PlacementEngine Interface"
Cohesion: 0.67
Nodes (1): PlacementEngine

### Community 17 - "Feedthrough & Shift-Left Concepts"
Cohesion: 0.67
Nodes (3): Feedthrough Conversion Efficiency (Stepwise Area Penalty), Shift-Left EDA Paradigm, Early Floorplanning with Global Route (Problem E)

### Community 18 - "B*-tree & Bus-Driven Floorplanning"
Cohesion: 1.0
Nodes (2): B*-Tree + Fast-SA (Chen/Chang), Bus-Driven Floorplanning

### Community 19 - "Analytical Placers (DREAMPlace)"
Cohesion: 1.0
Nodes (2): AutoDMP, DREAMPlace

### Community 20 - "Bundle Routing (FastRoute)"
Cohesion: 1.0
Nodes (2): Bundle/Commodity Routing, FastRoute

### Community 27 - "Model Orchestration"
Cohesion: 1.0
Nodes (1): Multi-Model Orchestration Workflow

### Community 28 - "Sequence-Pair Concept"
Cohesion: 1.0
Nodes (1): Sequence Pair (Murata)

### Community 29 - "Corblivar 3D Floorplanning"
Cohesion: 1.0
Nodes (1): Corblivar (3D IC Floorplanning)

### Community 30 - "GoodFloorplan RL"
Cohesion: 1.0
Nodes (1): GoodFloorplan (GCN + RL)

### Community 31 - "Contest QA 0608"
Cohesion: 1.0
Nodes (1): Problem E Q&A (2026-06-08)

### Community 32 - "Binary Naming Rule"
Cohesion: 1.0
Nodes (1): Binary Naming/CLI Convention

## Ambiguous Edges - Review These
- `Candidate Evaluation (Phase One)` → `Candidate Evaluation (Phase Two)`  [AMBIGUOUS]
  /home/yyds/Early-Floorplanning-with-Global-Route/docs/figures/hybrid_placement_flow.png · relation: conceptually_related_to

## Knowledge Gaps
- **73 isolated node(s):** `GappedEngine`, `SequencePairEngine`, `BStarEngine`, `Multi-Model Orchestration Workflow`, `Legacy floorplan.cpp Order-Only Placement` (+68 more)
  These have ≤1 connection - possible missing edges or undocumented components.
- **Thin community `PlacementEngine Interface`** (3 nodes): `placement_engine.hpp`, `PlacementEngine`, `.PlacementEngine()`
  Too small to be a meaningful cluster - may be noise or needs more connections extracted.
- **Thin community `B*-tree & Bus-Driven Floorplanning`** (2 nodes): `B*-Tree + Fast-SA (Chen/Chang)`, `Bus-Driven Floorplanning`
  Too small to be a meaningful cluster - may be noise or needs more connections extracted.
- **Thin community `Analytical Placers (DREAMPlace)`** (2 nodes): `AutoDMP`, `DREAMPlace`
  Too small to be a meaningful cluster - may be noise or needs more connections extracted.
- **Thin community `Bundle Routing (FastRoute)`** (2 nodes): `Bundle/Commodity Routing`, `FastRoute`
  Too small to be a meaningful cluster - may be noise or needs more connections extracted.
- **Thin community `Model Orchestration`** (1 nodes): `Multi-Model Orchestration Workflow`
  Too small to be a meaningful cluster - may be noise or needs more connections extracted.
- **Thin community `Sequence-Pair Concept`** (1 nodes): `Sequence Pair (Murata)`
  Too small to be a meaningful cluster - may be noise or needs more connections extracted.
- **Thin community `Corblivar 3D Floorplanning`** (1 nodes): `Corblivar (3D IC Floorplanning)`
  Too small to be a meaningful cluster - may be noise or needs more connections extracted.
- **Thin community `GoodFloorplan RL`** (1 nodes): `GoodFloorplan (GCN + RL)`
  Too small to be a meaningful cluster - may be noise or needs more connections extracted.
- **Thin community `Contest QA 0608`** (1 nodes): `Problem E Q&A (2026-06-08)`
  Too small to be a meaningful cluster - may be noise or needs more connections extracted.
- **Thin community `Binary Naming Rule`** (1 nodes): `Binary Naming/CLI Convention`
  Too small to be a meaningful cluster - may be noise or needs more connections extracted.

## Suggested Questions
_Questions this graph is uniquely positioned to answer:_

- **What is the exact relationship between `Candidate Evaluation (Phase One)` and `Candidate Evaluation (Phase Two)`?**
  _Edge tagged AMBIGUOUS (relation: conceptually_related_to) - confidence is low._
- **Why does `isSoft()` connect `Legacy Floorplan & Geometry Model` to `Routing & 0608 Regression`, `Placement Engine (edge anchoring)`, `Validation & 0618 Regression`, `Scoring, Search & Main Driver`?**
  _High betweenness centrality (0.042) - this node is a cross-community bridge._
- **Why does `makePlacementEngine()` connect `Placement Engine Regression Tests` to `Placement Engine (edge anchoring)`, `Validation & 0618 Regression`, `Scoring, Search & Main Driver`?**
  _High betweenness centrality (0.033) - this node is a cross-community bridge._
- **Why does `main()` connect `Scoring, Search & Main Driver` to `Placement Engine Regression Tests`, `CSV Parse & CFG I/O`, `Validation & 0618 Regression`?**
  _High betweenness centrality (0.031) - this node is a cross-community bridge._
- **Are the 7 inferred relationships involving `routeProblem()` (e.g. with `testHorizontalChannelCapacityUsesHeight()` and `testFeedthroughMismatchIsMeasured()`) actually correct?**
  _`routeProblem()` has 7 INFERRED edges - model-reasoned connections that need verification._
- **Are the 12 inferred relationships involving `isSoft()` (e.g. with `routeAwareProxyPenalty()` and `routeProblem()`) actually correct?**
  _`isSoft()` has 12 INFERRED edges - model-reasoned connections that need verification._
- **Are the 4 inferred relationships involving `parseProblem()` (e.g. with `testParsePortAndFtRates()` and `testBomCsvAndMultiPortEdgeRoute()`) actually correct?**
  _`parseProblem()` has 4 INFERRED edges - model-reasoned connections that need verification._