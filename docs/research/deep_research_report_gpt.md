# Early Floorplanning with Global Route 深度研究與解法建議

## 問題本質與競賽解讀

從官方題目頁可以確認，這題是 2026 年 Problem E「Early Floorplanning with Global Route」，題目文件在 2026-04-14 更新、Q&A 在 2026-05-09 更新；競賽也明確要求作品可在 Linux 上執行，編譯器以 gcc / g++ 為主。公開 testcase 表則顯示，輸入至少包含固定外框、混合 block 型別（edge-constrained block、hard macro、soft block with aspect-ratio range）、連線矩陣，以及一個 α 參數。這代表它不是單純「把 block 排進 outline 內」的 floorplanning，而是 **帶有幾何限制、連線拓樸、以及早期繞線可行性 proxy 的聯合最佳化問題**。citeturn29view1turn29view0turn29view2

就解題角度來看，這類題目的核心通常不是做出最終 signoff 級路由，而是要在 floorplanning 階段盡量保留後續 global routing 的可行性，讓 cost ranking 對真實繞線風險有足夠辨識力。這也是為什麼近年最相關的研究，不是傳統純 HPWL / area floorplanning，而是 **routability-aware macro placement、early routability assessment、feedthrough-aware floorplanning、pin-assignment-aware floorplanning，以及用更高精度 global router 做 calibration**。citeturn33view3turn31view6turn31view7turn31view4turn39search1

如果直接用一句話總結這題：**它本質上是「有限時間內，找一個 legal、可擴充、且對 routing proxy 友善的 topological floorplan 搜尋器」**。在這個前提下，最重要的不是把單一子模組做到極致，而是把「表示法 + 增量 cost 評估 + 平行搜尋 + 少量高精度驗證」整合成一個穩定的系統。citeturn34view0turn38view2turn38view1

## 近十二年研究脈絡

這個方向的第一條主線，是 **routability-driven macro placement**。2014 年的 *Routability-Driven Blockage-Aware Macro Placement* 已經把 macro placement 從純幾何 packing 推向「wirelength + congestion」共同考量；2019 年的 *Routability-Driven Macro Placement with Embedded CNN-Based Prediction Model* 更進一步把 routability predictor 嵌進 simulated annealing，直接以後段 DRC/routability proxy 來引導 macro placement。這些工作對你這題最有價值的啟發是：**floorplanner 的 move acceptance 不該只看 HPWL / area，而要看 routing 風險 proxy**。citeturn10search0turn31view7

第二條主線，是 **early routing / early routability assessment**。2018 年的 *STAIRoute* 明確主張在 floorplanning 後就做 early global routing，估 routability、wirelength、vias 與 congestion；同年的 *HGR* 則把 over-the-block routing 納入 generalized model，並在其報告中指出，相較於 STAIRoute，平均可進一步降低 netlength、via count 與 congestion，且在 industrial flow 中帶來約 3 倍 runtime boost。這條線直接對應你題目的「Early Floorplanning with Global Route」命名精神。citeturn33view3turn31view6

第三條主線，是 **hierarchy / dataflow aware macro planning**。2019 年的 *HiDaP* 利用 RTL hierarchy 與 dataflow affinity 做 multilevel macro placement；2022 年的 *RTL-MP* 和 2023 年的 *Hier-RTLMP* 則把這個想法推向大型 IP block，強調單層 peripheral placement 對數百 macro 的大型設計已不再夠用，必須利用 hierarchy 和 dataflow 做 multilevel planning。對競賽題而言，這些工作最值得借的是 **多層分群、先粗後細、先 topology 後 legalization 的工程策略**。citeturn27search1turn27search0turn33view2turn38view0

第四條主線，是 **近兩年非常貼題的 feedthrough / pin-aware floorplanning**。2024 年的 *PARSAC* 重新強調 constraints-aware simulated annealing 的價值，指出把 hard constraints 粗暴丟進 SA objective 容易得到 sub-optimal 甚至 illegal 解，因此提出 CA-SA 與 massively parallel floorplanner；2025 年的 *FTAFP* 明確把 feedthrough-aware floorplanning 作為研究目標，並提出 SCB-Tree 與 two-phase SA；2025 年的 *Flora* 再把 floorplanning 跟後續模組內 component placement 串起來；同年的 *Piano* 則把 pin assignment 一起納入 floorplanning，報告中同時改善 HPWL、feedthrough wirelength、feedthrough modules 與 unplaced pins。若你的更新版題目真的把 inter-block route / guiding point / channel 類成本視為核心，這一串工作就是最近、最接近的文獻群。citeturn34view0turn31view4turn34view1turn39search1

