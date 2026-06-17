# Early Floorplanning with Global Route 競賽研究報告

## 題目本質與技術難點

這題不是一般的「先排版、再繞線」；它本質上是**在 very-early stage 同時做 fixed-outline floorplanning、soft block shaping、channel reservation、以及可穿越 soft block 的 global routing**。題目明確要求所有 block 都必須是矩形、不能重疊、必須放在 outline 內；routing 不能 open；channel 與 feedthrough overflow 不是直接 fail，而是依程度扣分；而 α 需要做成可調參數，預設值是 1。再加上 hidden case 的 overall block 數量小於 50、官方測試環境不支援 GPU、全部 testcase 總 runtime 上限 2 小時，這題明顯偏向**中小規模、強約束、CPU-only 的 combinatorial optimization**，而不是大規模 GPU placement。 fileciteturn0file0 fileciteturn0file1 citeturn29view0

更關鍵的是，題目的 routing model 是**幾何上客製化的**。channel 不是先給 grid，而是由所有 block 的左右邊界切出 vertical strips，再在 strip 內取未被 block 蓋住的矩形；wirelength 也不是單純 block-center HPWL，而是經由 overlap segment 的 guiding point 逐段加總。soft block 又可作 feedthrough，但 feedthrough 會依 through-net 數量和 conversion table 增加面積；因此 soft block 其實是一種**會膨脹的 routing resource**，而不是單純可變形模組。這些設定讓 placement 與 routing 彼此高度耦合：你移動一個 block，channel 拓撲、soft block 成本、可行路徑、最終 outline 可能都一起變。 fileciteturn0file0

從演算法角度看，這題最重要的結構紅利有兩個。第一，輸入是 **block-to-block connection matrix**，不是上萬條獨立 pin-level nets；你真正要解的是**bundle/commodity routing**，可以把同一對 block 之間的需求當成一個帶權 commodity，而不是逐 net maze routing。第二，n < 50 代表你可以把大量時間花在**更強的 cost model 與多起點搜尋**，而不是追求 GPU 大吞吐量。這也是我後面會把「約束感知 SA + 內嵌 router」排在第一的核心原因。 fileciteturn0file0 fileciteturn0file1

## 文獻脈絡與最接近的研究方向

如果把相關研究按「跟這題的接近程度」排序，最底層的經典基礎是 fixed-outline floorplanning。Adya 與 Markov 的 *Fixed-outline Floorplanning: Enabling Hierarchical Design* 指出 fixed-outline 比傳統 outline-free floorplanning 更難，並用 slack-based moves 改善 simulated annealing；這篇很重要，因為你的 edge block 與 soft block shaping 都屬於「不能只看 area/HPWL」的 constrained floorplanning。Murata 的 sequence-pair 與 Chen/Chang 的 B\*-tree + Fast-SA 則是兩套最經典、最可用的拓撲表示；尤其 Chen/Chang 那篇不只做 fixed-outline，還直接處理 **bus-driven floorplanning**，也就是在 floorplanning 階段就把 interconnect constraint 納進來，和本題精神非常接近。 citeturn18view0turn5view0

再往前一步，就是 routability-driven floorplanning 與 early global routing。Sham 與 Young 在 2003 年的 routability-driven floorplanner，已經把 congestion estimation 與 two-phase simulated annealing 放進 floorplanning，並證明兩階段 SA 可以顯著縮短 runtime；RUDY 則成為日後大量 routability-driven placement 的快速 congestion proxy。OpenROAD 的 global placement 文檔也清楚寫到 routability-driven placement 會在每輪用 RUDY 估計 congestion，對擁塞區塊做 inflation。這一脈對你最有價值的啟發是：**不要在 SA 的每一步都跑完整精算 router；先用便宜 proxy 粗篩，再把精算 router 放到後期或 top-K candidate 上。** citeturn23view2turn23view0turn7search0turn28view0

