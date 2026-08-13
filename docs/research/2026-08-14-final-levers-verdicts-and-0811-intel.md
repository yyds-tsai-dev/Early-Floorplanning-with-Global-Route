# Final 槓桿判定(L6/L2 600s A/B)+ 官方 0811 更新情報

**日期**: 2026-08-14(final 截止 2026-08-21 前一週)。
**基底**: 主樹 fable5 @ `e28dcaf`;槓桿 worktree `cc/final-levers` @ `6b8e9e3`(163cbe6 CONN 防護 → db9a188 L6 → 6b8e9e3 L2)。
**設計依據**: `docs/research/2026-07-30-final-levers-design.md`(判準 L2.6 / L6.8 照抄執行)。
**量測環境**: WSL、22 threads;每批內各臂並行同載(批內公平);binary = worktree build,
旗標 OFF 對主樹 binary **位元組同一 5/5 案**(iters200/seed1 md5,本日重驗)。
**0811 官方檔**: `docs/official/beta_test/E_Beta_Ranking(20260811).pdf`、
`docs/official/beta_test/E_reference(20260811).html`、
`docs/official/final_submission/E_Contestant Compilation Checklist(20260811)`、
`test/E_testcase_20260811.tar.gz`(sha256 `639e7fea…ea9f`,已解壓 `test/E_testcase_20260811/`)。

## TL;DR

1. **L2(edge-free)否決**:case2 **+8.8%**、case3 **+15.1%**,兩案皆決定性變差
   (設計 L2.6 成功判準「兩案皆勝」完全未達)。機制運作正常(γ 有計、edge 真離邊、
   open=0、自洽 ok)→ 這是乾淨的負面結論:**邊界釘死是有利鷹架,不是束縛**。
   旗標保留(考古),預設 OFF,不再追。R4(主辦方確認)議題隨之作廢。
2. **L6(undirected merge)在舊 5 案不採**:Σ +1.2%,唯一贏 case1(−4.8%)——
   恰是矩陣完全對稱的案。**新 3 案(case5/6/7)矩陣全部鏡像對稱**(case5 值也全等),
   是 L6 的主場;600s A/B 見 §3,**採用判定見 §5**。
3. **官方 0811 包 = 8 案時代開始**:case0–4 與 0727 位元組全同(A/B 有效);
   **case5(全 SOFT、30 blocks、Σnets 345,386)是新的支配項**——我們裸呼叫 828.8s
   (**900s 單案時限只剩 8% 餘裕**)、score 91.1M vs 冠軍估 43.3M(**2.1×落差**)。
   case6/7 是官方故意的 parser 壓力測試(FT 欄裸 0/空值)——**我們原生通過**。
4. **Beta 全場**:41 隊×8 案僅 131 項 PASS;只有 ~4-5 隊 8/8。我們 7/31 交的 binary
   因 glibc 全掛(0/8,記 ~1292M 統一分),8/12 靜態重連結已修復並重交(8/13 期限)。
   compilation checklist 六項全符合(RHEL 8.8/glibc 2.28/靜態/C++17/x86_64/no GPU)。

---

## 1. L2(edge-free)600s A/B — 否決

批次 1(case2/3 三臂同批並行,seed 1,`--time-budget 600`):

| case | anchor(off) | edge-free | Δ | γ(edge_loc_penalty) | OA 變化 |
|---|---|---|---|---|---|
| 2 | **36,629,849.35** | 39,866,084.82 | **+8.8% ✗** | 30,929.7(有動) | 27.24M → 28.58M(+4.9%) |
| 3 | **23,080,365.75** | 26,560,712.00 | **+15.1% ✗** | 15,611.0(有動) | 15.89M → 19.20M(+20.8%) |

- 兩臂 `open_routes=0`、`validation=ok`(warning 行如預期);golden rescorer 與 solver
  分數逐位一致(`--edge-free` 下自洽 rel err 0)。
