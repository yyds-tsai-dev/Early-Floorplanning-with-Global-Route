### Task 3: adaptive bottleneck chunking (design §Q3)

**Files:** `src/routing.cpp` bundle loop (~209-217).
- [ ] RED: bundle 3000 nets, path channel slack 1000 nets ⇒ chunks split ≈(1000, rest...) filling slack then diverting; kMaxChunks=16 bound; PATH-per-chunk output preserved (io writes one line per route).
- [ ] Implement greedy split-at-bottleneck (new-router path only). GREEN + suites + commit `feat: adaptive bottleneck chunking`.

