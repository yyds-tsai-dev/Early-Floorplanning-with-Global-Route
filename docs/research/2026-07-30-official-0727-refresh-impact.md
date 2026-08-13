# 官方 2026-07-27 更新影響分析 — QA / Reference / Alpha Ranking / 新測資

**日期**: 2026-07-30(beta 截止前一天)。
**資料源**: `docs/official/E_QA_20260727.pdf`(21pp, Q1–Q50)、`docs/official/E_reference_20260727.html`、
`docs/official/alpha_test/E_Alpha_Ranking(20260727).pdf`、`test/E_testcase_20260727.rar`。
**基底**: fable5 @ `e28dcaf`(V5-port + tail-pricing #2 + F-FT + G4 + F1 全部落地後)。
**方法**: 兩個子代理精讀(QA diff、測資解析;findings 存 session scratchpad)+ 主線交叉驗證
(F1/G4/golden 錨點逐一對原始文件與程式碼複核)+ 新測資 smoke 實測。

## TL;DR(按影響排序)

1. **新測資 = 同 5 案,但 CSV 內 α 改為官方評測值**(0.2 → 0.3/0.15/0.1/0.1/0.1;case3 順帶
   把兩個對角 self-loop 修為 0,QA A46「題目修正」)。我們的 solver 從 CSV 讀 α → 內部搜尋目標
   **自動對齊官方目標**。smoke 實測(--iters 200, seed 1):case2/3/4 分數立刻大幅改善,
   **Σ=115.99M,已優於 F1@600s 在舊 CSV 上的 138.82M(−16.4%)** — 是本次更新最大紅利。
   **600s 裸呼叫 re-baseline 完成(§9):Σ=110.41M(−20.5%),case0/case4 已勝過
   alpha 單案冠軍,Σ 距 composite 夢幻隊僅 0.8% → 已在 alpha #1 水準。**
2. **V5 度量層全面被官方確認且升格為規範**:QA 唯一新增 Q45–Q50,其中 **A50 把
   reference.html 升格為官方引用文件**;該頁明載完整公式與參數(β_lin=20、β_tail=1.5、β_ft=3、
   θ=0.05、γ=1、δ=0.5、λ_ft=10、M=1.8),FAQ#10 明言**常數沿用 Beta/Final、A41/A5 維持修正後
   口徑**。我們 7/28 V5-port 逐項吻合(`route_cost.hpp` 全對上)→ 度量方向零翻修。
3. **Alpha 總排名我們第 9 / 12**(5/5 CORRECT;以 Total WL 804,784,610 與我們 6/22 提交的 V5
   重算精確比對確認)。落差解剖:輸在 **OutlineArea**(我們 Σ117.5M vs 冠軍 83.6M);官方明示
   優先級 `OA ≫ WL ≫ Overflow ∼ FT ≫ EdgeLoc ∼ Runtime`。α 修正後這個短板已在收斂
   (smoke:case2 OA 27.1M、case4 OA 25.5M 皆優於 alpha 單案冠軍)。
4. **runtime penalty δ·t·√OA 不是新威脅**:F1(7/29)的 rank 口徑**已含**此項且 Σrank 對
   budget 單調下降(600s 最優;case4 需 600s 才破解 −7.48M)→ 維持裸呼叫 600s。
   (QA 子代理曾判讀 F1「未計 runtime」— 經查 F1 文件第 5–6 行與結論第 1 點,**已計**,予以更正。)
5. 整數輸出維持(A43 整數化「待確認」,兩頭安全);絕不穿 macro/edge(ILLEGAL_FT WARNING 與
   ILLEGAL_INTERNAL_TURN FAIL 述詞重疊、判定相反 → 不可當可購買選項);outline 3um shave
   不做(§5-L4)。

## 1. Cost Function V5 官方版 vs 我們的實作對照

官方公式(reference §cost;經 A50 具規範地位):

```
Cost = OutlineArea + α·TotalWL + OverflowPenalty + FTPenalty
     + λ_ft·IllegalFTDeltaArea + γ·EdgeLocationPenalty + δ·runtime_sec·√OutlineArea
```

| 項 | 官方定義 | 我們的狀態 |
|---|---|---|
| OverflowPenalty | R=ΣOverflow/ΣCapacity;R≤θ:β_lin·R·OA;過 θ:β_lin·θ·OA+β_tail·√(R−θ)·OA | ✅ `officialOverflowPenalty`(route_cost.hpp:58)+ 三段式 round pricing(tail #2) |
| FTPenalty | β_ft·max(0, Σft_area_by_block − OA)(聚合餘裕) | ✅ `roundCostOfficial`(route_cost.hpp:370)、eval.cpp:225、cfg_score.cpp:380 |
| WL(A41/A48) | 重疊邊界線段**中點** GP + 同 rect 轉角點(A48 附兩個 worked example:spec p10 例 =108000、case0 PATH1500 =520.42,Alpha 舊規則 111000/655.6 作廢) | ✅ V5-port 孿生實作 |
| Demand(A5/A47/A49) | 每 net 過 channel 計 1 次;直穿 1 軸、轉向 2 軸;容量 h=25·height、v=25·width;**channel-level 非 segmental**(A47 引 checker 源碼;A3.2 舊例作廢) | ✅ V5-port 單計;A49 證實 Alpha report.json demand 曾 ×2(5700→3600) |
| γ·EdgeLocation | 違規 EDGE 偏移距離(um)×γ=1;**WARNING 非 FAIL**;50um 位移限制取消 | ✅ eval 已計(kGammaEdge);G4 量測維持 off — 但見 §5-L2 的量測缺陷 |
| δ·runtime | 0.5·rt·√OA;FAIL 案 δ·max_rt_pass·√median_OA(官方表反算驗證至 5 位有效數字) | ✅ F1 rank 口徑已含(solver 內無需算);600s 決策成立 |
| λ_ft·IllegalFT | MACRO/EDGE 當 FT:λ_ft=10×delta_area,「WARNING 非 FAIL」 | ⚠️ 不實作、**維持不觸發**:`ILLEGAL_INTERNAL_TURN`(ERROR)述詞覆蓋同一行為(reference 自相矛盾,§6-C2)→ SOFT-only feedthrough 不變 |
| α | **評測 α 已寫進 0727 新 CSV**(0.3/0.15/0.1/0.1/0.1);checker 另有 `--alpha-override` | ✅ 我們本來就讀 CSV α → 自動對齊(§7);A29/A35.5 vs reference 的矛盾**被資料更新實質化解** |
| Incompletion | 未 PASS:worst_cost_of_case×M=1.8 + fail_runtime | n/a(5/5;隱藏 3 案靠 robustness + anytime 護住) |

**座標精度(A34/A39/A43,A50→reference §coord-precision)**:checker 全程**原始浮點**、
不 ceil 不 round;幾何判斷統一 **EPS=3um**;整數化 A43「待主辦方確認」,且 checker 作者明文
反對(會破壞浮點隊伍,點名 top-2 cade0007)。官方 FAQ:「輸出整數可以」。
→ **全整數輸出是唯一兩頭安全策略,beta 維持**。integer 量化的理論代價(官方例 outline +0.036%,
QA 子代理估堆疊累積 0.3–0.8% OA)列為 final 階段研究(§5-L5)。

## 2. Alpha Ranking 解讀:我們在哪、差多少、差在哪

- **身分比對**:rank 9 的 Total WL=804,784,610 = 我們 6/22 提交五案 V5 WL 和
  (5,373,448+46,726,264+246,461,724+125,992,806+380,230,369);Total Area=117,484,376 同吻合
  → **rank 9/12,5/5 CORRECT**(5/5 中最差;case3 僅 7 隊過,M=1.8 讓四支 4/5 隊仍在我們前)。
- **對手強度(單案冠軍,cost ≈ OA+α·WL 估,其 overflow/FT 欄被遮蔽)**:

| case | alpha 單案最佳(估) | 我們 F1@600s(舊 CSV) | 我們 smoke 200-iter(新 CSV) |
|---|---|---|---|
| 0 | ≈4.93M(OA 4.302M) | **4.109M ✅ 已勝全場** | 4.82M(α↑0.3 需預算,600s 待驗) |
| 1 | ≈6.98M(OA 6.508M) | 7.973M | 7.81M |
| 2 | ≈34.71M(OA 27.975M) | 43.635M | **36.44M(OA 27.14M < 冠軍)** |
| 3 | ≈20.84M(OA 16.543M;場上最小 OA 14.70M) | 29.316M | **23.41M(OA 16.02M < 冠軍)** |
| 4 | ≈42.09M(OA 27.229M) | 53.785M | **43.50M(OA 25.50M < 冠軍)** |
| Σ | ≈109.55M(composite,不可同時達成) | 138.82M | **115.99M** |

- **解剖**:落差本質是 **placement 密度(OA)**,而非路由;α 修正讓 SA 終於以正確權重壓 OA,
  200 iters 就把 case2/3/4 的 OA 壓到冠軍級。**強化既有 placement-engine 長期方向,不改架構**。
- 隱藏 3 案(A38 仍「ETA 7/15」未交付;A42 總時限 2h/8 案):偏大 case 預期,600s×8=4800s
  留 33% 裕度(F1 already 論證)。官方 PASS 隊 max runtime:case1 1028.7s、case4 688.4s
  → 單案時限遠寬於 600s,TIMEOUT 風險低。

## 3. QA 20260727 逐條 diff(vs 0717;唯一新增 = Q45–Q50)

QA 0727 pp.1–18 與 0717 **逐頁完全相同**(含過時的 A1.3/A38/A44 措辭);新增僅 pp.18–21:

| # | 內容 | 對我們的影響 |
|---|---|---|
| Q45/A45 | MACRO PORT EDGE=0「可擇多組任選出發」(多 edge 同時用) | ✅ 已合規:`portAllows()` 對 free-port 全開;chunk 級 Dijkstra 本可分邊出線 |
| Q46/A46 | case3 self-loop:「題目修正」 | ✅ 新 CSV 對角已歸 0(§7);io.cpp:186 本就跳過對角;`CONN_MISMATCH` 要求對角=0 一致 |
| Q47/A47 | overflow 是 **channel-level per-axis**,附 checker 源碼(cap_h=25h/cap_v=25w) | ✅ 模型完全一致;A3.2 舊「水平切割」例作廢,勿實作 |
| Q48/A48 | guiding point =「交界邊的中點」(兩個 worked example 判 Alpha 規則作廢) | ✅ 我們就是 A41 中點鏈;golden 已重釘 V5 錨(見下) |
| Q49/A49 | demand「單次進出算一次」(Alpha report.json 5700 vs 修正 3600) | ✅ 我們單計;意涵:beta checker 回報的 overflow 將比 alpha 回饋低 ~40%,headroom 更大 |
| Q50/A50 | A43 整數化?「請參考 reference.html」 | **reference 升格規範文件**;該頁答案 = 不整數化、全浮點、EPS=3um,但 A43 標「待確認」→ 整數輸出續留 |

**Golden 錨點驗證(QA 子代理疑慮,已排除)**:`test/regression_cfg_score.cpp:1-11` 明載
golden 值為 **V5 錨點**(tools/verify_v5_alpha.py 推導 + 官方 V5 計算頁交叉驗證);
report.json 只再用於 V5 未變的 `channel_total_capacity` 與舊-R 尾項恆等式 → oracle 沒有
錨在被廢止的 Alpha 規則上。

## 4. Error/Warning 代碼與檢查邊界(reference §errors)

- **ERROR(FAIL)**:CSV/CFG 格式、OUTLINE_EXCEED(宣告>MAX)、OUTLINE_VIOLATION(塊超宣告
  outline **>3um**)、BLOCK_OVERLAP(**雙軸皆** overlap >3um)、ROUTING_OPEN(「edge 非
  opposite / 無共享邊 / 共享長度為零」— 我們的 PATH 建構天然滿足)、ILLEGAL_INTERNAL_TURN、
  CONN_MISMATCH(**undirected 合計比對**)。
- **WARNING(記 penalty)**:EDGE_LOCATION_VIOLATION(γ)、ILLEGAL_FT(λ_ft)、CHANNEL_OVERFLOW。
- **執行失敗根因**(GLIBC/GLIBCXX/SEGFAULT/ARG_HANDLING/WROTE_NO_CFG/TIMEOUT…):
  **exit code 不在其中** — 官方只看有沒有產出 .cfg;我們的 exit-1 慣例是 dev 訊號,安全。
- **官方沒有的檢查**(QA 子代理盤點):SOFT 面積/aspect-ratio、MACRO 尺寸/旋轉、PORT EDGE
  違規都**沒有 error code**。我們 validator 比 checker 嚴 → 維持嚴(未公布 ≠ 不存在),
  但要知道這是搜尋空間的自我設限。
- EPS 語義:官方 3um vs 我們 kEps=1e-6(嚴 3×10⁶ 倍)→ 我們絕不會被浮點噪音 FAIL;
  反向者見 §5-L3。

## 5. 槓桿清單(效益/風險/工程量;結論在各項末)

### L1|新 CSV re-baseline(**完成,見 §9**)
α 修正讓內部目標=官方目標,smoke 已證 −16.4%(§2 表)。行動:5 案 **裸呼叫**(600s 自帶
budget)完整 benchmark + solver-score==rescorer 自洽驗證 + 5/5 validation green → 這組數字
就是 beta 提交的預期成績。**風險零**(無程式碼改動,純資料對齊)。註:F1/B-ladder 的既有
結論(600s、default legs)原則上被 α 變更擾動,但方向(Σrank 隨 budget 單調降)有 B2 fold
結構保證;600s 結果出來後只驗不改。

### L2|Edge soft-anchor 真・鬆綁實驗(final 階段,量測先行)
官方:EDGE_LOCATION_VIOLATION = WARNING、γ=1/um(≈免費);50um 限制取消。QA 子代理發現
G4「0/5 採用」的量測**無效**:`--edge-soft-anchor` 只加了沿邊滑動的 mutation
(placement_engine.cpp:523-546),而 `decode()` 仍無條件 `placeEdge`/`legalizeEdgeBlocks`
重新貼邊(:341,419-424)→ **搜尋從未見過 edge block 離邊的盤面**。case2 四塊 300×1305 釘死
四邊,鬆綁 OA 上限可能是百分點級。但:spec 原文 hard-fail、reference 自稱「放寬」;若 final
變卦即 FAIL(M=1.8×worst≈63M/case2)。**行動:beta 不動;final 前做隔離實驗**(decode 加
旗標跳過 edge 重貼 + γ 罰入 eval 已有),用數字決定,並向主辦方直接確認。

### L3|EPS=3um 相鄰判定放寬(觀察項)
我們 routing 相鄰判定 kEps 級,checker 認可 ≤3um 縫隙的相鄰 → 理論上有 checker-legal 但
被我們丟棄的 routable 邊。整數 decode 下縫隙為 0 或 ≥1um;channel 抽取按塊邊切割,天然精確
相鄰 → 實際影響估零。**不行動,列為 respacing 改動時的檢查點**。

### L4|Outline 3um shave(**否決**)
宣告 outline 每邊少 3um 可省 ≈3·Σ(W+H)≈122K Σ(+0.076%),但 channel 會落在宣告 outline
外 — checker 是否檢查 channel-in-outline **未公布**;不對稱風險(FAIL×1.8)遠大於收益。
兩個獨立分析(主線、QA 子代理)同結論:**不做**。

### L5|整數量化的內部成本(final 階段研究)
整數 decode 是自我扣分(官方例 +0.036% outline;堆疊 ceil 累積、A43.3 soft「取小」強制,
估 0.3–0.8% OA)。但 A43 未撤回 → 浮點輸出在「整數化突然實作」情境下是災難(官方自己的
per-component ceil 例)。**beta 維持整數;final 若官方裁定不整數化,再開浮點輸出 gated 實驗**。

### L6|Undirected bundle 合併(final 候補)
CONN_MISMATCH 無向比對(A↔B 合計)且「方向分配不受限」;我們按有向逐項建 bundle
(io.cpp:187)→ 合併後兩向 nets 可全走較優路、PATH 減行、chunk 顆粒變大(注意與 FT tier
≤3000 邊界互動)。估 −0.1~−0.5% WL。**beta 不動(路由行為變更需全迴歸),final 首波**。

## 6. 矛盾與未決清單

- **C1|α 來源文字矛盾(已被資料化解)**:A29/A35.5「α 由 input file 讀取」vs reference
  「CSV α=0.2 僅供參考、評測用預設表」。0727 新 CSV 直接把評測 α 寫進檔案 → 兩句同時為真。
  隱藏 3 案的 α 預期同樣隨其 CSV 交付(讀 CSV 即正確;不硬編 per-case 表)。
- **C2|ILLEGAL_FT(WARNING)vs ILLEGAL_INTERNAL_TURN(ERROR)述詞重疊**:MACRO/EDGE 出現在
  PATH 中間既是前者(λ_ft 罰)也是後者(FAIL)。→ λ_ft 不可視為可購買;SOFT-only 不變。
- **C3|A43 整數化「待主辦方確認」**:checker 作者反對實作;維持整數輸出對沖。
- **C4|FT 換算內部的 ceil(A34 是否也被「全浮點」覆蓋)**:ft.hpp 依 spec 帶 ceil;五個
  alpha 提交 FT 用量全 0,無官方數字可校準 → 未驗證假設,列 watch(FT 真被用到時的 ±1um² 級)。
- **C5|A38 的 3 筆追加測資仍未交付**(QA 文字未更新)→ beta 以 8 案規劃不變。

## 7. 新測資 E_testcase_20260727 事實(子代理解析 + 主線 smoke)

- 內容物:`testcase-upload-20260727/2026 ICCAD_case.case{0..4}.csv` — **同名同構的 5 案更新版**,
  無新案、無新欄位、無新表頭;FT CONVERSION 四檔(≤3000:20%、≤6000:40%、≤9000:80%、
  >9000:100%)與 20260712 完全相同。
- **逐案唯一差異 = α**:case0 0.2→**0.3**、case1 0.2→**0.15**、case2/3/4 0.2→**0.1**;
  case3 另把 matrix[BLK09][BLK09]=63、[BLK13][BLK13]=123 歸 0(QA A44/A46 errata 上游修正;
  我們 parser 本就跳過對角,行為不變)。
- 規模:blocks 7/14/16/15/24,nonzero bundles 11/82/74/82/51,Σnets 7,400/47,964/73,230/
  54,891/77,416;矩陣 4/5 案非對稱(case1 恰對稱)。
- **Smoke(--iters 200, seed 1,全案)**:5/5 `validation=ok, exit=0`,零 open routes;
  分數見 §2 表。600s 裸呼叫 re-baseline 已完成,結果見 §9(Σ=110.41M)。

## 8. 建議執行序

**Beta(7/31 17:00 前)**
1. **P0 提交護航**:§4 根因表 checklist(靜態連結/glibc 相容、輸出同名 .cfg、ARG_HANDLING
   裸呼叫語義、WROTE_NO_CFG 防呆);以 **0727 新 CSV** 做 5×600s 提交演練 + 自洽 rescorer 全綠。
2. **P1 = L1 re-baseline**(完成,§9):Σ=110.41M 即提交預期成績;僅驗證不調參。
3. P2(僅在 P1 發現異常時):individual leg 檢視(α 變更可能輕移 legs 相對強弱;B2 fold
   保證不劣化,但最優 leg 可能換)。

**Final 階段(beta 後)— 2026-07-31 執行狀態**
4. ✅ 設計:`docs/research/2026-07-30-final-levers-design.md`;實作落地於 worktree
   `/home/yyds/efgr-levers` 分支 `cc/final-levers`:`163cbe6`(CONN 防護,官方
   CONN_MISMATCH 至今零防護)→ `db9a188`(L6 `--undirected-bundles`)→
   `6b8e9e3`(L2 `--edge-free`)。每步 byte-identity gate 10/10、24/24 suites 綠、
   selfconsistency 在 `--edge-free` 下 rel err ~1e-12。
5. 600s A/B 量測(L6 五案 off/on 交錯 + L2 case2/3)進行中 → 去留判定見 §10。
6. F1 附記的改善型 early-stop(case1 型預算稅回收)未啟動;L5 浮點輸出實驗
   (視 A43 裁決)、L3 檢查點維持。
7. **主軸不變**:placement 密度(OA)為第一優先 — §2 密度標竿(case3 場上最小 OA 14.70M,
   whitespace ~11%)是 final 的目標線。

## 9. Re-baseline 結果(600s 裸呼叫,新 CSV;2026-07-30 23:16 完成)

主樹 `e28dcaf` binary,裸呼叫(自帶 600s budget),seed 1,輸出 /tmp/b600_case{0..4}.cfg,
log /tmp/bench600_20260727.log。**5/5 validation=ok、open_routes=0、exit=0。**

| case | score(=checker) | OA | WL | ovf_nets | iters | vs 舊CSV@600s | vs alpha 單案冠軍(估) |
|---|---|---|---|---|---|---|---|
| 0 | 4,305,930 | 3,858,480 | 1,491,500 | 0 | 2,623,500 | +4.8%(退) | 4.932M → **贏 12.7%** |
| 1 | 7,269,497.75 | 5,687,600 | 10,545,985 | 0 | 225,000 | **−8.8%** | 6.982M → 差 4.1% |
| 2 | 36,357,734.55 | 28,235,680 | 81,220,545.5 | 0 | 117,000 | **−16.7%** | 34.712M → 差 4.7% |
| 3 | 22,754,385 | 16,434,948 | 63,194,370 | 0 | 94,500 | **−22.4%** | 20.835M → 差 9.2%(OA 16.43M ≈ 冠軍 16.54M) |
| 4 | 39,721,207.26 | 24,816,540 | 140,303,704 | 2,521 | 55,000 | **−26.1%** | 42.086M → **贏 5.6%**(OA −8.9%) |
| **Σ** | **110,408,754.6** | 79,033,248 | | | | **−20.5%**(138.82M→110.41M) | composite 109.55M → **差 0.8%** |

解讀:
- α 對齊的紅利全部兌現:Σ −20.5%,其中 case3/4 主要靠 OA(SA 終於以正確權重壓面積)。
- 「夢幻隊」composite(每案取全場冠軍、其 overflow/FT/runtime 罰項被遮蔽=低估)只贏我們 0.8%;
  真實 alpha #1 的總分必高於 composite → **本結果已在 alpha 冠軍水準之上**(對手 beta 也會
  進步,此為「追平上一輪冠軍」的證據,非 beta 保證)。
- ΣOA 79.03M vs alpha 提交時 117.5M(−32.7%);對照 alpha 全場最佳 ΣArea 83.6M — 我們現在
  的 ΣOA **低於當時全場任何一隊**。
- Watch items:(a) case0 比舊 CSV run 退 0.197M(α=0.3 下搜尋軌跡不同;case0 量小、
  影響 0.2M 級,可用 leg/seed 檢視);(b) case4 與 L6 實作編譯並行,CPU 競爭可能吃掉
  少量 iters(數字偏保守方向);(c) case4 唯一 overflow 非零(2,521 nets,線性區,
  罰項已含在 score)。
- **此表即 beta 提交的預期成績**(主辦方 harness 餵這批 CSV、裸呼叫、同一顆 binary)。

## 10. 去留判定(2026-08-14 補記)

L6/L2 的 600s A/B 已於 2026-08-13/14 完成,判定與官方 0811 更新情報(8 案時代、
case5 900s 時限、beta 冠軍水位)整併於
**`docs/research/2026-08-14-final-levers-verdicts-and-0811-intel.md`**:
L2 否決(case2 +8.8%/case3 +15.1%);L6 舊 5 案不採(Σ+1.2%,唯 case1 −4.8%),
新案(case5/6/7,矩陣全鏡像對稱)判定見該文件 §3.3。

## 引用

- reference 規範地位:`E_QA_20260727.pdf` p21 A50;內容:`E_reference_20260727.html`
  (§cost、§errors、§spec、§qa-diff、§coord-precision)。
- QA 新六題:`E_QA_20260727.pdf` pp.18–21(Q45–Q50);pp.1–18 與 0717 逐頁相同。
- 排名:`E_Alpha_Ranking(20260727).pdf`;我們 = rank9(WL 804,784,610 比對,
  `docs/research/2026-07-28-alpha-v5-cost-recalculation-analysis.md` §二逐案表)。
- F1 600s(runtime penalty 已入帳):`docs/research/2026-07-29-f1-budget-selection.md`。
- G4 soft-anchor 量測缺陷:`src/placement_engine.cpp:341,419-424,523-546`(decode 無條件重貼邊)。
- Golden V5 錨點:`test/regression_cfg_score.cpp:1-11`。
- 子代理 findings(session scratchpad):`qa20260727-findings.md`、`testcase20260727-findings.md`。
- 新測資解壓:`test/E_testcase_20260727/testcase-upload-20260727/`(smoke: 5/5 ok)。
