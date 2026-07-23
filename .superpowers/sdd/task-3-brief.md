### Task 3: Direct abutment = zero wirelength

**Files:**
- Modify: `src/routing.cpp:186` (delete the 1-guide special case)
- Test: `test/regression_p0_20260706.cpp`

- [ ] **Step 1: Write the failing test:**

```cpp
void testDirectAbutmentZeroWirelength() {
    Problem p;
    auto addSoft = [&](const std::string& name, double x_hint) {
        (void)x_hint;
        Block b;
        b.name = name;
        b.area = 10000.0;
        b.type = BlockType::Soft;
        b.ar_min = 0.25;
        b.ar_max = 4.0;
        b.ft_rates = {0.2, 0.4, 0.8, 1.0};
        p.block_index[name] = static_cast<int>(p.blocks.size());
        p.blocks.push_back(b);
    };
    addSoft("BLK01", 0.0);
    addSoft("BLK02", 100.0);
    p.outline_w = 200.0;
    p.outline_h = 100.0;
    p.alpha = 1.0;
    Bundle bd;
    bd.source = 0;
    bd.target = 1;
    bd.nets = 100;
    p.bundles.push_back(bd);

    Placement pl;
    pl.rects = {Rect{0.0, 0.0, 100.0, 100.0}, Rect{100.0, 0.0, 100.0, 100.0}};
    pl.aspect = {1.0, 1.0};

    RoutingResult r = routeProblem(p, pl);
    assert(r.open_routes == 0);
    assert(r.routes.size() == 1);
    assert(r.routes[0].node_path.size() == 2);  // direct block->block, no channel
    assert(near(r.routes[0].wirelength, 0.0));  // 1 guiding point -> empty HPWL sum
    assert(near(r.total_wirelength, 0.0));
    std::cout << "testDirectAbutmentZeroWirelength ok\n";
}
```

Call it from `main()`.

- [ ] **Step 2: Run to verify it fails.** Expected: assert fails — current code adds `nets` (100) for 1-guide routes.

- [ ] **Step 3: Implement.** In `src/routing.cpp` delete the line `if (guides.size() == 1) route.wirelength += nets;` (line 186).

- [ ] **Step 4: Run tests.** `bash test/run_p0_regressions.sh` and the four existing `test/run_*.sh` — Expected: all pass.

- [ ] **Step 5: Commit.**

```bash
git add src/routing.cpp test/regression_p0_20260706.cpp
git commit -m "fix: direct block abutment routes have zero wirelength per cost spec"
```

---

