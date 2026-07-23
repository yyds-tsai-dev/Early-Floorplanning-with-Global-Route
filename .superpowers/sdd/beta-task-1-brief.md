### Task 1: Extract shared route-accounting helpers (byte-identical refactor)

**Files:**
- Create: `src/route_geometry.hpp`
- Modify: `src/routing.cpp` (move code out; include the new header)
- Test: byte-identity harness below (no new .cpp — uses the solver binary)

**Interfaces:**
- Consumes: `Rect`, `Channel`, `kEps`, `manhattan` from `src/model.hpp`.
- Produces (exact signatures Task 2 builds on):

```cpp
namespace efgr {
// Edge numbering (QA A22): 1 = left, 2 = top, 3 = right, 4 = bottom (clockwise from left).
int edgeFromTo(const Rect& a, const Rect& b);              // moved verbatim from routing.cpp:24
std::pair<double, double> guidePoint(const Rect& a, const Rect& b);  // moved verbatim from routing.cpp:31

// One routed rect-sequence's contribution to wirelength and per-channel directional usage.
// `rect_of(i)` returns the rect of the i-th node in the path; `channel_of(i)` returns the
// Channel* if node i is a channel else nullptr. Implements exactly the accumulation loop
// currently inlined in routing.cpp:316-345: guides = midpoints of consecutive shared borders
// (QA A41), per-segment dx>kEps charges horizontal_usage, dy>kEps charges vertical_usage
// on the channel that owns the segment, and WL = nets * Σ manhattan(consecutive guides).
double accumulatePathUsage(const std::vector<Rect>& node_rects,
                           const std::vector<Channel*>& node_channels,
                           double nets);
}
```

- [ ] **Step 1: Capture the pre-refactor baseline**

```bash
cd /home/yyds/Early-Floorplanning-with-Global-Route && make >/dev/null
for i in 0 1 2 3 4; do
  bin/efgr "test/2026 ICCAD_case.case$i.csv" /tmp/base_case$i.cfg --seed 1 --iters 200 2>/dev/null
done
md5sum /tmp/base_case*.cfg > /tmp/baseline.md5
```

- [ ] **Step 2: Create `src/route_geometry.hpp` and move the helpers**

Move `edgeFromTo` (routing.cpp:24) and `guidePoint` (routing.cpp:31) into the header as `inline` functions, unchanged. Extract the accumulation loop at routing.cpp:316-345 into `accumulatePathUsage` with the signature above; `routing.cpp` calls it wherever the loop lived, passing the same rect/channel sequence it already builds (`guides.push_back(guidePoint(...))` at :316, the `dx/dy` usage stamps at :331-333, steps emission at :342-343 stays in routing.cpp — only WL+usage accumulation moves). The function must be a **verbatim transplant**: same iteration order, same `kEps` comparisons, same `ch.usage = std::max(h,v)` update.

- [ ] **Step 3: Verify byte-identity**

```bash
make >/dev/null
for i in 0 1 2 3 4; do
  bin/efgr "test/2026 ICCAD_case.case$i.csv" /tmp/post_case$i.cfg --seed 1 --iters 200 2>/dev/null
done
sed 's|/tmp/base_|/tmp/post_|' /tmp/baseline.md5 | md5sum -c -
```

Expected: five lines of `OK`. Any mismatch = the transplant changed behavior — fix before proceeding.

- [ ] **Step 4: Full suites**

Run: `for s in run_case00 run_placement_engine_regressions run_20260618_regressions run_20260608_regressions run_p0_regressions run_p1_regressions run_p2_regressions run_p3_regressions; do bash test/$s.sh || exit 1; done`
Expected: all green.

- [ ] **Step 5: Commit**

```bash
git add src/route_geometry.hpp src/routing.cpp
git commit -m "refactor(routing): extract shared route-geometry accounting helpers"
```

---

