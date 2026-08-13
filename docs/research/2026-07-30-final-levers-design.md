# Final 階段兩支槓桿設計 — L6 undirected bundle merge / L2 edge-block 真鬆綁

**日期**: 2026-07-30
**基底**: fable5 @ `e28dcaf`(beta 提交 binary 已凍結)
**上游**: `docs/research/2026-07-30-official-0727-refresh-impact.md` §5-L2 / §5-L6;官方事實見
`docs/official/E_reference_20260727.html`(A50 已升格為規範文件)。
**硬性紀律(B5a 教訓)**: 兩支槓桿一律 **flag-gated**,旗標 OFF 時 **byte-identical**;
既有 regression floor 全數維持綠燈。本文件只做設計,不改任何程式碼。

---

## 0. 兩支槓桿的官方依據(一句話版)

| 槓桿 | 官方條文 | 我們目前的行為 | 差距 |
|---|---|---|---|
| L6 | `CONN_MISMATCH` = **undirected 比對**,`PATH(A→B)+PATH(B→A) = m[A][B]+m[B][A]`,**方向分配不受限**(reference.html:459-460, 641-643) | `io.cpp:187` 每個非零**有向**矩陣元素各建一個 `Bundle` | 同一對 block 的兩向 nets 被迫各自跑一次 Dijkstra,不能共用最佳路徑;bundle 數多 18–50% |
| L2 | `EDGE_LOCATION_VIOLATION` = **WARNING**,γ=1/um 位移距離(reference.html §errors;`route_cost.hpp:50` kGammaEdge) | `placement_engine.cpp:410-425, 798, 1003, 1503-1508` decode 無條件把 edge block 貼回 LOCATION 邊界;SA 拓樸完全不含 edge block(`movableBlocks`, :385-391) | 搜尋**從未見過** edge block 離邊的盤面;G4(`--edge-soft-anchor`, b632e03)只加了沿邊滑移 move(:543-556)+ exact-tier γ,**cheap tier 無 γ 梯度** → 0/5 採用的測量無效 |

---

# LEVER L6 — Undirected bundle merge

## L6.1 決策

**做**,以 **parse 後的純函式轉換 + CLI 旗標** 實作,預設 OFF。

- 新增 `std::vector<Bundle> mergeUndirectedBundles(const std::vector<Bundle>&)`(宣告於 `io.hpp`,
  實作於 `io.cpp`):把每個無序對 `{A,B}` 合併成單一 `Bundle{min(A,B), max(A,B), m[A][B]+m[B][A]}`。
- **不動 `parseProblem`**(`io.cpp:125-197`)。合併發生在 `main.cpp` 解析旗標之後。
- 新增 `Problem.undirected_bundles`(`model.hpp`,預設 `false`,**永不由 CSV 解析**,與
  `route_pricing_official`/`abutment_credit`/`ft_pricing_aggressive`/`edge_soft_anchor` 同一慣例)。

### 為何是「parse 後轉換」而非改 `parseProblem` 簽章

`parseProblem(path)` 沒有 CLI 旗標的視野。改成 `parseProblem(path, bool merge)` 會波及
`tools/rescore.cpp:11`、`tools/*`、以及 **11 個** 直接呼叫它的 regression `.cpp`。
更關鍵:`test/regression_testdata_20260712.cpp:23-25` 直接對 `parseProblem` 產出的 bundles 做斷言
(`b.source != b.target`);post-parse 轉換讓那支測試**完全不受影響**。
`mergeUndirectedBundles` 是純函式 → 可獨立單元測試,零 I/O。

### 正規化方向規則(必須明確,否則不決定性)

```
key = (min(source,target), max(source,target))
nets = Σ over both directed entries
輸出順序 = 依 (source, target) 遞增排序          // 等同上三角 row-major 掃描
```
方向選 `min→max` 而非「先遇到的方向」:後者在 `m[A][B]==0 && m[B][A]>0` 時會依賴掃描順序,
不必要地脆弱。**Q4 已證明方向對合法性/成本完全對稱**(見 L6.5),故任何正規化方向都安全。

## L6.2 `Problem.bundles` 的完整消費者清單與語義變化

| # | 位置 | 用途 | 合併後語義 | 是否不變 |
|---|---|---|---|---|
| 1 | `eval.cpp:166-176` `cheapScore` wire proxy | `Σ nets·manhattan(center_a, center_b)` | `n1·d + n2·d = (n1+n2)·d` | ✅ **完全不變**(對 nets 線性) |
| 2 | `eval.cpp:79-102` `routeAwareProxyPenalty` congestion | `max(0, nets − 25·narrow)·kBetaLin` | `max(0,n1−c)+max(0,n2−c) ≤ max(0,n1+n2−c)` | ❌ 變大。**合併版才是真實的** pair demand(兩向 nets 走同一條實體通道)→ 這是設計上想要的修正 |
| 3 | `eval.cpp:104-115` 同函式的 FT bbox proxy | `ftRequiredArea(base, ftExtend(bundle.nets, tier(bundle.nets)))` 逐 bundle 計 | tier 由 pair 總 nets 決定(例:case1 3273+3273 → 40% 變 80%) | ❌ 變大。同樣是**更貼近**真實 accumulated-nets 語義(見 L6.4 定理) |
| 4 | `eval.cpp:132-144` `channelMarginProxy` | `[[maybe_unused]]`,C1 後已無呼叫者 | 對 nets 線性,理論上不變 | ✅ 死碼,無影響 |
| 5 | `routing.cpp:616-630` `runLegacyPass` | `split_count = min(8, ceil(nets/1000))` | chunk 變少變粗(見 L6.4) | ❌ 變(僅影響 comparison-gate 的 floor leg) |
| 6 | `routing.cpp:649-653` `order` | `stable_sort` by nets 遞減 | 排序鍵變成 pair 的**真實**總需求 → heaviest-first 啟發式終於看到正確權重 | ❌ 變(**方向正向**) |
| 7 | `routing.cpp:719-767` cost-driven chunk 迴圈 | `kMinChunkFor(nets)`, `kMaxChunks=16`, `pathSlack` | 見 L6.4 | ❌ 變 |
| 8 | `placement_engine.cpp:1204-1235` `wireMedianTarget` | 對 `(partner_center, nets)` 取加權中位數 | 兩筆同座標權重相加 = 一筆合併權重 → 加權中位數定義上相同 | ✅ **完全不變**(可證) |
| 9 | `placement_engine.cpp:1236-1252` `netWeightedBundle` | roulette,單次 `uniform_int_distribution<long long>(1,total)` | `total` 不變、**抽樣次數不變**、落在同一 pair 的機率不變;但 index→pair 的映射改變 | ⚠️ 分布等價、**非 byte-identical**(RNG 流走向不同) |
| 10 | `placement_engine.cpp:1254-1277` `worstBlock` | `α·nets·manhattan`(線性 ✅)+ `congestionShare`(非線性 ❌) | 同 #2 | ❌ 部分變 |
| 11 | `placement_engine.cpp:608-623` `pickAbutmentPair` | 由 bundle 決定 `(x, p)` | 正規化方向可能翻轉 x/p 角色(edge 判定規則 :612-621 仍成立) | ❌ 變(良性,僅 moves_v2 legs) |
| 12 | `main.cpp:198` stderr `bundles=` | 純遙測 | 數字下降 18–50% | ✅ 無腳本 grep 此欄(`scripts/sweep.sh:38` 只抓 `iters=`/`open_routes=`/`validation=`)→ 正好是 A/B 的可見簽名 |
| 13 | `validate.cpp` | **完全不讀 bundles**(只走 `routing.routes`, :112-131) | 無變化 | ✅(但見 L6.3 的新增檢查) |
| 14 | `io.cpp:236-241` `writeCfg` PATH | 只寫 `routing.routes`,不讀 bundles | PATH 行數下降 | ✅ |
| 15 | `src/cfg_score.cpp`(golden oracle) | **完全不讀 bundles**(:216-218 只解析 .cfg 的 PATH) | 無變化 | ✅ oracle 不需碰 |
| 16 | 測試 fixture:`test/regression_{p0,p1,p2,p3,b5,b7,ftagg,v5_tail,20260608,20260618,placement_engine}*.cpp` | 手工建 `p.bundles` | 這些 Problem **不設新旗標** → 預設 false → 行為零變化 | ✅ |
| 17 | `test/regression_testdata_20260712.cpp:23-25` | 對 `parseProblem` 產物斷言 | post-parse 轉換不影響 `parseProblem` | ✅ |