和本題更近的一支，是「floorplan level 的 early global routing」。STAIRoute 直接在 floorplan 階段做 monotone staircase routing，報告 100% routing completion、無 over-congestion；後續的 HGR 認為 floorplan-level routing 若允許 over-the-block 資源，會比只看 staircase boundary 更貼近 post-placement global routing，並在 wirelength、via、congestion 上更合理。對你的題目來說，這一脈的重點不是照搬演算法，而是借它的觀念：**在 floorplanning 階段直接評估 routability 與 route cost，會比單純用 HPWL 更可靠。** citeturn22view3turn22view1turn22view0

真正和本題「最像」的，是近兩年的 feedthrough-aware 與 pin-assignment-aware floorplanning。2025 的 FTAFP 把 feedthrough optimization 提前到 hierarchical floorplanning，提出 feedthrough estimation model、SCB-Tree、slack-aware packing、two-phase SA；這跟你的題目幾乎同一個味道，只是題目中的 feedthrough 成本被寫得更具體。2025 的 *Piano* 更進一步，把 graph shortest path、pin assignment、feedthrough/unplaced-pin 指標一起拉進 floorplanning 迴圈，並明確指出只看 HPWL 會漏掉後續實作代價。對這題而言，FTAFP 給你**表示法與 cost 設計方向**，Piano 給你**圖式局部優化與 shortest-path 評估**的方向。 citeturn12view1turn12view3turn26view0

最後才是 analytical / ML 路線。UFO、PeF、以及較新的 SDP floorplanning，證明 analytical global floorplanning + legalization 在大規模 fixed-outline 問題上很有競爭力；DREAMPlace、AutoDMP、AlphaChip / Circuit Training 則代表 GPU placement 或 RL macro placement 的現代主線。但這類方法大多針對**大規模 macro/mixed-size placement**，而且 hard constraints、feedthrough area inflation、custom path output 不是它們原生擅長的。AutoDMP 自己就寫到 RL/ML 的 training/fine-tuning 往往要多天與上千 CPU；DREAMPlace 雖然支援 CPU-only 與 macro placement，但它的強項仍是 analytic global placement，不是這種 geometry-defined channel + feedthrough rule 的 contest。 citeturn20search1turn17view0turn5view2turn21search0turn4view10turn4view11

## 可直接借鑑的公開程式與工具鏈

如果只看「可直接拿來拆零件」的開源資源，我認為最有用的不是單一 repo，而是一個三層工具箱。

第一層是 floorplanning / constraint handling。urlPARSAC repoturn10search0 已經實作了 boundary constraints、grouping、preplaced blocks、fixed outline、parallel SA，且 repo 直接給出 API 和 hard pre-placement / fixed-outline 的使用方式；urlFloorSet repoturn10search24 則提供了一個很新的 constrained floorplanning baseline，裡面甚至有 ICCAD 2026 contest 用的 `optimizer_template.py` B\*-tree SA baseline；urlCorblivar repoturn25search2 雖然是 3D IC floorplanning，但它對「large-scale interconnect planning + SA cost scheduling」很有參考價值。這三個 repo 都適合拿來學 representation、move set、constraint encoding，而不是整包直接套上題目。 citeturn11view0turn24view0turn27view0

第二層是 routing / cost evaluation。urlFastRoute repoturn3search1 提供經典 rip-up and reroute global router 的結構；urlCUGR repoturn3search2 與 urlCUGR 2.0 repoturn3search6 代表近年的 detailed-routability-driven global routing，尤其 CUGR 2.0 README 清楚標示它把 quality 定義為最終 detailed routing 結果，而不是單看 overflow / wirelength；urlOpenROAD repoturn3search3 和它的 urlglobal placement 文檔turn0search7 也很值得讀，因為它把 RePlAce、RUDY、routability-driven inflation、以及 global routing heatmap 都串在同一流程裡。這些工具不能直接讀你的 contest format，但它們非常適合借鑑 **pricing、reroute、capacity update、與 proxy-to-exact 的迭代架構**。 citeturn4view8turn4view7turn14view0turn4view9turn28view0