- **判讀**:G4 時代「量測無效」的兩個根因(decode 重貼邊、cheap tier 無 γ)在 L2 已修
  (E5 梯度測試在案),所以這次的負結果是可信的。edge block 釘邊等於免費的邊界
  預排(packer 繞著固定鷹架壓縮);放開後 contour 搜尋空間變大、SA 在 600s 內反而
  找到更差盆地(iters 沒有掉——c2 54,000 vs 52,500——不是吞吐問題,是**地形問題**)。
- **處置**:旗標與 regression 保留;`defaultVariants()` 不進;文件記檔後不再投入。
  對主辦方的 Q5(edge 放寬保證)不再是 blocker,但問題本身仍值得留在 QA 草稿裡。

## 2. L6(undirected merge)600s A/B — 舊 5 案不採,新案見 §3

批次 1+2(off/l6 同批並行,seed 1,`--time-budget 600`):

| case | off | +L6 | Δ | 備註 |
|---|---|---|---|---|
| 0 | 4,305,930 | 4,475,745 | +3.9% ✗ | 9 對中只 2 對互惠 |
| 1 | 7,594,969.45 | **7,229,638.20** | **−4.8% ✓** | 矩陣**完全對稱**(41/41 對,值全等);WL −13%、OA −3.4% 同時進步 |
| 2 | 36,629,849.35 | 37,494,279.75 | +2.4% ✗ | OA/WL 皆退;mismatch ×5 |
| 3 | 23,080,365.75 | 23,107,408.35 | +0.1% ≈ | WL −3.1% 進、OA +1.6% 退,互抵 |
| 4 | 39,721,207.26 | 40,347,250.25 | +1.6% ✗ | OA −4.3% 進但 WL +10%、overflow +22% |
| Σ | 111,332,321 | 112,654,321 | **+1.2% ✗** | |

- 與設計 R3/R4 預言一致:合併讓 cheap congestion/FT proxy 變大(更真實但更保守),
  SA 轨跡整體外移;WL 常改善、OA 常反噬。**淨效果由矩陣互惠度決定**:
  互惠比例最高的 case1 全面贏,低互惠的 case0/2 明確輸。
- 批次內同載對照,c1_off(7.59M)高於 §9 乾淨基線(7.27M)是 6 路併發搶 CPU 的
  已知效應(B7 教訓:同 session 同載才可比),不影響 A/B 方向。

## 3. 新 3 案(case5/6/7)結構、風險與 600s A/B

### 3.1 結構事實(`test/E_testcase_20260811/testcase_upload_20260712/`)

| case | blocks | 類型 | MAX | α | 有向非零 | Σnets | 鏡像對稱 | 值相等 |
|---|---|---|---|---|---|---|---|---|
| 5 | 30+1 | **全 SOFT** | 8200×7500 | **0.05** | 602 | **345,386** | ✅ 301 對 | ✅ |
| 6 | 10+1 | 混合 | 4000×4000 | 0.2 | 40 | 22,000 | ✅ 20 對 | ✗ |
| 7 | 12+1 | 混合 | 2400×2100 | 0.15 | 100 | 30,940 | ✅ 50 對 | ✗ |

- case0–4 與 0727 版**位元組全同**(cmp 5/5);case3 對角歸零維持。
- **case6/7 是官方 parser 壓力測試**(reference 0811 Beta 注意事項明載):case6 BLK02
  FT 首檔裸 `0`、case7 BLK06–12 首檔空值;「要求恰 4 個 %」的 parser 直接拒讀。
  **我們 io.cpp 原生容忍**:iters50/200 煙測 case6/7 皆 `validation=ok, open=0, rc=0`。
- **case5 = 全 SOFT 30 塊 + 34.5 萬 nets**:beta TIMEOUT 重災區(官方明載 **900s 單案時限**)。

### 3.2 case5 runtime 風險(提交 binary 實測)

用實際提交的 static binary(`submission/beta_resubmission_20260812/`)裸呼叫:
**wall 828.8s(13:48.81)**、RSS 659MB、rc=0、`validation=ok`、score 91,146,163
(OA 39.81M、WL 980.4M、overflow 7,838 nets、iters 14,500)。

- 900s 時限餘裕僅 8%(且本測前 4.5 分鐘與 6 路批次共載,乾淨值估 ~700–780s;
  官方機速未知)——**TIMEOUT 風險真實存在**。