最後一條主線，是 **learning-based floorplanning / routing**。開源的 AlphaChip（Circuit Training）代表 RL-flow；FlexPlanner 代表 hybrid action-space 的 floorplanning RL；而 HubRouter、DSBRouter 代表 routing 端的深度學習路線。不過，2026 版 *An Updated Assessment of Reinforcement Learning for Macro Placement* 仍明確指出：更強的多執行緒 SA baseline 依然非常具有競爭力，而且 AlphaChip 在 scalability 和 pre-training 方面仍有未完全釐清的問題。這對競賽的含義很直接：**ML 很適合當 warm start 或 surrogate，但不應該是你唯一、也不該是第一個押注的主解法**。citeturn31view9turn31view10turn37view2turn33view4

## 可直接利用的 code repo 與工具鏈

就工程可落地性而言，entity["organization","OpenROAD","open-source eda"] 是最值得直接借力的基底。它現在已經公開提供 hierarchical macro placement、routability-driven global placement，以及 global routing 三段能力；其中 global placement 可以用 RUDY 做快速 congestion estimation，也可以切到更精準、但更慢的 FastRoute / grt。這非常適合拿來做 **外圈驗證器**，而不是每一步 move 都重跑。citeturn38view0turn38view2turn38view1

你最應該先 bookmark 的入口，是：urlProblem E 官方題目頁turn29view1、url官方 testcase 表turn29view0、urlOpenROAD Macro Placement 文件turn30view0、urlOpenROAD Global Placement 文件turn30view2、以及 urlOpenROAD Global Routing 文件turn30view1。這五個入口基本上就足夠把題目需求、可借用的 placer / router，以及 routability-driven optimization 的實作邏輯串起來。citeturn29view1turn29view0turn30view0turn30view2turn30view1

若你要找 **可直接跑、可改、可重現的 floorplanning baseline**，最好的出發點是 urlPARSAC repoturn36view5。它是 paper 對應的公開實作，強調 constraints-aware SA，並且 repo 中保留了 configs、problem specs 與實驗入口。不過它目前是 archived 狀態，所以你應把它當「可 fork 的 baseline」，不是會持續有人維護的現成工具。citeturn36view5turn34view0

若你要找 **高精度或高速度的 global routing verifier**，則首選是 urlCUGR repoturn31view12 與 urlInstantGR repoturn37view4。CUGR 的定位是 detailed-routability-driven global router，品質判準更接近最後 detailed routing 結果；InstantGR 則把 GPU 平行化導入 global routing，論文也明講它適合邏輯綜合與 physical planning 等較早階段的 routability estimation。對你的硬體來說，這兩者剛好對應「CPU 高精度校準」與「L4 GPU 快速驗證」兩個角色。citeturn31view12turn31view13turn31view11turn37view4

若你想留一條 **學習式 warm start / surrogate** 的備案，最值得看的不是直接抄 end-to-end RL，而是把資料、baseline、可重現流程先補齊：urlAlphaChip repoturn31view9、urlMacroPlacement repoturn37view5、urlFloorSet repoturn37view6、以及 urlEDA-AI repoturn37view2。其中 FloorSet 提供 2M floorplan circuits 的大規模資料集；MacroPlacement 提供公開 testcases、flow、baseline 與 post-route reproduction；EDA-AI 則把 FlexPlanner、HubRouter、DSBRouter 等近期方法整理在一個 repo 裡。這一組更像是「研究與 warm-start 加速包」，不是直接能拿去交競賽 binary 的主框架。citeturn37view6turn37view1turn31view9turn37view2

## 我最推薦的前三個解法

### 第一名方案

我最推薦的是 **約束感知平行 simulated annealing，加上顯式 early-route proxy，再用少量高精度 global router 做外圈校準**。這個方案同時吃到 PARSAC 的 CA-SA 思路、STAIRoute / HGR 的 early global routing 視角，以及 FTAFP / Flora / Piano 在 feedthrough、pin assignment、whitespace、fixed-outline 這些面向上的新 cost 設計。它不是最炫的，但對 hidden cases 最穩，也最符合「競賽 binary 必須跑得穩、可 debug、可解釋」這件事。citeturn34view0turn33view3turn31view6turn31view4turn34view1turn39search1

