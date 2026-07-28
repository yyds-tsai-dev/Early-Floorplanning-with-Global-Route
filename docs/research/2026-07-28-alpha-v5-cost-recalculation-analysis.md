# Alpha 詳細重評（Cost Function V5）分析 — 2026-07-28

**來源**: `docs/official/alpha_test/cade0002_calculation.html`（官方「PATH Detail Cost Calculation」頁，
標示 *Cost Function V5 · Evaluation Run 2026-07-19*）。受測物與先前 alpha 套件相同
（v3-qa0618 @ 6/22 提交、同一批 `caseN/output.cfg`、同 runtime），**是同一份提交在新版
cost function 下的重新計分**。頁面產生器有顯示 bug：所有「乘積結果」欄位印成 0.00，但所有
輸入數字（OA、overflow、rate、runtime、範例 path 的 GP 座標與小計）為靜態真值，本文全部
數值由這些輸入重建並交叉驗證。

## 一、V5 與舊評測的三處規則變更 + 一處解謎（全部數值級驗證）

### 1. Wirelength 規則變更（來源：E_QA_20260717.pdf A41 + Figure 6）
- **新規則**: guiding point = 相鄰兩矩形**重疊邊界區段的中點**（cross）；channel 內轉彎
  （進出邊軸向不同）插入**兩邊交角點**（corner）；直穿（進出同軸向）不插點。
  WL = 相鄰 GP 之曼哈頓距離鏈總和 × nets。
- **舊 checker**（report.json 所依）= 兩矩形「面向邊中點的中點」鏈（center-anchored），
  即 `src/cfg_score.cpp:29 guidingPoint` 逐位元組驗證過的規則 — **已被 V5 取代**。
- **驗證**: 5 個 case 的官方範例 path 小計（1175.56850 / 1490.57750 / 8606.18400 /
  4225.04700 / 8085.23100）以新規則重算全部 EXACT（≤5e-6）；case0 範例逐 GP 座標
  逐段長度全對。
- **量級**: 同一份 cfg，V5 WL vs 舊 WL = case0 −9.82%、case1 −0.74%、case2 +7.44%、
  case3 +6.24%、case4 +12.99% — 不是常數偏移，會改變排序。
- A41 於 7/17 公佈；同批 QA 的 A43（整數輸出）、A44（case3 對角線）已處理，
  A41 的計分含義當時未移植（T2 於 7/22 對「舊 checker 輸出」做 byte-verify，覆蓋了它）。

### 2. Overflow demand 記帳變更：per-edge 雙計 → 每次穿越每軸單計
- **新規則**: 一條 path 穿越一個 channel，對其觸及的每個**軸向**收一次 nets
  （直穿收該軸 1 次；轉彎 H、V 各 1 次）。不再是「每個進出邊各收一次」（直穿收 2 次）。
- **驗證**: 以單計規則重算 5 case 的 overflow 總量 = 469.23 / 3,375.27 / 18,647.65 /
  12,764.98 / 19,662.17，與 V5 頁面全部 EXACT（另一候選 max-edge 規則在 case1 得
  2,888.88 ✗，被排除）。capacity 規則不變（25 nets/µm，H 用 height、V 用 width；
  Σcapacity 與舊 report 逐位元相同）。
- **量級**: 舊 demand 高估 1.35×（case3）~ 7.9×（case1）。V5 下**五個 case 全部落在
  線性區**（R 最大 0.00906 << θ=0.05）；舊評測 case1 R=0.0538 突破 θ 的問題消失。

### 3. Overflow tail 正確形式（同時解開 case1 0.31% 殘差之謎）
```
R ≤ θ:  β_lin·R·OA
R > θ:  β_lin·θ·OA + β_tail·√(R−θ)·OA      （線性部分封頂在 θ、尾項開根號）
```
- 用舊 case1 的 R=0.05384513657 代入：8,537,517.705283 = 舊官方 penalty **到小數第 6 位
  完全吻合（+0.000000%）**。舊 checker 一直就是這個公式；H1 線性尾項假設（rescorer 現行
  H1 + 0.5% case1 gate）是錯的，qa-draft Q1.3（尾項形式）已有答案，可撤題。
