# F1 預算選擇掃描與出貨預設決策 — 2026-07-29

**基底**: `8a6ddb0`（V5 port + F-FT 10-leg registry + tail-pricing 全部落地後）。
掃描於隔離 worktree `/home/yyds/efgr-f1-sweep` 執行（避免並行工作換掉二進位），
`scripts/sweep.sh`，seed 1，rank = OA + α_rank·WL + overflow_penalty + ft_penalty
+ edge_penalty + 0.5·rt·√OA（V5 口徑，Codex #4/G4 修正後）。

## 結果（hybrid；budget 0 另含 bstar 對照）

| budget | Σchecker | Σrank | Σ runtime |
|---|---|---|---|
| 0（iters=1000 參考） | 153,934,223 | 120,196,963 | 398s |
| 120 | 152,756,217 | 119,665,623 | 656s |
| 300 | 147,467,875 | 117,387,905 | 1,480s |
| **600** | **138,818,040（−9.82%）** | **115,786,635** | 2,976s |

（bstar@0：Σchecker 163.05M / Σrank 128.88M — hybrid 全面領先，僅列參考。）

逐案 checker（M）與膝點：

| case | b0 | b120 | b300 | b600 | 膝點（rank 最優） |
|---|---|---|---|---|---|
| 0 | 4.752 | 4.119 | 4.119 | 4.109 | ~120s（rank 4.383M） |
| 1 | 7.973 | 7.973 | 7.973 | 7.973 | 0（預算全是稅） |
| 2 | 46.567 | 46.021 | 43.635 | 43.635 | ~300s |
| 3 | 33.380 | 33.380 | 30.478 | 29.316 | 600s（仍在改善） |
| 4 | 61.263 | 61.263 | 61.263 | **53.785** | 600s（600s 才破解，−7.48M） |

## 決策：出貨預設 = 裸呼叫時自帶 time-budget 600s（F1-flip）

理由：
1. **Σrank 對 budget 單調下降**——runtime penalty 已入帳，600s 仍最佳。
2. B2 fold 結構保證 budget 永不損害 value（phase-1 legs 恆在折疊集內）。
3. 主辦方以無參數方式呼叫 → 預設值就是提交行為；測試/開發呼叫（帶明確
   `--iters` / `--time-budget`）語義完全不變。
4. 2h/8 案限制：8×~600s ≈ 4800s < 7200s，餘裕 ~33%；anytime 輸出護底。
5. 統一 600s 而非逐案自適應：膝點差異真實存在（上表），但自適應要靠 5 個
   公開案調參，對 3 個未公開案是過擬合風險；隱藏案偏大 → 更接近 case3/4
   的 600s 膝點。留作 final 階段的可能精緻化。

## 量測品質備註

- budget-0 列與 G3（B7 A/B）/G4（edge 量測）併行，runtime 欄受 CPU 爭用
  污染（rank 高估 <0.1M 級）；checker 欄為決定性數值不受影響。600 列在
  併行工作結束後執行，較乾淨。結論對污染穩健（120.13M vs 115.79M 的差距
  遠大於污染量級）。
- case1 在任何預算下 checker 不動（7.973M）——其最優在 phase-1 早期即達；
  600s 預設對它是 ~0.72M 的 rank 稅，已計入 Σ 仍勝。改善型 early-stop
  （連續無改善輪即停）可回收此稅，deadline 前不動。

## B7 重量測附記（G3，同日）

V5 下 B7 貢獻 Σchecker −1.64%（hybrid）/−1.37%（bstar），Pareto-safe；
價值來源已從 case4/hybrid（現為 0）遷移到 **case3**（−7.12%/−5.69%）。
「−0.1~−0.3%/case」出處更正：那是 B3 周邊 margin 的估計，非 B7 的宣稱。
維持 B7 於 default。edge 軟約束（G4）量測結論：0/5 案採用違規、不開
registry leg（`b632e03` 落地為 default-off 機制 + rescorer γ 邊罰無條件化）。