**結論**:cheap tier 的 WL 項(#1)與 `wireMedianTarget`(#8)可證明不變;真正改變的是
congestion/FT 的 **cheap proxy**(變準)、bundle **排序**(變準)、以及 **chunking 顆粒**(變粗,唯一風險)。

## L6.3 PATH 輸出與 CONN 帳目

### 官方側
`writeCfg`(`io.cpp:236-241`)寫的是 `PATH <nets> <obj> <edge> <obj> <edge> ...`,**不含方向 metadata**;
判題端只能由 step 鏈的首/尾 object 推得 `(A,B)`。合併後全部 nets 以 `block[min] → block[max]`
單向輸出,`PATH(A→B)+PATH(B→A) = (n1+n2)+0 = m[A][B]+m[B][A]` → **直接滿足 undirected 規則**
(reference.html:459-460)。對角線我們本來就跳過(`io.cpp:186`),合併不會產生 self-pair。

### 我們自己的側 — **驗證結果:validate.cpp 目前完全沒有 CONN 帳目**
`validateSolution`(`validate.cpp:68-132`)只檢查:outline 容納、fixed 尺寸、edge LOCATION、
block overlap、`route.open`、source/target port edge、edge id 範圍。
**沒有任何一行把 `route.nets` 加總回矩陣。** 所以合併「不會被我們的 validator 擋下」,
但也代表 **`CONN_MISMATCH`(官方 ERROR/FAIL)目前零防護**。

### 決策:新增 CONN coverage 檢查(**先於 L6 落地,且旗標無關**)

在 `validate.cpp:112` 的 route 迴圈之後新增:

```
// 依「無序對」聚合,兩邊都用 unordered key → 對 merged / split 兩種 bundle 形態同式
map<pair<int,int>, long long> want, got;     // key = (min,max)
for (b : problem.bundles)      want[key(b.source,b.target)] += b.nets;
for (r : routing.routes) if (!r.open) got[key(r.source,r.target)] += r.nets;
每個 key: want != got  →  fail("conn mismatch: <A> <B> want=.. got=..")
got 有而 want 沒有的 key → fail
```

- **旗標無關**:因為兩側都用無序鍵聚合,`want` 在 merged 與 split 兩種形態下**恆等**
  (`n1+n2` vs `n1`+`n2`),`got` 亦然。同一段程式碼同時守住兩條路徑。
- **今天就該是綠的**:`routing.cpp:719-767` 的 chunk 迴圈保證 `Σ chunk.nets == bundle.nets`
  (`remaining -= n` 直到 0),唯一例外是 open route,而 open route 已被 `:113` 硬擋。
- **落地順序**:先單獨落地此檢查,在 5 案 + 全 regression 上證明綠燈,**再**開始 L6 的合併。
  若意外紅燈 → 那是既有 bug,必須先修;絕不因為要趕 L6 就把它降級成 flag-gated。

## L6.4 Chunking 互動 + FT tier 定理

### (a) FT 不變性定理(Q3 的答案:**證明「不變」**)

> **定理**:固定「每一條 net 的實體路徑」不變,則合併 bundle **不改變** 總 FT required area。

**證明**。FT 只透過三個地方進入成本,三者都只是「每個 soft block 的**累計**穿越 nets 數」的函數:

1. `routing.cpp:435-446` `commitRoutePath`:對 `node_path` 的**內部**節點,
   `rr.soft_feedthrough_nets[node] += nets`(每個 chunk 一次)。
   ⇒ `N_i = Σ_{通過 i 的 chunk} chunk.nets = 通過 i 的 net 總數`,**與 chunk 如何分組無關**。
2. `routing.cpp:594-604` `computeOverflowAndMismatch`:
   `ft_area_sum = Σ_i [ isSoft(i) ? ftRequiredArea(base_i, ftExtend(N_i, ftTierRate(block_i, N_i))) : area_i ]`。
   **只是 `N_i` 的函數**;tier(3000/6000/9000, `ft.hpp:10-16`)與 `ceil`(`ft.hpp:19-21`)
   都只施加於**最終總量 `N_i`**,不是每個 chunk。
3. `cfg_score.cpp:264-281`(golden oracle)從寫出的 .cfg 逐 PATH 累加 `p.nets` 到 block,
   然後 **一次** 轉面積 — 與 (2) 同構。

`route_cost.hpp:200-205` 的 `ftRequiredDelta` 是 telescoping 的:
`Σ_k [R(a_k+n_k) − R(a_k)]` 且 `a_{k+1}=a_k+n_k, a_0=0` ⇒ `= R(N_i) − R(0)`,
與 chunk 切法無關(`ceil`/`round2` 的非線性不影響求和,因為它們只在真實累計點上被求值)。
`routing.cpp:594` 的 `rr.ft_area_sum = 0.0` 重算又獨立地覆寫一次,雙保險。

而端點的 soft block **永不計 FT**(`:435` 迴圈 `i ∈ [1, size-2]`),合併不改變誰是端點。∎

> **反面(必須同時說清楚)**:定理的前提是「路徑選擇相同」。合併**會**改變路徑選擇,
> 因為 (i) Dijkstra 的 FT 邊價 `ftRequiredDelta(block, acc, nets)`(`routing.cpp:286`)以
> `nets = remaining`(bundle 尺度)定價;(ii) `pathSlack`(`:494-506`)在 tier 邊界切 chunk;
> (iii) bundle 排序改變。所以**實測 FT 會變**,但那是搜尋軌跡的變化,不是會計口徑的變化。

**額外收穫(定價變準,可證)**:split 時 bundle1 只看到 `R(3273)−R(0)`(tier 40%),
bundle2 才看到 `R(6546)−R(3273)`(tier 80%);merged 一次看到 `R(6546)−R(0)`。
兩者**總和相同**,但 merged 在**第一次決策時就看見完整代價** → 近視偏誤消失。

### (b) Chunk 數量/顆粒

實測(新 CSV,`test/E_testcase_20260727/testcase-upload-20260727/`):

| case | 有向非零(off-diag) | 無序對 | 互惠對 | bundle 減量 | 最大合併 nets |
|---|---|---|---|---|---|
| 0 | 11 | 9 | 2 | 18.2% | 1,500 |
| 1 | 82 | 41 | 41 | **50.0%** | 6,546 |
| 2 | 74 | 43 | 31 | 41.9% | **8,049** |
| 3 | 82 | 58 | 24 | 29.3% | 7,003 |
| 4 | 51 | 28 | 23 | 45.1% | 7,552 |

- `kMaxChunks = 16`(`route_cost.hpp:382`)是 **per-bundle** 上限。互惠對合併後,該 pair 的
  chunk 預算從 32 掉到 16 → 一旦撞到 `last_slot`(`routing.cpp:736-739`)會把**全部剩餘 nets
  一次倒進單一路徑**,可能引爆 overflow。**這是 L6 唯一的實質風險。**
- `kMinChunkFor(nets) = max(1, nets/64)`(`route_cost.hpp:395`)隨 bundle nets 放大
  (3273/64=51 → 6546/64=102)。這**是對的**:下限本來就該相對於 bundle 規模,維持不動。

**決策**:在旗標內把上限還原:
```cpp
// routing.cpp:727 附近
const int max_chunks = problem.undirected_bundles ? 2 * kMaxChunks : kMaxChunks;
```
一個 `if`,旗標 OFF 時 byte-identical,旗標 ON 時精確恢復合併前的 per-pair chunk 預算。
`runLegacyPass` 的 `min(8, ...)`(`routing.cpp:619`)**維持不動** — 它只是 comparison gate 的
floor leg,顆粒變粗只會讓 floor 略弱,不會讓結果變差(`routing.cpp:887` 取兩者較優)。

## L6.5 Port edge 合法性(Q4 的答案:**完全對稱,不需選方向也不需拆分**)

三個謂詞逐一檢查:

1. **可達性** `allowed(node, source, target, allow_soft)`(`routing.cpp:94-98`):
   `node==source || node==target` 一律放行,channel 放行,soft 視 `allow_soft`。
   → 對 `(source,target)` 對稱。
2. **Port edge** `endpointEdgeAllowed(u, v, source, target)`(`routing.cpp:100-108`):
   ```
   if (u 是 source 或 target)  需要 portAllows(blocks[u], edgeFromTo(rects[u], rects[v]))
   if (v 是 source 或 target)  需要 portAllows(blocks[v], edgeFromTo(rects[v], rects[u]))
   ```
   **注意它不區分「source 的 port 集」與「target 的 port 集」** — 兩端各自用自己的 port 清單。
   對路徑 `P = (A, v1, …, v_{k-1}, B)`,整條路徑上非平凡的檢查恰好是
   `portAllows(A, edgeFromTo(A,v1)) ∧ portAllows(B, edgeFromTo(B,v_{k-1}))`
   (source/target 不可能是最短路的內部節點)。此合取式**在反轉 P 並交換 (source,target) 下不變**。
3. **邊成本**(`routing.cpp:241-377`):
   - WL 項 `wireCost(α, nets, manhattan(center_u, center_v))` — 對稱。
   - FT 項只在 `v` 是 soft **且 `v != target`** 時計(`:251`)。路徑 `A→S1→S2→B` 正向計 S1,S2;
     反向 `B→S2→S1→A` 同樣計 S2,S1。對稱。
   - Channel 項:`u` 側受 `u_charge_suppressed`(`:339-343`,同軸不重複計)、`v` 側恆計。
     淨效果 = **每個 channel 節點,每個被觸及的軸各計一次** — 與遍歷方向無關。
     這與 `route_geometry.hpp:125-135` `accumulatePathUsage` 的口徑完全一致(同樣對稱)。
   - `marginalOverflowNets`(`route_cost.hpp:144-147`)讀的是**搜尋期間不變**的 `usage`,無方向性。

**⟹ 定理**:`path P 對 A→B 合法 ⟺ reverse(P) 對 B→A 合法`,且兩者成本相同。

**推論**:
- 不存在「非對稱 port 集需要選方向或拆分」的情形。任意正規化方向都安全,選 `min→max`。
- 合併**永不**製造新的 open route,也**永不**修好原本的 open route(可行路徑集完全相同)。
- `validate.cpp:115-126` 檢查 `portAllows(source, steps.front().edge)` 與
  `portAllows(target, steps.back().edge)` — 由 router 建構上保證滿足。
- 官方 **根本沒有 PORT EDGE 的 error code**(refresh doc §4)→ 我們是「比 checker 嚴」;
  合併維持同樣的嚴格度,不會放寬。
- **唯一可觀察差異**:`open_routes` 的**計數**在兩種旗標狀態下不可比(bundle 數不同)。
  A/B 時比較 `open_routes > 0` 的布林值,不要比數字。

### case2 的實例佐證
`BLK09` port=3(右)、`BLK10` port=1(左)、`BLK13` port=3、`BLK14` port=2、`BLK15` port=1、
`BLK16` port=4;`BLK01`–`BLK08`(SOFT)port 欄空 → `portAllows` 全開(`routing.cpp:29`)。
`m[01][09]=300, m[09][01]=571` 是典型的「一端受限、一端自由」非對稱 pair —
在上述對稱性下合併完全安全。

## L6.6 旗標接線

| 位置 | 變更 |
|---|---|
| `model.hpp`(接在 `edge_soft_anchor`, :121 之後) | `bool undirected_bundles = false;` + 與現有四個旗標同格式的 doc comment(強調「永不由 CSV 解析」) |
| `io.hpp` / `io.cpp` | 新增 `std::vector<Bundle> mergeUndirectedBundles(const std::vector<Bundle>&);`(純函式)。**`parseProblem` 一字不動。** |
| `main.cpp:142` 之後 | ```problem.undirected_bundles = hasFlag(argc, argv, "--undirected-bundles"); if (problem.undirected_bundles) problem.bundles = mergeUndirectedBundles(problem.bundles);``` — 隱藏旗標,**不進 usage banner**(與 G4 `--edge-soft-anchor` 同慣例,`main.cpp:137-142`) |
| `routing.cpp:727` | `max_chunks` 旗標化(L6.4b) |
| `validate.cpp:131` 之前 | CONN coverage 檢查(旗標無關,先行落地) |
| `search.hpp` `SearchVariant` | **不加**。這是量測旗標,不是 registry leg — 進 registry 會把 600s 預算再切一半,證明有效再說 |

### A/B 方法
`scripts/benchmark.sh:30` 已把 `"$@"` 透傳給 `bin/efgr`,所以
`scripts/benchmark.sh --undirected-bundles --iters 1000` 直接可用。
**但兩支腳本都硬編 `test/2026 ICCAD_case.case$i.csv`**(`benchmark.sh:22`、`sweep.sh:31`),
新 CSV 需要一個 `CASE_DIR` env 旋鈕(`sweep.sh:17-22` 已有 `ENGINES/BUDGETS/SEEDS/CASES/REF_ITERS/OUT`,
加第七個即可)。這是 L6 落地時順手加的**唯一**腳本改動,不影響預設行為。

## L6.7 測試計畫

**旗標 OFF 的 byte-identity gate(最高優先)**
落地前先抓基準:5 案 × `--engine {bstar,hybrid}` × `--iters 200 --seed 1` 的 `.cfg` md5。
落地後必須完全相同。

**對 bundle 身分/順序敏感的既有 suite**(旗標 ON 時預期會動,OFF 時必須不動):
`run_p2` `run_p3`(chunking/negotiated routing)、`run_b5`(`netWeightedBundle`/`wireMedianTarget`)、
`run_b7`、`run_ftagg`、`run_v5_tail_pricing`、`run_20260618`、`run_testdata`。
**不敏感但必須全綠**:`run_cfg_score`(golden)、`run_selfconsistency`、`run_case00`、`run_integer_output`。

**新增 `test/regression_undirected_20260730.cpp` + `test/run_undirected_regressions.sh`**:

| # | 測項 | 斷言 |
|---|---|---|
| U1 | `mergeUndirectedBundles` 純函式 | `{A→B:300, B→A:571, A→C:5}` → `{(A,B,871),(A,C,5)}`;輸出依 `(source,target)` 遞增;`source < target` 恆成立;空輸入 → 空輸出;**冪等**(對已合併清單再合併 = 恆等) |
| U2 | Σnets 保存 | 5 個新 CSV:`Σ merged.nets == Σ parsed.nets`,且逐無序對總和相符 |
| U3 | **對稱對照**(case1 矩陣恰對稱) | merged bundle 數 == 41 == 82/2;每個 merged bundle 的 nets == 對應有向 nets × 2 |
| U4 | **非對稱 port pair** | 手工 Problem:A(`port_edges={3}`)左、B(`port_edges={1}`)右相鄰,`A→B:100`+`B→A:50`。合併後 `routeProblem` → `open_routes==0`、路由 nets 總計 150、`validateSolution` 綠 |
| U5 | **port 不可行的負向孿生** | 同 U4 但 A 的 port 改成 `{1}`(背對 B)→ 合併與未合併**都** open。證明「合併既不救也不壞 port 可行性」 |
| U6 | **FT 不變性定理**(L6.4a) | fixture:A—S(soft)—B 唯一通路,`A→B:2000`+`B→A:2000`。split 與 merged 兩次 `routeProblem`:`soft_feedthrough_nets[S] == 4000` 且 `ft_area_sum` 相等 |
| U7 | **CONN sum on written cfg** | 對 case2 跑 solver(開/關旗標各一次),用 `parseCfg`(`cfg_score.hpp`)讀回 `.cfg`,逐 PATH 以首/尾 object 聚合成無序對,斷言等於 `m[A][B]+m[B][A]`;未連線的 pair 必須為 0 |
| U8 | CONN validator | 手工 RoutingResult 少送 1 net → `validateSolution` 回 `ok=false` 且訊息含 `conn mismatch` |

## L6.8 風險

| # | 風險 | 嚴重度 | 緩解 |
|---|---|---|---|
| R1 | 合併後 chunk 預算減半 → `last_slot` 一次倒完 → overflow 爆衝 | 中 | L6.4b 的 `max_chunks` 旗標化;A/B 時盯 `channel_overflow_nets` |
| R2 | `open_routes` 計數跨旗標不可比 | 低 | 比布林,不比數字(已寫進測試計畫) |
| R3 | cheap tier congestion proxy 變大 → SA 傾向拆開重互惠對 → 可能反噬 OA | 中 | A/B 時同時盯 `outline_area`;若 OA 退步而 WL 進步,考慮把 #2/#3 的 proxy 也旗標化(但**先量測再決定**,不要預先加旗標) |
| R4 | bundle 排序改變 → 整條 SA/routing 軌跡改變,「應該更好」不成立 | 高(方法論) | 硬紀律:只以實測 checker_cost 判定;B2 fold(`search.hpp:389-400`)保證 portfolio 不劣化,但這是**獨立旗標**,不在 fold 內 → 必須跑完整 A/B |
| R5 | 主辦方日後改判 CONN 為 directed | 低 | reference.html:459-460 與 :641-643 兩處獨立寫明 undirected + 「方向分配不受限」,且 A50 已升格規範。**beta 維持 OFF**;final 開啟前再確認一次 |

---

# LEVER L2 — Edge block 真・鬆綁(edge-free mode)

## L2.1 為何 G4 的測量無效(根因,兩層)

1. **拓樸層**:`movableBlocks`(`placement_engine.cpp:385-391`)排除 edge block,
   兩個 decode 都在開頭把 `placed[i] = isEdge(...)`(`:798`, `:1003`)標成已放置,
   core block 是**繞著釘死的 edge 位置**做 contour packing;
   `basePlacement`(`:410-426`)先 `placeEdge` 再 `legalizeEdgeBlocks`。
   G4 的 `applyEdgeSoftAnchor`(`:358-371`)在 `shrinkWrapOutline` **之後**才平移 —
   core 早就繞著舊位置排好了,平移只能製造 overlap 或浪費空間,**永遠不可能縮小 outline**。
2. **梯度層**:`cheapScore`(`eval.cpp:148-181`)**沒有 γ 項**;γ 只在
   `routeAndScoreAt`(`eval.cpp:234-242`)的 exact tier 計。SA 絕大多數迭代只跑 cheap tier
   (`--exact-interval 50`)→ 對 LOCATION 違規的代價**完全無感**,只感受到 overlap 1e9 的懲罰。
3. **擦除層**:`commitOutlineAt`(`:1502-1509`)、`anchorAtOutline`(`:1666-1672`)、
   `shrinkWrapOutline` 的 `feasible`(`:1635-1639`)每次都用 `dimensionFor` 重設 edge rect
   並 `legalizeEdgeBlocks` → G4 的位移在 `outlineSweep`/`compactTowardOrigin`/corrective grow
   路徑上被反覆抹掉(`:352-357` 自己的 KNOWN GAP 註記只承認了其中一條)。

## L2.2 決策:**表徵選「進拓樸」,模式選「全有全無」**

### 選項比較

| 方案 | 描述 | OA 能不能降? | blast radius |
|---|---|---|---|
| A. 自由浮動 rect + (x,y) offset mutation + overlap 罰則 | G4 的加強版 | **不能**。`lowerBoundNeeds`(`:1387-1406`)把 edge block 當獨立軸向下界、`placementLegalAt`(`:1471-1474`)要求每個 edge block `edgeRectLegal`、core 仍繞著舊位置 pack | 小但**無效** |
| B. per-block detach bit,edge block 可選擇性進拓樸 | 混合 | 能 | 大(每個站點都要查 per-block 位),且 `edgeBlocksFeasibleAt` 的 `collidesWithPlaced` 對 detached 塊有語義破口 |
| **C(選定). 全有全無 `edge_free`:旗標 ON 時 edge block 一律等同 MACRO(固定尺寸、進拓樸、無 LOCATION 錨定)** | 單一謂詞 | **能** | 中(24 處機械替換),但每處都是同一個謂詞,零判斷 |

**選 C 的量化理由 — γ 便宜到近乎免費**:
`kGammaEdge = 1.0`/um(`route_cost.hpp:50`)。case2 的 BLK09 是 `LT,TL` 雙側(AND 語義,
`route_cost.hpp:327-339`),完全脫離左上角到 outline 中心的違規距離 ≈ `|Δx|+|Δy| ≈ 2600+2600 = 5200`
→ 成本 5200。八個 edge block 全部脫離 ≈ **3 萬**,對照 case2 的 `OA ≈ 27,140,000`
→ **0.11% 的 OA**。四塊 300×1305 + 四塊小塊共 1.80M 面積(全案 block 面積 ~19.2M)從邊界釋放,
只要換到 >0.11% 的 whitespace 改善就贏。再者 BLK09–BLK12 各對 BLK01/BLK02 有 ~1370 nets 卻被釘在
**四個不同角落** → WL 也有明顯改善空間(case2 α=0.1、WL ≈ 93M)。

**選 C 也順帶回答了「move set」**:**不新增任何 move**。edge block 進了
`movableBlocks` 之後,既有的 SP swap / B\* `moveLeaf` / `mutateWireMedian` / `mutateAbutment` /
`mutateGreedyReposition`(`:696-754`, `:933-992`)自動全部適用。

**C 的誠實限制**:contour decode 是左下角壓縮,沒有「回貼邊界」的表達方式 → SA **無法回到
γ=0 的錨定盆地**。緩解:`--edge-free` 是**獨立的一次 run**,不進 `defaultVariants()`
(`search.hpp:364`);錨定版本永遠是另一次 run,兩者取優 → 結構上不可能比現況差。

## L2.3 精確編輯站點

### (a) 新增謂詞(`model.hpp`,緊接 `isEdge`, :195)

```cpp
// 「這個 block 被釘在 outline 邊界上、不屬於自由幾何」——
// 旗標 OFF 時恆等於 isEdge(blocks[i].type),故 OFF 路徑 byte-identical。
inline bool isAnchoredEdge(const Problem& p, std::size_t i) {
    return isEdge(p.blocks[i].type) && !p.edge_free;
}
```
外加 `bool edge_free = false;`(同 `undirected_bundles` 的 doc-comment 慣例)。

### (b) 機械替換清單(`isEdge(problem.blocks[i].type)` → `isAnchoredEdge(problem, i)`)

| 檔案:行 | 函式 | 為何要換 |
|---|---|---|
| `placement_engine.cpp:318` | `legalizeEdgeBlocks` | 不再蒐集 freed block → 不再重貼邊 |
| `placement_engine.cpp:388` | `movableBlocks` | **核心**:freed edge block 進 SP/B\* 拓樸。連帶讓 `initialState`/`seedFromPlacement`/`isValidState`(`:670-694, :818-821, :897-931, :1038-1054`)自動跟上 |
| `placement_engine.cpp:419` | `basePlacement` | 不再呼叫 `placeEdge`(也就避開 `:178` 的 `assert(edgeRectLegal(...))`,Makefile 無 `-DNDEBUG`,assert 是活的) |
| `placement_engine.cpp:798` | SP `decode` `placed[]` 預標 | freed block 由 contour 放置,不能預標已放置 |
| `placement_engine.cpp:1003` | B\* `decode` `placed[]` 預標 | 同上 |
| `placement_engine.cpp:1394` | `lowerBoundNeeds` | **核心**:freed block 改走 core 分支(`w_core = max(r.right())`)→ outline 下界變成真實 bbox |
| `placement_engine.cpp:1419, 1435` | `edgeBlocksFeasibleAt` | 全 freed 時 `edges` 為空 → `:1421` 直接 `return true`,函式乾淨退化 |
| `placement_engine.cpp:1472` | `placementLegalAt` | **核心**:不再要求 `edgeRectLegal` → shrink-wrap 可以壓到「edge block 必須貼邊」的前緣之下 |
| `placement_engine.cpp:1503, 1508` | `commitOutlineAt` | **核心**:不再 `dimensionFor` 重設 + 回寫 edge rect → 位置不再被擦除 |
| `placement_engine.cpp:1636` | `shrinkWrapOutline` 的 `feasible` | 同上,mid-bisection 版本 |
| `placement_engine.cpp:612-613` | `pickAbutmentPair` | freed edge 已是可移動塊;`source_edge && target_edge → return false`(:614)在 edge-free 下應改為可處理 |
| `search.cpp:40` | `compactTowardOrigin` | freed block 也要被壓向原點,否則壓縮後留洞 |
| `respace.cpp:193` | `relaxAxis` 的 `core` 集合 | freed block 要參與鬆弛,否則 core 被推進 freed block → overlap → respace 結果被 adoption gate 丟棄(可惜但不致錯) |
| `respace.cpp:262` | `axisSpan` | span 要含 freed block |
| `respace.cpp:297, 305` | `relaxOneAxis` 的 clamp/回寫 | 同上 |
| `respace.cpp:565, 566, 567, 570` | capacity-gap 候選列舉 | 原本因「edge 釘死不可實現」而排除;freed 後可實現。**落地時須順帶確認 `applyGapCandidate` 內部沒有其他 edge 假設** |

**共 24 處**,全部是同一個謂詞替換 → 適合交給 `fast-worker` 執行,但每一處都要在 diff review 時對照本表。

### (c) 保持 `isEdge` **不動** 的站點(語義是「型別是 EDGE」,不是「被釘住」)

- `validate.cpp:92`(LOCATION 檢查;`edge_soft_anchor` 已把它降級為 warning,`:101-105`)
- `eval.cpp:237`(γ 計費迴圈,必須依型別)
- `src/cfg_score.cpp:401`(**golden oracle,絕對不能碰**)
- `src/floorplan.cpp:*`(legacy,只有 `test/regression_20260608.cpp` 連結)
- `placement_engine.cpp:508`(`mutateEdgeChoice`,edge-free 下無意義但無害)
- `placement_engine.cpp:363, 546`(G4 的 `applyEdgeSoftAnchor` / `mutateEdgeAnchorShift`,見 (e))

### (d) cheap tier 的 γ 項(**沒有這個就重蹈 G4 覆轍**)

1. 在 `route_cost.hpp:307` 新增一個 `(rect, outline_w, outline_h, locs)` 的 overload,
   現有 `(rect, const Problem&, locs)` 版轉呼叫它(底層 `edgeLocSatisfied`/`edgeLocDelta`
   本來就只吃 `(w,h)`,`:250-286`)。**目的:避免在 `cheapScore` 熱路徑上複製整個 `Problem`。**
2. `eval.cpp:148-181` `cheapScore`:在解出 `w,h`(`:150-152`)之後新增
   ```cpp
   if (problem.edge_soft_anchor) {
       double g = 0.0;
       for (i : blocks) if (isEdge(blocks[i].type))
           g += edgeLocationViolationDistance(placement.rects[i], w, h, blocks[i].locations);
       penalty += kGammaEdge * g;
   }
   ```
3. **同時刪除** `eval.cpp:234-242` 在 `routeAndScoreAt` 裡那段 γ block。

**為何刪除是等價的(必須證明,否則 exact tier 會雙重計費)**:
`routeAndScoreAt(problem, placement, w, h, ...)` 在 `:190-192` 先把 `scoped.outline_w/h = (w,h)`,
再於 `:194` 呼叫 `cheapScore(scoped, placement)`。而在**每一條**呼叫路徑上
`placement.outline_w == w`:
- stamped 路徑:`exactScore:260-261` 直接取 `local.outline_w`;
- unset 路徑:`shrinkWrapOutline`(`:263`)自己 stamp `local.outline_w = fit.w`;
- grow 路徑:`anchorAtOutline`(`:293`)stamp `regrow.outline_w = grown_w`。
⇒ `cheapScore` 解出的 `(w,h)` 就是 `routeAndScoreAt` 的 `(w,h)`,γ 的 scope 完全相同,
數值逐位元相等。`:205` 只扣 `routeAwareProxyPenalty`,不會誤扣 γ。
旗標 OFF 時兩段都不執行 → byte-identical。

> **副作用(刻意的)**:這同時修好了 G4 `--edge-soft-anchor` 自己的梯度缺陷 —
> 該旗標的「0/5 採用」結論作廢,需重測。這正是 refresh doc §5-L2 的訴求。

### (e) 與 G4 的關係

- `--edge-free` 在 `main.cpp` **同時設** `edge_free = true` 與 `edge_soft_anchor = true`。
  這樣就直接複用既有的 exact-tier γ(→ 現在移到 cheap tier)與 `validate.cpp:101-105` 的
  warning 降級,**eval/validate 不需要為 L2 再多做任何事**。
- 兩個 engine 的 G4 move gate(`:701`, `:938`)改為 `if (problem.edge_soft_anchor && !problem.edge_free)`。
  理由:edge-free 下 rect 由拓樸決定,再做 post-hoc 平移只會製造無人修復的 overlap。
  `applyEdgeSoftAnchor`(`:358`)加一行 `if (problem.edge_free) return;` 作為防禦(邏輯上冗餘,
  因為 move 不觸發時 `edge_anchor_dx/dy` 全零)。

## L2.4 互動逐項裁決

| 互動 | 裁決 | 證據 |
|---|---|---|
| **channel 抽取** | 無需改動。`extractChannels`(`routing.cpp:34-70`)只按 rect 邊界切片,對 block 型別無感。且 shrink-wrap 後 outline == 緊 bbox,contour 保證最左塊在 x=0、最下塊在 y=0 → 不會產生退化的「整條邊界 channel」 | `routing.cpp:34-70` |
| **outline bbox 計算** | 這正是收益來源。`lowerBoundNeeds:1394` 換謂詞後 `w_lb = max(r.right())`;`feasible`(`:1629-1639`)退化成「容納 + 不重疊」→ `bisectShrinkWrap:1532` 第一次探測就成功 → 直接回傳緊 bbox(還省掉整個二分搜尋) | `placement_engine.cpp:1387-1406, 1526-1602, 1619-1664` |
| **respacing / P0–P3 margins** | 需 8 處謂詞替換(表 (b))。未替換的後果只是「respace 產出被 adoption gate 丟棄」,不是正確性破口 | `respace.cpp:193, 262, 297, 305, 565-571` |
| **PORT EDGE 語義** | **完全不變**。`portAllows`(`routing.cpp:25-30`)只問「從 block 的哪一面出線」,與 block 位置無關 | — |
| **⚠️ 新風險:port 死路** | 釘在左邊界且 `port_edge=3`(向內)的 block,釋放後右面可能貼到 outline 牆 → 該 block 完全不可達 → open route。cheap tier 對 port 無感,只有 exact tier 的 `open_routes * 1e15`(`eval.cpp:211`)會擋 | case2 BLK09/11 port=3、BLK10/12 port=1 |
| **validate soft mode + exit code** | `--edge-free ⇒ edge_soft_anchor` ⇒ `validate.cpp:101-105` 走 warning 分支 ⇒ `vr.ok` 維持 true ⇒ `main.cpp:210` 的 `return validation.ok && open_routes==0 ? 0 : 1` 給 exit 0。**量測 harness 可用** | `validate.cpp:92-107`, `main.cpp:210` |
| **`placeEdge` 的 assert** | `:178` 的 `assert(edgeRectLegal(...))` 在 edge-free 下不可達:唯二呼叫者 `basePlacement:420`(已 gate)與 `firstLegalEdgeSlot:249,312`(只被 `legalizeEdgeBlocks:332` / `edgeBlocksFeasibleAt:1439` 呼叫,兩者都已 gate) | Makefile 無 `-DNDEBUG`,assert 是活的 |
| **`dimensionFor`** | `!isSoft` 直接回 `(block.width, block.height)`(`:105`)→ freed edge block 尺寸不變,`validate.cpp:87-91` 的 fixed-dimension 檢查恆綠 | `placement_engine.cpp:98-115` |
| **exactScore corrective grow** | `anchorAtOutline`(`eval.cpp:293`)不再擦除幾何 → `:352-357` 記載的 KNOWN GAP 自然消失 | — |

## L2.5 Move set

**新增 0 個 move。** 決策與理由:

| 候選 | 裁決 | 理由 |
|---|---|---|
| 2D anchor offset(G4 式) | **移除**(edge-free 下停用) | 在拓樸決定位置之後平移 = 無修復的 overlap |
| swap into topology | **自動獲得** | `movableBlocks` 一換,SP swap(`:713-718`)/ B\* node-swap(`:951-955`)/ `moveLeaf`(`:1069-`)全部適用 |
| detach / re-attach(回貼邊) | **不做** | contour decode 無法表達「在座標 (x,y)」;真要回貼只能新增一個「以 `firstLegalEdgeSlot` 覆寫」的 move,那會重新引入被 shrink-wrap 擦除的問題。改用「跑兩次 run 取優」的結構解 |
| wire-median / abutment / greedy-reposition | **自動獲得**(moves_v2 legs) | `:838-888`, `:965-991`;`pickAbutmentPair` 需一併換謂詞(表 (b)) |

**決定性**:不新增任何 RNG 呼叫 → 沒有新的決定性風險面。但 `movableBlocks` 的大小改變會讓
`uniform_int_distribution(0, size-1)` 的值域改變 → **RNG 流必然與旗標 OFF 分岔**。這是變體的本質,
不是 bug;所有測試仍以固定 `--seed` 保證同旗標下可重現。

## L2.6 量測計畫

環境:WSL native shell,`/home/yyds/Early-Floorplanning-with-Global-Route`。

```bash
D="test/E_testcase_20260727/testcase-upload-20260727"
for c in 2 3; do
  CSV="$D/2026 ICCAD_case.case$c.csv"
  bin/efgr "$CSV" /tmp/c${c}_anchor.cfg --seed 1 --time-budget 600 2> /tmp/c${c}_anchor.log
  bin/efgr "$CSV" /tmp/c${c}_free.cfg   --seed 1 --time-budget 600 --edge-free 2> /tmp/c${c}_free.log
  echo "case$c anchor: $(bin/efgr_rescore "$CSV" /tmp/c${c}_anchor.cfg)"
  echo "case$c free  : $(bin/efgr_rescore "$CSV" /tmp/c${c}_free.cfg)"
done
```
(`--time-budget 600` 顯式給,語義等同官方 harness 的裸呼叫 600s,`main.cpp:112-120`;
`--seed 1` 是預設值,顯式寫出以求可重現。)

`bin/efgr_rescore`(`tools/rescore.cpp:16-19`)已直接輸出所需全部欄位:
`area / wl / overflow / rate / overflow_penalty / ft_penalty / edge_location_penalty / checker_cost`。
**γ 由 oracle 無條件計費**(`cfg_score.cpp:395-407`)→ 量測不受我們的旗標污染。

**回報表格**(每案兩列):`OA | α·WL | overflow_penalty | ft_penalty | edge_location_penalty | checker_cost | open_routes | iters`
(`iters=` 從 stderr 取,用來量化 L2.7-R1 的搜尋空間成長代價)。

**成功判準(全部必須成立)**:
1. `checker_cost(free) < checker_cost(anchor)`,**case2 與 case3 皆然**;
2. `open_routes == 0` 且 `validation=ok`(允許 warning 行);
3. 分解上必須是 `Δarea + α·Δwl` 的節省 **大於** `Δedge_location_penalty` 的增加 —
   若 checker_cost 贏但 γ 幾乎為 0,代表拓樸自己把 edge block 排回了邊界,槓桿等於沒作用,需檢查接線。

**失敗/中止判準**:任一案出現 open routes → 是 L2.4 的 port 死路風險成真,
必須先做 port-aware 的 move 改良(或把 `mutateAbutment` 的 port 檢查提前到 cheap tier)再重測,
**不得**用「多跑幾個 seed」掩蓋。

## L2.7 風險

| # | 風險 | 嚴重度 | 緩解 / 說明 |
|---|---|---|---|
| R1 | **搜尋空間成長 vs 固定 600s**。case2 可移動塊 8 → 16。SP decode 是 **O(n³)**(`:773-788` 的 O(n²) 約束建圖 × `:792` 的 `blocks.size()` 次鬆弛)→ decode 成本約 ×8;B\* contour 是 O(n²) → ×4 | **高** | 必須把 `iters=` 一併回報。若迭代數掉超過 3×,需考慮把 SP 的鬆弛迴圈改成拓樸排序(獨立優化,不屬本槓桿),或先只在 B\* engine 上量測 |
| R2 | 錨定盆地不可回 → free run 可能單方面更差 | 中 | 結構解:獨立 run,不進 `defaultVariants()`。取兩次 run 的較優,不可能比現況差 |
| R3 | port 死路 → open routes | 中 | 見 L2.6 中止判準 |
| R4 | **spec-vs-checker 反轉**:spec 原文寫 hard constraint,reference 寫 WARNING+γ。若 final 判回 hard,每個離位 edge block = FAIL = `M=1.8 × worst_cost`(case2 ≈ 63M) | **高** | **本槓桿在主辦方書面確認前只做量測,絕不翻預設值。** 旗標隱藏、不進 usage banner、不進 registry。同時向主辦方直接提問 |
| R5 | respace / compaction 開始移動 edge block → 新的 overlap/legality 路徑 | 低 | 既有 adoption gate(`search.cpp:84-94` `fitsOutlineWithoutOverlap`、`placementLegalAt`)會拒絕不合法結果,是「錯失機會」而非「產出非法」 |
| R6 | 24 處機械替換漏改一處 | 中 | 用 L2.3(b) 表逐項對照 diff;並加一支「`grep -n 'isEdge(problem.blocks' src/{placement_engine,search,respace}.cpp` 應只剩白名單行」的 CI 式斷言到 review checklist |

## L2.8 測試計畫

**旗標 OFF byte-identity**:同 L6.7 的 md5 gate(這是 L2 唯一的硬性接受條件)。

**新增 `test/regression_edgefree_20260730.cpp` + `test/run_edgefree_regressions.sh`**:

| # | 測項 | 斷言 |
|---|---|---|
| E1 | `movableBlocks` | `edge_free=false` → 不含 edge;`true` → 含全部 block |
| E2 | decode 真的離邊 | fixture:outline 遠大於 block 總面積、一個 `T` 型 edge block。`edge_free=true` decode 後該 block **不** `satisfiesEdgeLocations`,但 `placementLegalAt`(容納+不重疊)為真 |
| E3 | shrink-wrap 退化 | `edge_free=true` 時 `shrinkWrapOutline` 回傳的 `(w,h)` == `ceil(緊 bbox)`,且**沒有任何 rect 被移動** |
| E4 | `commitOutlineAt` 不擦除 | `edge_free=true` 呼叫後所有 `placement.rects` 逐位元不變,只有 `outline_w/h` 被 stamp |
| E5 | **cheap tier γ 梯度存在**(G4 缺的那一塊) | 兩個只差一個 edge block 位置的 placement:`cheapScore` 差值 == `kGammaEdge × Δ edgeLocationViolationDistance`(其他項用相同幾何抵銷)。**這是本槓桿最關鍵的一支測試** |
| E6 | exact tier γ 未雙計 | 手工 fixture,`edge_soft_anchor=true`:`exactScore.value` 等於 `A + α·WL + overflow + ft + kGammaEdge·Σdist`,逐項對照(證明 (d) 的搬移是等價的) |
| E7 | validate soft mode | 離位 edge block + `edge_soft_anchor=true` → `vr.ok == true` 且訊息含 `warning: edge location violation` |
| E8 | G4 move 在 edge-free 下停用 | `edge_free=true` 時 `mutate` 不呼叫 `mutateEdgeAnchorShift`(以 `edge_anchor_dx` 全零 + 固定 seed 下 RNG 消耗數斷言) |

**golden**:`run_cfg_score_regressions.sh` 不受影響(oracle 未動)。
**自洽**:`run_selfconsistency_regressions.sh` 在 `--edge-free` 下**也要跑一次** —
solver score 必須等於 rescorer 的 `checker_cost`,這是 γ 搬移是否正確的端到端證明。

---

# 合併實作順序(單一 worktree)

**為何 L6 先、L2 後、且同一 worktree**:兩者只在 `main.cpp`(旗標解析)與 `model.hpp`(旗標欄位)
重疊,循序做可完全避開衝突;L6 風險較低,且它附帶的 CONN 檢查是一張對 L2 也無害的安全網;
若 L2 的 24 處機械替換出事,L6 已經綠燈落地。

| 步驟 | 內容 | 驗收 |
|---|---|---|
| **0** | 抓 byte-identity 基準 md5(見下方指令 §B) | 存檔 |
| **L6-1** | `mergeUndirectedBundles`(`io.hpp`/`io.cpp`)+ U1/U2/U3 單元測試。**不接線** | 新測試綠 + 全 regression 綠 |
| **L6-2** | `Problem.undirected_bundles`(`model.hpp`)+ `main.cpp` 旗標與 post-parse 轉換 | md5 identity gate 通過 |
| **L6-3** | `routing.cpp:727` 的 `max_chunks` 旗標化 | md5 identity gate 通過 |
| **L6-4** | `validate.cpp` CONN coverage 檢查(**旗標無關**)+ U8 | **先驗證 5 案全綠**,再往下走 |
| **L6-5** | U4/U5(port)、U6(FT 定理)、U7(cfg CONN sum) | 新測試綠 |
| **L6-6** | `scripts/{benchmark,sweep}.sh` 加 `CASE_DIR` env 旋鈕(預設值 = 現行硬編路徑) | 預設行為不變 |
| **L6-7** | 全 regression + md5 gate + A/B 量測 | 記錄於 refresh doc §9 |
| **L2-1** | `Problem.edge_free` + `isAnchoredEdge`(`model.hpp`) | 編譯乾淨 |
| **L2-2** | `route_cost.hpp` 的 `(w,h)` overload + `eval.cpp` γ 搬到 `cheapScore`(**同時刪 `:234-242`**)+ E5/E6 | md5 gate + `run_selfconsistency` 綠 |
| **L2-3** | 24 處機械替換(placement_engine 15 / search 1 / respace 8),逐項對照 L2.3(b) 表 | md5 gate + 全 regression 綠 |
| **L2-4** | G4 move gate(`:701`, `:938`)加 `&& !problem.edge_free`;`applyEdgeSoftAnchor` 早退;`pickAbutmentPair:614` 的 edge 規則 + E8 | md5 gate |
| **L2-5** | `main.cpp --edge-free`(隱藏,同時設 `edge_soft_anchor`) | md5 gate |
| **L2-6** | E1–E4, E7 + 全 regression + L2.6 量測(case2/case3 @600s) | 依 L2.6 判準決定去留 |

## §A 全 regression 指令(WSL native shell)

```bash
cd /home/yyds/Early-Floorplanning-with-Global-Route
make
bash test/run_case00.sh
for s in placement_engine 20260618 20260608 cfg_score selfconsistency integer_output testdata \
         p0 p1 p2 p3 b1 b2 b3 b4 b5 b6 b7 ftagg v5_tail_pricing f1; do
  echo "=== $s ==="; bash "test/run_${s}_regressions.sh" || echo "!!! FAIL $s"
done
# 本設計新增的兩支
bash test/run_undirected_regressions.sh
bash test/run_edgefree_regressions.sh
```

## §B byte-identity gate(每個步驟都要跑)

```bash
cd /home/yyds/Early-Floorplanning-with-Global-Route
D="test/E_testcase_20260727/testcase-upload-20260727"
rm -f /tmp/id_*.cfg
for e in bstar hybrid; do for i in 0 1 2 3 4; do
  bin/efgr "$D/2026 ICCAD_case.case$i.csv" "/tmp/id_${e}_$i.cfg" \
           --engine $e --iters 200 --seed 1 2>/dev/null
done; done
md5sum /tmp/id_*.cfg | sort -k2      # 必須與步驟 0 的基準逐行相同
```
(`--iters 200` 明確關閉 F1 的 600s 自帶預算,`main.cpp:116-117`,讓 gate 秒級完成且完全決定性。)

## §C A/B 量測指令

```bash
# L6
scripts/benchmark.sh --iters 1000                        # 基準(CASE_DIR 指向新測資)
scripts/benchmark.sh --iters 1000 --undirected-bundles   # 變體
# L2:見 L2.6 的 case2/case3 @600s 指令區塊
```

---

## 一頁結論

- **L6** 在會計口徑上是**安全的**:PATH 單向輸出直接滿足官方 undirected 規則;port edge 已被證明
  完全對稱,不需選方向也不需拆分;FT required area 在「路徑固定」下**證明不變**。
  真正改變的是 cheap proxy(變準)、bundle 排序(變準)、chunk 顆粒(變粗 → 唯一風險,已用
  旗標內 `2×kMaxChunks` 中和)。附帶產出一個至今缺席的 `CONN_MISMATCH` 防護。
- **L2** 的關鍵洞察是 **γ=1/um 相對於 OA(10⁷ 量級)近乎免費**:case2 全部 edge block 完全脫離
  也只要 ~3 萬 ≈ 0.11% OA。因此正確的實驗是**全有全無地把 edge block 當 MACRO 丟進拓樸**,
  而不是任何形式的「軟錨定」。同時必須把 γ 加進 **cheap tier** — 沒有這一步就是 G4 的重演。
- 兩者都是 **final 階段、量測先行**;beta 提交 binary(`e28dcaf`)不受任何影響。