- 策略含義：θ 處的邊際懲罰是 √ 型（θ+ 端斜率無窮大），**壓線 θ 以下比以往認知更重要**；
  遠超 θ 端則比 H1 預估便宜（遞減尾）。

### 4. FT 扣分公式首次成文（= B8/Q4 的答案）
```
FTPenalty        = β_ft · max(0, FT_area_sum − OA)          β_ft = 3.0（確認，不再是未驗證）
IllegalFTPenalty = λ_ft · Σ(illegal block delta_area)        λ_ft = 10（MACRO/EDGE 當 FT 用＝罰款、非 FAIL）
```
- 官方語義是**聚合餘裕**：FT 後的全部 block 面積總和超過 outline 面積才開始扣分
  （對應舊 report `ft.ft_area_by_block` / `cost_breakdown.ft_area_excess` 欄位）。
  alpha 5 case 利用率 ~55–75%，聚合餘裕百萬 µm² 級 → 我們 budget-0 winners 帶的
  1k–1.58M µm² per-block mismatch 在官方口徑下**幾乎必然 0 扣分**。
- 對照解讀（保守層仍留意）：全部 case FT=0，無法從數值分辨 `FT_area_sum` 是 Σ(base+delta)
  還是 Σdelta（依欄位命名取前者）；`outline_after_ft` 對 FT-heavy 解的機制仍未經數據驗證。

## 二、V5 其餘確認（與舊結論一致的部分）

- 5/5 PASS 不變；conn_check、edge violation = 0 不變。
- 常數表 per case：β_lin=20、β_tail=1.5、θ=0.05、β_ft=3、γ=1（edge 違規每 µm）、
  λ_ft=10、δ=0.5（runtime）、M=1.8（incompletion）。
- **排名 α 表 = 0.3 / 0.15 / 0.1 / 0.1 / 0.1**（CSV 內全是 0.2）— 與舊 scoreboard 相同，
  qa-draft 的「α 矛盾」問題**仍然成立且更確定**（V5 頁面直接印 `0.3 × TotalWL`）。
- runtime penalty `δ·t·√OA` **本來就在排名 cost 內**（舊 case_result.json 每 case 都有
  `runtime_penalty` 欄），V5 延續並新增 FAIL 案的 `δ·max_rt_pass·√median_OA`；
  FAIL 案 default_cost = worst_base_cost × M(1.8)。benchmark.sh 的公式正確，
  但 **B2/F1 的 budget 效益計算從未把它扣進去**。

## 三、同一份提交：舊制 vs V5 重建對照

| case | 舊 WL | V5 WL | ΔWL | 舊 ovf | V5 ovf | 舊 cost(排名) | V5 cost(重建) |
|---|---|---|---|---|---|---|---|
| case0 | 5,958,738 | 5,373,448 | −9.8% | 969.2 | 469.2 | 8,659,843 | 8,279,815 |
| case1 | 47,076,253 | 46,726,264 | −0.7% | 26,679.1 | 3,375.3 | 23,411,126 | 15,885,302 |
| case2 | 229,400,000 | 246,461,724 | +7.4% | 29,993.7 | 18,647.7 | 76,218,177 | 73,662,073 |
| case3 | 118,590,978 | 125,992,806 | +6.2% | 17,296.8 | 12,765.0 | 41,036,759 | 40,270,100 |
| case4 | 336,522,396 | 380,230,369 | +13.0% | 39,442.9 | 19,662.2 | 79,619,459 | 79,893,610 |
| **Σ** | | | | | | **228,945,364** | **217,990,901** |