我會把這個方案做成五層：  
- **表示法**：以 B\*-tree / sequence pair / SCB-tree 類表示法擇一為主，必要時保留 soft block shape assignment。  
- **增量 cost**：至少同時評估 outline/overlap/edge constraint、HPWL、soft-block aspect legality、channel / congestion proxy、關鍵 net 距離，以及你題目裡最重要的 route-related penalty。  
- **搜尋核心**：multi-start SA + targeted perturbation + destroy-and-repair，並保留 top-K elite solutions。  
- **低頻 verifier**：每隔固定步數，用 OpenROAD grt 或 InstantGR / CUGR 去校正 proxy 漂移。  
- **最終選解**：不要只交 global-best HPWL，而是交 proxy 最穩且高精度 verifier 分數也好的解。citeturn38view2turn38view1turn31view12turn31view11

**優點**  
它對訓練資料依賴最低、對 hidden test 分布轉移最不敏感，而且完全吃得到你 64 vCPU Linux server 的平行搜尋優勢。另一方面，OpenROAD / CUGR / InstantGR 都能拿來做 calibration，不需要自己從零寫一顆成熟 global router。citeturn38view0turn38view2turn31view12turn37view4

**缺點**  
真正難的不是 SA 本身，而是「proxy 怎麼做得夠便宜、又能正確排序候選解」。如果 proxy 跟真實 route 風險脫鉤，再好的搜尋也會被帶歪。另一個風險是 high-fidelity verifier 太頻繁，會把 runtime 吃掉。citeturn33view3turn31view6turn38view2

**實作難度與時間資源**  
我認為這是 **中高難度，但最高報酬率** 的方案。以你目前時間點來看，單人做法的合理預估是：  
- 約一週做完 parser、legalizer、基本 SA、HPWL / area / constraint cost。  
- 再一到兩週把 route proxy、parallel multi-start、elite archive、top-K replay 補齊。  
- 最後一到兩週做 verifier 校準、參數 sweep、Linux 打包與 regression。  

也就是說，**三到五週能有很像樣的版本**；在 2026-06-12 alpha submission 前先交一個穩定 classical baseline，非常可行。citeturn29view2

### 第二名方案

第二名我推薦 **hierarchical / dataflow-aware multilevel floorplanning，最後再做 route-aware refinement**。如果公開與隱藏 testcase 的 block 數偏多，這個方案往往比純平面 SA 更穩，因為它會先把 search space 壓到「拓樸合理的 cluster arrangement」，再在 cluster 內做局部優化。這條路最值得借的研究是 HiDaP、RTL-MP 與 Hier-RTLMP。citeturn27search1turn27search0turn33view2turn38view0

我建議的具體做法是：先依 connectivity matrix 建 hypergraph clustering，對高耦合 block 形成 coarse cluster；先在 coarse 層級求 topology，再往下展開做 local SA / legalization；每次展開後再用簡化 route proxy 檢查 cluster 之間的 corridor 與 channel 是否過窄。這條線的強項是 **把 combinational explosion 變成「先粗後細」的搜尋**，非常適合 block 數量或 constraint 數量增加時維持穩定性。citeturn27search1turn33view2turn38view0

**優點**  
擴充性最好，對大型案例比純 SA 更容易收斂；而且 OpenROAD 已經把 Hier-RTLMP 放進可用工具鏈，代表你不是從零開始。citeturn38view0

**缺點**  
如果題目沒有明確 hierarchy 可用，只剩平面 block + connection matrix，那你得自己做 clustering，做不好反而會把後面 refinement 綁死。其次，這條路常常 wirelength 很漂亮，但最後輸在某些局部 channel / route hotspot。citeturn27search1turn33view2

**實作難度與時間資源**  
我會評為 **中等**。如果你借 OpenROAD / Hier-RTLMP 的想法重寫一個簡化版，而不是直接改 OpenROAD 本體，大概 **兩到四週** 能做出實用原型。它很適合當成第一名方案的「前置候選解產生器」：先給出 20~50 個 topology 好的起始點，再把它們丟進方案一做深度 refinement。  

### 第三名方案

第三名才是 **learning-augmented warm start / surrogate ranking，再接 classical refinement**。我不建議你把 end-to-end RL 或 end-to-end neural routing 當主解法，但我認為它很適合扮演兩個角色：**幫你選起始解**，以及 **幫你近似 route-related cost**。最值得吸收的點包括：2019 年 CNN routability predictor 內嵌到 SA 的作法、AlphaChip / Circuit Training 的 candidate generation、FlexPlanner 的 transferable planning，以及 FloorSet / MacroPlacement 提供的資料與重現基礎。citeturn31view7turn31view9turn31view10turn37view6turn37view1

這個方案比較合理的版本不是「訓一個模型直接輸出最終 floorplan」，而是：先用方案一或二離線產生大量候選解與 verifier 分數，然後訓一個小型 GNN / CNN ranking model，去預測哪類 move、哪類 seed 比較值得深搜。這樣做可以把 L4 GPU 用在真正有價值的地方，而且不會讓整個交付風險壓在訓練穩定性上。citeturn31view7turn37view6turn37view1