第三層是 modern macro placement / ML baseline。urlDREAMPlace repoturn8search2 值得參考它的 macro mode 與 legalization/min-cost-flow refinement；urlMacroPlacement repoturn2search14 提供 open、可重現的 SA 與 Circuit Training baseline；urlAlphaChip repoturn21search0 則是 RL floorplanning 的官方開源框架。這一層我不建議當主解，但如果你想做「seed generator」或「proxy scorer」時，它們很有用。 citeturn17view0turn4view10turn21search0

我沒有找到**直接對應這一題輸入/輸出格式與評分方式**的公開 solver。公開程式幾乎都只覆蓋「floorplanning」或「LEF/DEF global routing」其中一邊，而你的題目把兩者綁在一起，且 channel 生成規則、soft block FT area rule、PATH grammar 都是 contest-specific。這也代表競賽的分差大概率會來自**整合能力**，而不是純粹套一個現成 framework。 citeturn11view0turn14view0turn17view0

## 最值得做的三種解法

### 約束感知 SA 主解器

我把這條路線排第一。核心做法是用 **B\*-tree 或 SCB-tree 類型的拓撲表示**做 floorplanning，用 **PARSAC 式 constraints-fixing moves** 處理 edge/boundary legality，再在 cost function 內嵌一個**bundle-aware router**。floorplanning 端借 Chen/Chang 的 B\*-tree + Fast-SA、Adya/Markov 的 slack move、PARSAC 的 anchored/preplaced handling；routing 端則借 RUDY/early-routing literature 的 proxy-to-exact 思想，以及 FastRoute/CUGR 類 router 的 pricing + reroute 框架。對本題最重要的改造是：把 connection matrix 的每個非零 block pair 視為一個 commodity，讓 router 在「block + channel」形成的 rectangle-edge graph 上找 path，而 soft block 的 feedthrough 就是可付費使用的 over-the-block 資源。 citeturn18view0turn5view0turn9view0turn11view0turn7search0turn13view0turn12view0

實作上，我建議做成**兩階段**。前段 SA 的 cost 只用 outline area、weighted block-pair distance、edge-block/AR penalty、以及一個便宜的 congestion proxy；後段只對較好的 candidate 跑精算 router，精算 outline + α·route wirelength + overflow penalty。這和 2003 routability-driven floorplanner 的 two-phase SA、以及 FTAFP 的 two-phase SA 都一致。真正的 scoring 關鍵在第二段：你要讓 router 對 channel 的水平/垂直容量分開記帳，並把 soft block 的 FT area inflation 視為 piecewise cost。這不是教科書式 global routing，但因為 n < 50，geometry graph 其實不會大到不可做。 citeturn23view0turn23view2turn12view1turn12view3turn22view0

它最強的地方，是**和題目對齊度最高**。這題最終不是比 abstract HPWL，而是比「合法性 + route-aware cost」；而這套方法可以把 legality、channel、FT、outline 都直接放進搜尋迴圈。它也最符合你的硬體：Mac mini 很適合本地 parser/debug；64-core Xeon server 極適合多起點 SA；GPU 幾乎不是必要條件。缺點則是工程量不小：你需要自己寫 channel extractor、route graph、capacity accounting、以及 SA 與 router 的 coupling。以競賽實戰來看，這條路如果你已有 C++ 基礎，大概 **2–4 週能做出有競爭力的版本，4–7 週能做出成熟版**；記憶體需求不高，但你會很花時間在 cost calibration 與 corner case parser 上。這是我唯一會建議你「當主軸 all-in」的方案。 fileciteturn0file0 fileciteturn0file1

### 分解式精修器

我把這條路線排第二。它不是最好的主解，但非常適合做**endgame polisher**。概念是先用第一名方法或其他 heuristic 生成一批好的 floorplan topology / adjacency pattern，再用 **CP-SAT / MILP / LNS 類 exact-or-near-exact 方法**精修 block 座標、soft block 尺寸、以及少量 route choice。原因很簡單：這題 fixed-outline 又有多種 hard constraints，本來就比傳統 floorplanning 難；但 hidden case 的 block 數又小於 50，代表**在 top-K 候選解附近做 exact polishing 是有機會的**。Adya/Markov 早就指出 fixed-outline floorplanning 比 classical 更難；UFO、PeF、以及近年的 analytical/global floorplanning 也都走「先全局、再 legalization」的雙層流程。你的 exact polisher 本質上可以視為把這個思路推到更激進：只在小 neighbourhood 內求更接近最優的 legal solution。 citeturn18view0turn20search1turn26view0