（V5 cost = OA + α_rank·WL_V5 + β_lin·R_V5·OA + δ·t·√OA；全案線性區。V5 頁面總分欄位
因產生器 bug 顯示 0.00，此欄為本文重建值。成本結構：OA 53.9%、α·WL 38.5%、overflow 7.6%。）

## 四、對目前程式與 B-ladder 的影響（stale 清單）

| 元件 | 現狀 | V5 落差 |
|---|---|---|
| `src/cfg_score.cpp`（golden rescorer） | center-anchored GP 鏈 + H1 尾項 + case1 0.5% gate | GP 規則、尾項全要換；gate 移除 |
| `tools/official_cost.py` | H1 線性尾項；ft = per-block mismatch 加總 | 尾項、FT 聚合公式都要換 |
| `src/routing.cpp`（B4 demand 模型） | 進出邊雙計（直穿 ×2） | 單計；高估 1.35–7.9× → 路由/respace 過度保守 |
| `src/eval.cpp:223` | `kBetaFt × feedthrough_mismatch`（per-block） | 官方是聚合餘裕 → 此項大幅高估 FT 成本 |
| aligned metric（B5a.2 裁定） | `official_expected = checker + 3.0·ft_mismatch` | 3.0·mismatch 幾乎必然多收 → 改用 V5 FT 公式 |
| β_ft flip table（B2 報告） | case3 在 β≈7.8 翻轉的脆弱性 | 前提消失（官方按聚合、非 per-block） |
| B7 sliver 量測（+1µm = −0.1~−0.3%/case） | 舊雙計 demand 下量的 | overflow 減半後收益縮水，需重量 |
| F1 budget sweep {0,120,300,600} | 效益未扣 runtime penalty | 必須扣 `0.5·t·√OA`：Σ√OA(5case)≈22.8k → 600s ≈ −6.84M，B2 的 −14.24M 淨益剩約一半 |
| 全部 benchmark 序列（253.4M→153.17M 等） | 舊制 golden 度量 | 需 V5 重跑重排；per-case Pareto-safe 結論待重驗 |

**方向結論**：大方向（WL 為最大槓桿、壓 overflow ≤ θ、絕不 FAIL、anytime 輸出）**不變且被強化**；
但「度量層」已偏離官方 → **V5 對齊移植優先於任何新 B-rung**。順帶新槓桿：edge LOCATION
違規是 γ=1/µm 的軟約束（非 FAIL），理論上可用小幅違規換大 WL 節省（低優先）。

## 五、建議優先序（beta 截止 7/31 17:00 前）

1. **V5-port（新 T3）**: cfg_score.cpp（GP 規則 + 單計 demand + 精確尾項 + FT 聚合式）→
   以本文 5 組 EXACT 驗證點當 golden fixtures；再移植 solver 端（routing demand、eval FT 項、
   Dijkstra 計價用 V5 GP 鏈）。
2. **V5 重跑 benchmark**：重選 default legs / budget（效益扣 runtime penalty）。
3. qa-draft 更新：撤 Q1.3（尾項已解）；保留並強調 α 矛盾題；FT 聚合語義（Σbase+delta vs
   Σdelta、outline_after_ft）可加一小題。
4. （可選）FT 積極化與 mismatch-lexicographic fossil 重新調參 — 在 V5 度量下重評。

**驗證腳本**（重跑可再現本文全部數字）：`tools/verify_v5_alpha.py`（V5 WL 規則、
單計 demand、√-tail、逐案重建；`python3 tools/verify_v5_alpha.py` 全 PASS 即證據成立）。

## 六、執行狀態（2026-07-28 同日補記：V5-port 已完成）

第五節的優先序 1–3 已於同日全部落地（分工：A rescorer → B official_cost.py →
C solver 端 → D 重新基線化 → E Codex 審查）：