**優點**  
如果你在 alpha 後累積到足夠多的自產生案例，它有機會明顯改善 search efficiency，尤其是在 seed selection、move pruning、以及 top-K re-ranking。FlexPlanner、HubRouter、DSBRouter 也提供了不錯的工程參考。citeturn37view2turn37view3

**缺點**  
風險最大。更新版 RL assessment 已經提醒，強 SA baseline 很難被穩定打掉，而且 RL / pretraining 的 reproducibility 仍是問題。對競賽來說，ML 如果沒有穩定提升，最後反而只會耗掉你最寶貴的調參時間。citeturn33view4

**實作難度與時間資源**  
我會評為 **高**。如果從零開始，單人通常要 **四到八週** 才可能有足夠說服力的結果；更務實的作法，是把它當成 final 之前可選的加速器，而不是 alpha 前的主體。  

## 依你現有硬體的實作策略

你的 Mac mini M4 16GB 很適合做 parser、可視化、單元測試、小案例 debug；但因為競賽最後要跑 Linux / gcc / g++，**從第一週開始就應該把主執行環境放在遠端 Linux server**，避免最後移植時踩雷。這點我會很強調，因為官方規範本身就把提交環境鎖在 Linux。citeturn29view1

在你提供的遠端機器資訊中，最有價值的是 **64 vCPU + 1 張 L4**。如果照我的第一名方案來配置，我會這樣分工：  
- **CPU 端**：跑 16~48 條平行 SA / LNS 搜尋鏈，每條鏈共享同一個 parser 與 cost evaluator，但使用不同 seed、溫度表與 perturbation 配比。  
- **GPU 端**：只做兩件事，其一是 InstantGR 類快速 global routing 驗證；其二是若你後期要做 surrogate ranking，才拿來訓小模型。  
- **I/O 與快取**：保留 best-K floorplans 與其 verifier 結果，做 replay 與 proxy calibration。citeturn31view11turn37view4turn36view5

如果你問我「從今天開始，最實際的時程怎麼排」，我會這樣排：  
- **現在到五月底**：完成方案一的 baseline，至少做到 parser、legalizer、constraint cost、HPWL、簡化 congestion / channel proxy、平行 multi-start。  
- **六月初到 alpha 前**：接上 OpenROAD grt 或簡化 verifier，建立 top-K replay 與參數 sweep。  
- **alpha 到 beta 之間**：加入方案二的 multilevel clustering，讓起始解品質更好。  
- **beta 到 final 之間**：只有在 baseline 已經很穩時，才上方案三的小型 surrogate / warm-start。官方時程顯示 alpha 在 2026-06-12、beta 在 2026-07-24、final 在 2026-08-21，所以這樣的節奏是來得及的。citeturn29view2

如果只允許我給一個最直接的決策，那就是：**先做一顆很強的 classical engine，再把 ML 當加分器，不要反過來。** 文獻與近年的公開評估幾乎都在支持這個排序。citeturn34view0turn33view4

## 開放問題與限制

公開可直接機器讀取的官方來源，目前我能高信心確認的是題目入口與 testcase 結構；完整 scoring 係數、幾何邊界案例、以及 Q&A 中某些 corner-case 定義，仍應以最新的官方 PDF / Q&A 為最終準則。因此，實作時不要太早把某個 proxy 係數寫死，先保留可調參數與 ablation 開關會更安全。citeturn29view1turn29view0

另外，我在高可信來源裡能直接確認到的公開實作，主要是 OpenROAD、PARSAC、CUGR / InstantGR、AlphaChip / MacroPlacement、FloorSet / EDA-AI 這一組；像 FTAFP、Flora、Piano 這些最貼題的 2025 方向，我能確認到論文與方法描述，但沒有同等成熟、可直接拿來交 binary 的官方公開工具鏈。因此，它們更適合提供 **cost 設計與 perturbation 設計靈感**，而不是當現成解。citeturn31view4turn34view1turn39search1turn36view5turn31view12turn37view4turn37view1turn37view6

綜合來看，我的最終排序不變：**第一名是 constraints-aware parallel SA + route proxy + verifier；第二名是 multilevel hierarchy/dataflow floorplanning；第三名是 learning-augmented warm-start / surrogate。** 如果你的目標是「最有機會在這個競賽拿到穩定高分」，我會把 70% 精力壓在第一名、20% 壓在第二名、10% 留給第三名。citeturn34view0turn33view3turn31view6turn33view2turn33view4