這條路線最適合處理兩種事情。第一種是**最後一哩的 legality tightening**：例如 edge block 貼邊、soft block aspect ratio、少量 block 的 x/y 細調整。第二種是**容量卡邊緣時的局部 route-pattern 優化**：只對最 critical 的 commodities 允許多條候選路徑，讓 solver 在小圖上選路。你不應該一開始就嘗試做 full joint MILP，因為太容易炸；正確玩法是把它當成 top-20 或 top-50 candidate 的 polish stage，用 OR-Tools CP-SAT 或自寫 LNS。這樣它不必接手全域搜尋，只負責把 heuristic 解磨尖。 fileciteturn0file0 fileciteturn0file1

它的優點是**上限高、對 hidden case 穩定性好、很有機會把小面積或小 overflow 再壓下去**。缺點是 solver engineering 成本高，而且若你沒有控制好 neighbourhood，2 小時總 runtime 很容易被吃掉。因此我不建議把它當唯一主解。我對它的評價是：**非常值得做，但只能當第二層**。若你現在才開始，建一個可用的 polish 版本，大概需要 **1.5–3 週**；做得夠穩，大概 **3–5 週**。CPU-only 即可，64-core server 很適合平行跑不同 neighbourhood / candidate；L4 基本上用不到。這條路線最好的價值，不是取代第一名，而是幫第一名多拿最後 3–10% 的分數。 

### 圖式增量優化器

我把這條路線排第三。這是比較「輕量但有針對性」的方案：先拿任一 baseline floorplan，然後做 **FTAFP / Piano 風格的 graph-based incremental optimization**。FTAFP 已經證明 feedthrough 可以在 floorplanning 階段用估計模型 + two-phase SA 提前處理；Piano 更直接把 geometric graph、shortest path、feedthrough / unplaced-pin 指標、以及 local operators 放進一個 incremental optimizer。把這個想法轉到你的題目上，就是：先生成一個合法 floorplan，再建 adjacency graph，找出最重的 feedthrough path 或最擁塞的 channel，然後對相關 block 做 swap、拉近、boundary-adjust、aspect-ratio change、或 whitespace redistribution。它本質上不是從零開始解全域問題，而是對一個已有解做**結構性 repair**。 citeturn12view1turn12view3turn26view0

它最大的好處是**開發快、回報高、很適合當競賽中的「第二顆引擎」**。如果你已經有一個普通的 B\*-tree SA baseline，但還沒空把 exact router 深度嵌進 SA，這種 graph-based local optimizer 非常值得先做。你可以讓它每次只看 top few critical nets / block pairs，然後對路徑上的 block 做 targeted move，而不是盲目地全域擾動。這類方法通常比純 SA 更容易解讀，也更容易 debug。它的缺點則是相反：**它對初始解品質很敏感，且缺乏全域重排能力**。如果起點很差，它常常只能把壞解修成普通解，而不太容易自己跳到真正的好拓撲。 citeturn26view0

所以我對它的建議是：如果你時間有限，它可以當「先上場的競賽版本」；如果你時間夠，它應該被吸收進第一名方法，變成後段 local repair operator。單獨做，大概 **1–2 週**可以完成可用版本；做得成熟，**2–3 週**。CPU-only 就很夠，且因為它通常是局部評估，不太吃資源。它不是最強，但它經常是**最划算的第二步**。 

## 為什麼我不推薦純 RL 或純 DREAMPlace 路線