- **rescorer** `src/cfg_score.cpp`：V5 GP 鏈 + 單計 demand + 精確分段尾項 +
  **FT 聚合式**（oracle ft_penalty=0 覆蓋缺口一併關閉）；golden 迴歸改釘 V5 錨點
  （5 條官方範例 path 小計 + 5 案 overflow + case1 舊 R 尾項恆等式 + 衍生總表）。
- **solver** `route_geometry/route_cost/routing/eval/model`：同套規則的孿生實作；
  `channelPathSlack` 去減半、Dijkstra u 側出邊在同軸直穿時抑制、
  `RoutingResult.ft_area_sum` 新欄位、`roundCostOfficial` 恆等式保持。
  自洽恆等式 **solver score == rescorer checker_cost** 逐位成立（rel ≈ 1e-11）。
- **19/19 迴歸套件全綠**。值得記錄：所有歷史階梯 floor/ceiling 宣稱（B1/B3/B4/p0/p1）
  在 V5 度量下**真實成立**（overflow 罰款下降抵銷 WL 變動），無需任何翻案。
  p2/20260608 的行為 fixture 以 probe 重推導（V5 紅利實證：channel 直穿可填滿
  真實容量，不再因雙計只填一半）；p3 兩處非空洞性前置改釘 `channelDemands` 非空。
- **V5 benchmark 新基線**（iters=1000 預設）：hybrid Σchecker = **160.24M**、
  Σrank = **124.10M**（rank 含 per-case α + δ·t·√OA + ft_penalty）；bstar Σchecker
  167.54M / Σrank 129.99M。逐案 rank：case4 bstar（46.47M）**勝過** hybrid
  （47.55M，其 overflow 13,037）→ F1 變體重排的直接輸入。
- **Codex 對抗性審查**（task mode）：needs-fixes，4 Major。#3（official_cost.py
  預設代入舊 report WL）與 #4（rank_cost 漏加 ft_penalty）**已修**；
  #1（router FT 定價仍 per-block mismatch，官方免費的 FT 路徑被高價迴避）為
  **有意保守**，= 「FT 積極化」後續 rung；#2（Dijkstra p_ovf 在 R>θ 區仍線性、
  respace.cpp:526 同）已登記為尾項定價精緻化後續（現行全案 R≤0.009 深處線性區，
  且 roundCostOfficial 比較閘已用精確尾項）。
- **#2 已落地（7/29 尾項定價精緻化）**：新增 `roundOverflowPrice`（route_cost.hpp）三段式
  round 定價 — `(O+1)/C≤θ` 維持 `kBetaLin*A/C` 原式**逐位元不變**；跨 θ 用單 net 精確
  secant（入尾 cliff 嚇阻，呼應 §一.3 壓線策略）；`R>θ` 用**平均價 P(R)/O** 而非端點邊際
  —— full rip-up 每輪從零重建 overflow，凹尾上端點邊際會低收（O·P′(O) ≪ P(O)；p2
  ChannelVsFt fixture 實測被帶到官方值 3.02M 的全 channel 解、劣於 2.36M 正解且三輪同病，
  best-round 選擇救不回），平均價是唯一「整輪總收費＝真實罰款」的固定單價。routing.cpp 的
  p_ovf 改為每輪開始時由**上一輪**全域 overflow 重算（round 0 恆為原式；只依前一輪結果，
  無 iteration-order 依賴）；respace `capacityGapCandidates` 在 R>θ 逐候選改用**精確分段
  delta**（θ 下穿 cliff 正確計價），R≤θ 保留 B7 原式逐位元不變。新迴歸
  `test/run_v5_tail_pricing_regressions.sh`（7 fixtures，含合成 R≈1.8 案例：round≥1 尾價使
  次要 bundle 改走短路 + overflow，55000→55750、WL 3.84M→3.40M、gate 勝 legacy）；
  20/20 套件全綠；V5 benchmark 與變更前基線 **.cfg 逐位元相同**、checker/overflow/WL 全欄
  相同（「線性區結果不變」的最強實證）。