- 根因:裸呼叫 600s budget 只管 phase-2;**phase-1(全 registry @ iters1000)結構上
  不受 budget 中斷**(B5a.2 設計,`search.cpp` runPricingPortfolio)。case5 的
  per-leg 成本(602 bundles × Dijkstra × exact evals)把 phase-1 撐到 ~830s。
- **處置(P0,見 §5)**:phase-1 leg 間加 wall-clock 檢查(僅 budget>0 生效;
  explicit `--iters` 路徑零改動 → 全 regression byte-identical)。B2 的 fold 結構
  保證「跑完的 legs 折疊取優 + anytime 輸出」,中斷安全。

### 3.3 case5/6/7 600s A/B(批次 3;case5 對單獨計時,2026-08-14 完成)

| case | off | +L6 | Δ score | off wall | L6 wall | iters off→L6 |
|---|---|---|---|---|---|---|
| 5 | 91,146,163.24 | **88,290,296.10** | **−3.1% ✓** | **817.8s** | **636.2s(−22%)** | 14,500→17,000 |
| 6 | 5,355,290 | **5,219,032.50** | −2.5% ✓ | ~600s | ~600s | 693K→1,290K |
| 7 | **10,467,784.36** | 11,252,175.13 | **+7.5% ✗** | ~600s | ~600s | 307K→514K |

(六臂 golden rescore 逐位吻合;全臂 `open_routes=0, validation=ok`。case5 off 臂 score
與 §3.2 裸呼叫**完全相同**——phase-1 跑滿 8 legs 即決定性,budget 只砍掉了 phase-2,
walls 817.8/828.8s 差異純屬負載。批次 3 與迴歸電池部分並行,兩臂同載、批內公平。)

- **case5 雙贏**:score −3.1%(overflow 550,303→**0**、WL 980M→764M(−22%),
  代價 OA 39.8M→50.1M——merged 定價讓 router 用面積換 overflow/WL,α=0.05 下划算)
  **且 wall 817.8→636.2s**:900s 時限餘裕從 9% 升到 **29%**。
- case6 −2.5%(OA −6.5%);**case7 +7.5% 明確輸**(WL +14.7%)——兩案同為
  「鏡像存在但值不等」,方向相反 → 鏡像規則不可靠。
- **合併判定**:blanket default-on 被舊 5 案 Σ+1.2% 否決;**「矩陣值完全對稱才自動合併」
  規則恰好只圈住實測贏家 {case1 −4.8%, case5 −3.1%},並排除所有輸家**(case7 被排除
  正是輸家;case6 的 −2.5% 為規則保守性付出的代價)。**採用形式 = 自動規則**
  (裸呼叫無旗標可傳);`--undirected-bundles` 旗標保留供量測。

## 4. Beta 結果與 8 案冠軍水位(`E_Beta_Ranking(20260811).pdf`,Team ID 官方塗黑)

**全場**:41 隊 × 8 案 = 328 項,PASS 131;失敗分布 NO_EXECUTABLE 120、CHECKER_FAIL 42、
NO_OUTPUT_CFG 30(我們的 8 項在此,glibc)、TIMEOUT 5(集中 case5)。8/8 全過僅 ~4-5 隊。
總榜 rank1:8/8、ΣWL 543,851,254、ΣArea 109,108,258。0/8 隊伍(含我們)統一 ~1292M
(fail_runtime_penalty 用全體 PASS 統計,設計如此)。

**各案冠軍(cost ≈ Area + α·WL 估;其 overflow/FT/γ 罰項被遮蔽 → 低估)**:

| case | PASS | 冠軍 Area | 冠軍 WL | 冠軍 cost 估 | 我們(600s) | 差距 |
|---|---|---|---|---|---|---|
| 0 | 19/41 | 3,698,486 | 1,404,302 | 4,119,777 | 4,305,930 | +4.5% |
| 1 | 19/41 | 5,406,271 | 3,649,393 | 5,953,680 | 7,269,498(§9) | +22.1% |
| 2 | 19/41 | 25,558,911 | 66,232,400 | 32,182,151 | 36,357,735(§9) | +13.0% |
| 3 | 18/41 | 14,327,456 | 31,410,001 | 17,468,456 | 22,754,385(§9) | +30.3% |
| 4 | 18/41 | 20,808,648(r2) | 76,777,974(r2) | 28,486,445 | 39,721,207 | **+39.4%** |
| 5 | **13/41** | 27,421,020 | 316,594,862 | 43,250,763 | 91,146,163(§3.2/§3.3) | **+110.7%** |
| 6 | 15/41 | 3,365,461 | 3,938,700 | 4,153,201 | 5,355,290(§3.3) | +28.9% |
| 7 | **10/41** | 4,838,400 | 17,852,665 | 7,516,300 | 10,467,784(§3.3) | +39.3% |
| Σ | | | | **≈143.1M** | **≈217.4M**(§9 五案 110.41M + 本日三案) | **+51.9%** |

- **beta 場整體大幅進步**(對照 alpha:case0 我們曾贏 12.7%、case4 曾贏 5.6% →
  現在分別 +4.5%/+39.4% 落後)。α 對齊紅利大家都拿到了。
- **落差集中**:case5(+47.9M)≫ case4(+11.2M)> case3(+5.3M)> case2(+4.2M)>
  case7(+3.0M)> case1(+1.3M)> case6(+1.2M)> case0(+0.2M)。
  **case5 一案的落差(47.9M)= 其餘七案總和(26.4M)的 1.8 倍** → final 週優先級由它定調。
- case7 冠軍 Area 4.84M < MAX(5.04M)而我們貼 MAX;ranks 3/4 也貼 MAX——
  shrink 空間存在但需先解 overflow(我們 6,560 nets)。

## 5. 採用決策與 final 週優先序

**本日落地(cc/final-levers → fable5)**:
1. CONN coverage 檢查(`163cbe6`,旗標無關)——官方 `CONN_MISMATCH` 至今唯一防護。
2. L6/L2 旗標實作(預設 OFF,位元組同一已證)+ 本文件 + 0811 官方檔。

**接續實作(P0,依證據排序)**:
1. **Phase-1 budget bound(timeout 安全)**:§3.2 處置。case5 828s→~600s,
   消除 900s 風險;所有 explicit-iters 路徑 byte-identical。
2. **L6 值對稱自動規則**(§3.3 判定):矩陣值全對稱 → 自動 `undirected_bundles=true`
   後合併。實測圈住 {case1 −4.8%, case5 −3.1%+wall−22%},排除所有輸家;
   估 Σ −3.2M,且 case5 時限餘裕 9%→29%(與 #1 疊加更穩)。
3. **case5 專項**(下一輪研究執行,`2026-08-13-external-levers-v3-survey.md` 對照):
   全 SOFT + α=0.05 + 34.5 萬 nets → soft-shaping(survey A4/#6)、cheap-tier
   channel 密度下界(B5)、incremental exact tier(B2/#5)、constant-gap 協商(B1/#4)。
   目標:WL 980M→冠軍 317M 的量級差、overflow 7,838 nets。
4. case4/2/3 WL:MCF whitespace 重分配(C1/#1)、weight-ladder legs(#3)、
   LNS 局部精排(C3/#7)。

## 引用

- A/B 原始檔:session scratchpad `ab/`(c{0..7}_{off,l6,ef}.{cfg,log,time});
  golden rescore 逐案與 solver score 逐位一致(`bin/efgr_rescore`)。
- 0811 官方檔:見文件頭;reference diff 重點 = Beta 注意事項新章(case3 對角、
  case6/7 格式變體、case5 900s、扁平提交結構、M=1.8、全 fail 統一分)+
  case5/6/7 參數列(β_lin=20、θ=0.05、γ=1、δ=0.5 不變)。
- 提交合規:`submission/beta_resubmission_20260812/VERIFICATION.md`(checklist 六項
  對照 Codex 四項審查,全過)。
- 對稱性驗證腳本輸出:case1 82/82 值等;case5 602/602 值等;case6/7 鏡像存在值不等。