我不把純 RL、AlphaChip、或「直接把 DREAMPlace 改一改」放進前三名，原因不是它們不先進，而是**和這題失配**。第一，官方環境明確說不支援 GPU，而題目規模又小，真正的難點是 custom legality 與 route-aware geometry，不是大規模連續優化吞吐。第二，AutoDMP 這類工作雖然很強，但目標是數百 macros、百萬等級 cells 的 mixed-size placement；同一篇也明確指出 RL/ML 的 training 和 fine-tuning 可能需要多天與上千 CPU。第三，MacroPlacement 的公開評估本身就告訴你：要把 Circuit Training 做到可重現、可比較、且和 heuristic/SA 公平對比，工程成本非常高。對一個 hidden n < 50、2 小時總 runtime、且 scoring 高度客製的競賽題，這個投資報酬比不好。 citeturn5view2turn4view10turn4view11turn21search0turn17view0

如果你真的很想用你的 L4，最合理的方式不是做 end-to-end RL，而是做**小模型 surrogate**：例如預測 route cost、overflow risk、或候選 move 的 gain。但那應該只是輔助第一名方法的 seed selection / candidate triage，而不是主解器。純 ML 在這題最可能輸的地方，不是 optimize 不動，而是**碰到 edge block、channel geometry、FT inflation、PATH grammar 這種特規時，很難保證 legal 且穩。** citeturn26view0turn12view1

## 實作排程與硬體配置建議

若以競賽產出為目標，我會這樣排。第一週先把 parser、output writer、channel extractor、基本 cost evaluator、以及一個最小 B\*-tree/sequence-pair baseline 完成，並把 α、channel overflow penalty、FT overflow penalty 全做成可調參數。第二週把 route graph、bundle shortest path、以及 basic rip-up/reroute 補齊，讓 solver 至少能在公開 case 上穩定產出合法解。第三週開始做 two-phase SA、multi-start parallel search、critical-net local moves、以及 top-K exact evaluation。若還有第四週，就把第二名方法中的 polisher 補上，至少對 final candidates 做一次精修。這樣的順序最符合競賽現實，因為題目沒有官方 evaluator，你必須先做出**穩定、可 debug、能出 cfg 的解**，再談 refinement。 fileciteturn0file0 fileciteturn0file1

你的 Mac mini M4 16GB 很適合放 parser、unit test、可視化、和小 case 調參；真正的主力應該是 64-core Xeon server。最實際的配置是用 **C++17 寫核心 solver**，Python 只做 orchestration。SA 很容易平行化成多 seed；64 核可以同時跑 16–32 個 seed，再在尾段集中資源處理 top candidates。L4 我只會保留給兩件事：一是若你想試 surrogate model；二是做大批量可視化或 profile 時的實驗，不建議把正式解答綁死在 GPU。官方規則本來就要求 Linux / gcc/g++ 與 CPU-only 測試環境，往這方向做最保險。 citeturn29view0 fileciteturn0file1

## 最終建議與開放問題

如果目標是**拿競賽分數，而不是寫一篇泛用 paper prototype**，我的結論很明確：**主解器請選第一名的「約束感知 SA + 內嵌 bundled router」；第二名的 exact-hybrid 只做 top-K polish；第三名的 graph local optimizer 則當成 repair operator 吸收進主解器。** 也就是說，你最終應該做的是一個**混合系統**，但主體一定要是 SA/拓撲搜尋，而不是純 analytical、純 ML、或純 router。這條路最符合題目尺寸、官方環境、以及公開文獻裡最成功的設計模式。 citeturn5view0turn23view2turn12view1turn9view0

還有幾個目前仍需保守處理的開放點。第一，官方不提供 evaluator，所以 channel / FT overflow 的**精確扣分係數**與 guiding-point 的若干 tie-breaking 細節，仍要靠你用公開 testcase 逆向校準；因此 cost function 裡的 overflow 權重一定要做成 sweepable hyperparameters。第二，PATH 格式看起來允許用「一條 PATH 搭配 net count」做 bundle routing，但文件沒有明講同一個 block pair 是否能拆成多條 PATH 並各自分配 count；如果官方 parser 接受，這會明顯提升 routing quality，若不接受，就要退回 unsplittable bundle。第三，α 的正式定值官方表示會再公布，所以你應該保留 Pareto search 或至少保留一鍵 retune 的能力。這些都不會推翻上面的推薦，只會影響你第二與第三週的調參策略。 fileciteturn0file0 fileciteturn0file1