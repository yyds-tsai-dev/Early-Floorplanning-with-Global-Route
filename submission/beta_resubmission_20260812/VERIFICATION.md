# Beta Resubmission — 2026-08-12

## 問題(官方 2026-08-12 email + `docs/official/beta_test/cade0002.7z` 回饋包)

7/31 提交的 binary 在官方測試環境全部 8 個 case loader-fail:
`GLIBC_2.29/2.34/2.38 not found`、`GLIBCXX_3.4.29/3.4.32 not found` → `NO_OUTPUT_CFG`,
根因 `GLIBCXX_TOO_NEW`。官方環境為 `/lib64` 舊系(glibc < 2.29,RHEL/CentOS 8 世代,
glibc 2.28);原 binary 在 Ubuntu 24.04(GCC 13.3 / glibc 2.39)動態連結。
官方回饋頁明示修法:「改用靜態連結重新編譯(`-static`)」。
harness `exec_status: CORRECT` — 呼叫方式無誤,純連結問題。

## 修復

原始碼零變更,自 `fable5` @ `e28dcaf`(與 7/31 提交同一版)重新連結:

```
g++ -std=c++17 -O2 -Wall -Wextra -pedantic -static -static-libgcc -static-libstdc++ \
    src/*.cpp -o EarlyFloorplanning_with_GlobalRoute
```

產物:`Beta_Resubmission/EarlyFloorplanning_with_GlobalRoute`
- `file`: ELF 64-bit x86-64, **statically linked**, for GNU/Linux 3.2.0
- `ldd`: not a dynamic executable;`readelf -d`: no dynamic section
- SHA256: 見 `SHA256SUMS.txt`
- 編譯 warning-clean(-Wall -Wextra -pedantic)

## 驗證證據

1. **本機 smoke(Ubuntu 24.04)**:官方 0727 五個公開 case,
   `--iters 300 --seed 1` 全部 `validation=ok, exit=0`, 0 open routes, .cfg 正常寫出。
2. **舊 glibc 真實環境(docker rockylinux:8, glibc 2.28)**:case0
   `--iters 200 --seed 1` → `validation=ok, exit=0`, .cfg 寫出。
   官方環境連 GLIBC_2.29 都沒有;Rocky 8 的 glibc 2.28 與其特徵一致。
3. **跨環境 determinism**:同參數在 glibc 2.28(容器)與 2.39(本機)產出
   **bit-identical** 的 .cfg(score=4822899)。
4. **Harness 式裸呼叫演練**:`./EarlyFloorplanning_with_GlobalRoute "2026 ICCAD_case.case0.csv"`
   → `exit=0`,輸出同名 `2026 ICCAD_case.case0.cfg`,600s budget 正常啟動
   (`budget_phase2=yes`, `legs_run=1754`),`validation=ok`, 0 open routes,
   score=4305930(2026-08-12)。

## Codex 獨立審查(2026-08-12, session 019ff645-41c2-7b02-bb04-782d72727895)

整體 **PASS**,四項全過:
1. ELF/static:no INTERP/DYNAMIC/NEEDED、`readelf -V` 無版本需求、strings 無 GLIBC_* 符號;
   ISA 需求 x86-64-baseline(非 AVX-512-only)。**獨立重建 e28cdaf→SHA256 與打包檔
   byte-for-byte 相同**(可重現建置)。
2. 檔名/打包:資料夾內恰一檔,檔名逐字元符合 A26/A30,mode 755。
3. Smoke run:exit=0、validation=ok、.cfg 含 OUTLINE/BLOCK/CHANNEL/PATH。
4. Harness 行為:輸出同名 .cfg(main.cpp 副檔名替換)、只寫 cwd(io.cpp .tmp+atomic rename)、
   無 dlopen/env/locale/網路/絕對路徑。裸呼叫 15s probe 確認 600s budget 啟動且
   anytime .cfg 已存在。

殘餘風險(已知、非本次修復範圍):600s budget 為 F1 設計決策,phase-1 legs 無硬性
deadline,官方硬體若極慢有超時可能;anytime 輸出需第一個 leg 完成後才存在
(case0 於 15s 內已產出)。8 case × 600s = 80 min < 2h 總限。

## 提交規格依據(E_QA_20260727.pdf)

- A26/A30:binary 檔名必須為 `EarlyFloorplanning_with_GlobalRoute` ✓
- A27:input 由 command-line 傳入,output 為「input 檔名」.cfg ✓(io.cpp 預設行為)
- A28/A37:只需提交編譯後 binary,不需原始碼/Makefile ✓
- 上傳位置:Google Drive 團隊資料夾內 **"Beta Resubmission"** 資料夾,
  期限 **2026-08-13 23:59 (GMT+8)**。上傳 `Beta_Resubmission/` 資料夾內的 binary。
