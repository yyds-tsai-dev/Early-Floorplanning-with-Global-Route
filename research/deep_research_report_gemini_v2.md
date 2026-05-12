# 2026 ICCAD 早期平面規劃與全域繞線競賽之高階演算法分析與資源架構報告

## 壹、 系統級晶片早期平面規劃之技術脈絡與競賽問題剖析
隨著積體電路（IC）製程技術的演進與系統級晶片（SoC）複雜度呈指數型增長，現代晶片設計正面臨空前的整合挑戰。不論是高效能運算（HPC）、人工智慧加速器抑或是車用電子，單一晶片內往往需要整合數以百計的矽智財（IP）與巨集區塊（Macro Blocks）。在傳統的物理設計（Physical Design）流程中，平面規劃（Floorplanning）與全域繞線（Global Routing）通常發生在暫存器傳輸階層（RTL）完全凍結並合成出網表（Netlist）之後。然而，這種設計典範往往導致在設計後期才發現嚴重的繞線壅塞（Routing Congestion）或時序違規（Timing Violation），迫使設計團隊必須回到架構階段重新修改設計，產生極高昂的迭代成本與上市時間延遲。
為了因應此一挑戰，電子設計自動化（EDA）領域近年來極力推動「設計左移（Shift-Left）」的概念，試圖在設計非常早期的階段，即進行物理特性的預測與最佳化。2026 年國際電腦輔助設計研討會（ICCAD）之 CAD 競賽 Problem E，由聯發科技（MediaTek）所提出，正是聚焦於此一前沿挑戰：「具備全域繞線能力之早期平面規劃（Early Floorplanning with Global Route）」。在開案初期，系統解決方案架構師（SSA）與設計團隊僅能掌握各矽智財的預估面積（Estimated Area）、形狀限制（Shape Constraint）以及其介面與匯流排需求（Interface and Bus Requirements），完全缺乏標準單元（Standard Cell）層級的詳細連線資訊。
在此資訊極度匱乏的條件下，演算法必須在給定的晶片輪廓（Chip Outline）邊界內，決定每一個區塊的最佳佈局位置與訊號繞線路徑。本競賽的核心目標在於優化總體晶片面積（Chip Area）與半周長繞線長度（Half-Perimeter Wirelength, HPWL），並同時確保所有物理與幾何限制均被嚴格遵守。這不僅為主導晶片面積預估、互連策略提供了量化基礎，更能提早揭露諸如過度擁擠、功耗不符預期或局部熱區等潛在的技術風險。因此，建構一套能夠在早期階段同時處理區塊形狀變異、穿透繞線（Feedthrough）以及通道分配（Channel Assignment）的平面規劃引擎，成為解決此問題的關鍵所在。

## 貳、 數學模型解構：穿透面積懲罰與分段線長最佳化
要建構有效解決此競賽問題的演算法，必須先深刻理解其獨特的成本函數（Cost Function）與非線性的物理約束模型。競賽的成本函數被精確定義為晶片輪廓面積與總繞線長度的線性組合，公式為 $Cost = OutlineArea + \alpha \times TotalWireLength$。其中，參數 $\alpha$ 是一個用以平衡面積與線長的權重常數，會隨著不同的測試案例（Testcase）動態調整，這要求演算法必須具備高度的適應性，能夠在「極度壓縮面積」與「極度縮短線長」這兩個可能互相衝突的目標間取得帕雷托最佳解（Pareto Optimal）。

### 異質區塊之物理約束特性
在早期平面規劃中，演算法必須處理三種具備截然不同幾何特性的區塊模型：
首先是「硬體區塊（Hard Block）」，這類區塊（如 SRAM 巨集或類比 IP）的形狀與面積是絕對固定的，且嚴格禁止任何外部訊號線穿透（No Feedthrough）。這意味著所有經過該區域的訊號都必須採取繞道（Detour）策略，使其成為平面規劃與繞線中最容易引發壅塞的瓶頸點。
其次為「邊緣區塊（Edge Block）」，這類區塊通常承擔晶片與外部環境的 I/O 介面連結功能（如 Bonding Pad 或 PHY 模組）。其形狀同樣固定且不允許訊號穿透，更嚴苛的是，它們必須被強制鎖定在晶片輪廓的特定邊界區域（例如圖中定義的 TL、TM、TR 等 12 個邊界區位），且僅允許在極小的 50 微米（um）範圍內進行微調與滑動。
最後是「軟體區塊（Soft Block）」，這是整個最佳化過程中彈性最大但也最複雜的元素。軟體區塊不僅允許長寬比（Aspect Ratio）在特定範圍內動態調整，更允許外部訊號以穿透（Feedthrough）的方式直接通過區塊內部。允許穿透能大幅縮短訊號的幾何飛行距離（Flyline Distance），並有效降低外部通道的佈線壓力，但作為代價，軟體區塊必須承受額外的面積懲罰（Area Penalty）。

### 通道佈線與穿透面積之非線性權衡機制
本問題最具深度的挑戰，在於全域繞線時的「通道（Channel）」與「穿透（Feedthrough）」決策。根據規範，所有未穿透軟體區塊的訊號，都必須沿著區塊間的通道走線。通道的佈線密度被嚴格限制為 25 nets/um，即在 1 微米的寬度內，單一方向僅允許容納 25 條訊號線。這種物理限制直接將繞線需求轉化為二維平面上的面積擴張。
當演算法選擇讓訊號穿透軟體區塊時，必須依據「穿透轉換效率表（Feedthrough Conversion Efficiency Table）」來計算擴展面積。該表格呈現高度階躍的非線性特徵：穿透線數若小於等於 3000，則轉換率為 20%；介於 3000 至 6000 之間，轉換率為 40%；6000 至 9000 為 80%；超過 9000 則高達 100%。
具體的面積計算機制極為精細。假設某軟體區塊原本的預估面積為 $Area_{base}$，且有一定數量的 net 需要穿透，演算法首先會計算出該區塊的基礎邊長（假定為正方形，邊長為 $\sqrt{Area_{base}}$）。接著，依據公式計算單邊所需擴張的寬度：$\Delta L = (\text{Net Counts} / 25) \times \text{Conversion Rate} / 2$。最終，該軟體區塊的實際佔用面積將膨脹為 $(\sqrt{Area_{base}} + \Delta L)^2$。
這種計算方式揭示了一個關鍵的數學現象：面積的增長是隨著穿透線數呈現二次方（Quadratic）級別的非線性膨脹，且由於轉換率的階躍設計，成本函數的梯度在閾值（如 3000、6000）附近會發生劇烈突變。這種不平滑且缺乏連續梯度的目標函數空間，使得傳統完全依賴偏微分的解析型（Analytical）平面規劃演算法（如基於二次規劃或力導向的模型）極易陷入局部最佳解（Local Minima），因而迫切需要具有強大全局搜尋與跳脫能力的現代離散最佳化架構。
另外，全域繞線長度的計算並非單純測量區塊中心點，而是透過計算繞線路徑（Path）穿越區塊邊界或通道時，兩兩接觸邊界之「重疊線段（Overlap Segment, OS）」的中點，定義為引導點（Guiding Point, G）。總線長是所有相鄰引導點之間的半周長線長（HPWL）加總，包含直線型（Straight）、L型（L-shape）與 Z型（Z-shape）等路由模式。這要求演算法不僅要排佈區塊位置，還必須即時且精確地切割出所有垂直通道矩形，並動態維護相鄰關係以計算 OS，計算複雜度極高。

## 參、 全球前沿佈局佈線演算法與開源技術總覽
為了尋找解決此競賽問題的最適方案，我們廣泛回顧了全球電子設計自動化領域的最新文獻與開源程式庫，涵蓋頂尖國際會議如 DAC、ICCAD、ISPD 以及 ASP-DAC。在處理固定輪廓（Fixed-outline）且混合了巨集區塊與可變形軟體區塊的平面規劃問題上，早期的研究多集中於改良資料拓樸表示法，例如 B*-Tree、Sequence Pair 或 O-Tree 等。這些拓樸結構結合模擬退火（Simulated Annealing, SA）能在純粹的區塊裝箱（Block Packing）問題上取得不錯的成效，但當引入繞線壅塞限制與穿透面積動態懲罰時，單純的 B*-Tree 往往需要耗費海量的迭代次數來尋找合法解，且難以將全域繞線成本即時且準確地反饋至成本函數中。
近年來，隨著「設計左移」趨勢的推進，學術界開始大量探索「繞線感知（Routing-aware）」與「穿透感知（Feedthrough-aware）」的同步最佳化技術。例如，引入 Corner Stitching 資料結構來精確管理空白空間（Whitespace），藉此快速推導出合法的繞線通道；或是利用圖論（Graph Theory）將相鄰的區塊與通道抽象化為權重圖，利用最短路徑演算法預估全域繞線；此外，更有研究採用機器學習、強化學習（Reinforcement Learning, 乃至 Graph Neural Networks, GNN）來取代傳統的熱力學隨機搜索，或是利用神經網路來極速預測佈局的繞線壅塞度。
在綜合考量聯發科技競賽題目中嚴格的 2 小時運行時間限制、強烈的邊界約束（12個 Edge 固定位置）、以及獨特的非線性階躍穿透成本後，我們篩選出三個最具潛力且代表了當今學術界最前沿思維的方法。這三個方法分別是：結合最新拓樸結構與雙階段退火的 FTAFP、採用張量遮罩與形狀重塑機制的 Flora、以及利用多約束圖論處理腳位與繞線的 Piano。以下將針對這三個方法的關鍵技術、優缺點、實作難度與資源消耗進行極度深入的剖析。

## 肆、 候選架構一：基於 SCB-Tree 與雙階段模擬退火之 FTAFP

### 演算法背景與核心機制
FTAFP (A Feedthrough-Aware Floorplanner for Hierarchical Design of Large-Scale SoCs) 是發表於 2025 年亞洲及南太平洋設計自動化會議（ASP-DAC）的最新研究，由北京郵電大學與華為諾亞方舟實驗室的研究團隊共同提出。該架構的設計初衷便是為了解決大型 SoC 在層級化設計中，模組間的大量訊號穿透與固定輪廓限制問題，其設計理念與 ICCAD 2026 Problem E 的競賽需求具有極高的重疊度。
FTAFP 突破性地提出了一種全新的拓樸表示法——**SCB-Tree (Slack Computation based Corner Block Tree)**。傳統的平面規劃拓樸（如 B*-Tree）雖然能有效表示區塊的相對關係，但在更新佈局時，牽一髮而動全身，難以預測單一節點的移動是否會導致整體佈局超出固定的晶片輪廓（Fixed-outline violation）。SCB-Tree 的巧妙之處在於，它將 CB-Tree 與 Corner Stitching（角縫合）資料結構進行深度融合。Corner Stitching 能夠將二維平面上的模組與空白空間（Empty Spaces）切分為一系列相互連結的矩形磚（Tiles），並利用四個方向的指標（Pointers）進行串接。
在建構 SCB-Tree 的過程中，演算法不僅排佈模組，更能以極低的運算複雜度（$O(n)$ 甚至局部 $O(1)$）即時計算出每個模組的「寬裕度（Slack）」。在水平約束圖（Horizontal Constraint Graph）中，某模組 $v_{hi}$ 的水平 Slack 被定義為其在不推擠其他模組且不超出總邊界的前提下，所能到達的最右側位置與最左側位置之差值（$slack(v_{hi}) = R(v_{hi}) - L(v_{hi})$）。透過掌握每個區塊的 Slack，演算法便擁有了「預知未來」的能力，能夠極度精準地篩選出不會導致邊界違規的微調動作（Perturbations）。
在搜索框架上，FTAFP 提出了一套**雙階段模擬退火框架（Two-phase SA Framework）**。第一階段名為「邊界收縮階段（Boundary Shrinking Phase）」，此階段的核心任務是不計一切代價將所有區塊塞入固定的晶片輪廓內。演算法會刻意挑選位於關鍵路徑（Critical Path，即 Slack 為零）上的區塊進行攝動，迅速消除輪廓外的溢出面積。一旦獲得合法的 Fixed-outline 初始解，便進入第二階段的「全面優化」。在此階段，FTAFP 引入了內建的 Feedthrough 估算模型。它會基於各區塊的位置建構最小生成樹（MST），即時預估每條 net 的走向，並套用類似競賽規則的成本模型，同步優化穿透模組的數量與穿透的線長（Feedthrough Wirelength）。

### 深度優缺點剖析與實作評估
在評估 FTAFP 演算法的適用性時，其最大的優勢在於對**固定輪廓（Fixed-outline）與合法化約束的絕對掌控力**。本競賽的晶片輪廓限制極端嚴格，且 Edge Block 被強制綁定在特定的 12 個區域邊界。SCB-Tree 的 Corner Stitching 空間劃分特性，使得開發者能輕易地將 Edge Block 的空間宣告為「不可動態配置的保留區」，完美確保 Edge Constraint 不被破壞。此外，雙階段模擬退火的熱力學特性，賦予了其極佳的跳脫局部最佳解能力。面對本題因 Feedthrough 轉換率（20% 至 100%）所帶來的突變型非線性面積擴張，SA 演算法能夠平滑地消化這類成本突刺，而不會如解析型演算法般引發梯度爆炸。
然而，FTAFP 的隱憂在於其**對全域繞線（Global Routing）細節的抽象化程度過高**。FTAFP 採用最小生成樹（MST）來預估繞線，這在一定程度上忽略了 ICCAD 競賽要求的精細「通道（Channel）」切割與「引導點（Guiding Point）」計算機制。競賽要求精確找出 Overlap Segment 的中點進行線段分段求和（如 Straight, L-shape, Z-shape），純粹的 MST 預估可能在最終產生 `.cfg` 輸出檔時，與官方評分系統的真實成本產生不小的誤差。
**實作難度與資源消耗**：
FTAFP 的實作難度屬於**中高級（Medium-High）**。開發者必須從零建立複雜的 Corner Stitching 指標系統，處理矩形分割與合併的極端案例（Corner cases），這極易引發記憶體區段錯誤（Segmentation Fault）。然而，在計算資源的消耗上，它的表現極為優異。由於所有的狀態更新都在拓樸圖與 Slack 陣列上進行，時間複雜度極低。在擁有 64 核心 Intel Xeon 的伺服器上，開發者可以輕易實作平行退火（Parallel Tempering），同時運行數十個不同溫度的馬可夫鏈，能在規定的 2 小時內完成數百萬次的佈局迭代探勘。

## 伍、 候選架構二：基於張量遮罩與形狀重塑之三階段平面規劃器 Flora

### 演算法背景與核心機制
Flora (One Step Beyond: Feedthrough & Placement-Aware Rectilinear Floorplanner) 是另一項發表於 2025 年的前沿研究，該論文已在 arXiv 上公開，由中國科學技術大學與華為聯合打造。有別於傳統將平面規劃視為單純二維裝箱問題，Flora 提出了一種極具創新性的思維：在決定模組位置的同時，便將後續的物理設計階段（特別是巨集與標準單元的實際放置位置以及模組間的穿透行為）納入最佳化考量，也就是具備了「穿透與放置感知（Feedthrough and Placement Aware）」的能力。
Flora 放棄了複雜的圖論拓樸，轉而將晶片畫布（Chip Canvas）全面網格化（Grid-based），並將問題解構為極具巧思的三個執行階段。
首先是**第一階段：粗粒度最佳化（Coarse-grained Optimization）**。Flora 捨棄了傳統的隨機擾動，創新地引入了「位置遮罩（Position Mask）」與「線長遮罩（Wiremask）」技術。Position Mask 是一個二維的布林張量（Boolean Tensor），精確標記出畫布上哪些網格是合法的，不會與已放置的硬體區塊或邊緣區塊發生重疊；Wiremask 則是一個與畫布等大的成本張量矩陣，矩陣中的每一個元素數值，代表著若將下一個模組放置於該網格點，將會導致總體半周長線長（HPWL）增加多少。當需要決定模組的最佳放置點時，演算法只需將 Wiremask 與 Position Mask 進行元素級別的相乘（Element-wise Product），便能在 $O(1)$ 的矩陣運算時間內，直接鎖定全局最優的合法放置點。這將平面規劃問題轉化為高度可平行化的張量數學運算。
接著進入**第二階段：細粒度最佳化與零空白重塑（Fine-grained Optimization & Zero-whitespace Reshaping）**。在完成初步配置後，晶片佈局中必然殘留許多未使用的碎裂空白空間（Whitespace）。Flora 在固定輪廓的嚴格約束下，啟動了局部形狀重塑演算法。它會動態拉伸與壓縮軟體區塊的長寬比，試圖完全吸收掉這些空白區域，達到零空白（Zero-whitespace）的極致佈局。這個過程等同於在進行細緻的 Feedthrough 最佳化，因為擴大軟體區塊的面積，實質上就是為後續的訊號穿透預留了足夠的面積增量（對應競賽中軟體區塊因 Feedthrough 產生的額外面積成本）。
最後是**第三階段：跨階段快速樹狀搜尋（Fast Tree Search for Cross-stage Optimization）**。Flora 利用樹狀搜尋結構進行微調，不僅優化區塊間的邊界對齊，更進一步減少冗餘的 Feedthrough 模組數量，確保整體的連線品質達到最優。

### 深度優缺點剖析與實作評估
Flora 的核心優勢在於其**無與倫比的運算速度與處理軟體區塊變形的適應力**。透過將搜尋過程張量化（Tensorization），Wiremask 與 Position Mask 徹底擺脫了傳統平面規劃中冗長的碰撞偵測計算。對於本競賽中高度彈性的 Soft Block，Flora 的第二階段形狀重塑機制（Reshaping）與競賽要求依照長寬比（Aspect Ratio Range）動態擴充 Feedthrough 面積的規則有著異曲同工之妙。演算法可以在吸收 Whitespace 的過程中，自然地消化掉因 25 nets/um 通道密度限制而產生的面積增量。
然而，Flora 也存在著一個針對本競賽規則的致命弱點：**對於「通道（Channel）」概念的排斥**。Flora 的設計哲學是追求零空白（Zero-whitespace），將所有剩餘空間分配給模組內部以優化內部放置。但在 ICCAD 的競賽定義中，不允許穿透的 Hard Block 與 Edge Block 必須強制依賴外部的 Channel 進行繞線。若過度消除區塊間的空白，將導致系統完全沒有空間劃分紫色的繞線通道（Top Net Route），進而觸發通道密度違規（Channel overflow: > 25 nets/um）或導致無法生成合法的 `.cfg` 通道定義檔。因此，若要套用 Flora，必須對其第二階段進行重度魔改，將「零空白」的目標改為「保留可控通道空白（Controlled Channel Whitespace）」。
**實作難度與資源消耗**：
Flora 的實作難度為**高（High）**。開發者需要具備將幾何演算法轉換為二維矩陣與張量運算的能力。在資源分配上，Flora 簡直是為 NVIDIA L4 GPU 量身打造。NVIDIA L4 搭載的 Ada Lovelace 架構與大量的 Tensor Cores，能夠在毫秒級別內完成數千萬次 Wiremask 與 Position Mask 的 Hadamard Product 運算。相較於依賴 CPU 的傳統方法，Flora 能利用 GPU 的高記憶體頻寬（Memory Bandwidth）實現指數級的加速，極度適合用來在開局的前五分鐘內，暴風式掃描並生成一個品質極高的初始解（Initial Placement）。

## 陸、 候選架構三：圖論驅動之多約束腳位與繞線感知平面規劃器 Piano

### 演算法背景與核心機制
Piano (A Multi-Constraint Pin Assignment-Aware Floorplanner) 同樣是於 2025 年公開於 arXiv 的最新重量級研究，其關注點直指平面規劃長久以來被忽略的盲區：腳位分配（Pin Assignment）與真實繞線路徑的感知。傳統平面規劃器多數依賴抽象的半周長線長（HPWL）來逼近繞線長度，這種做法在面臨現代晶片嚴苛的固定輪廓、穿透限制與特定引腳約束時，往往會產生極大的估算誤差。Piano 提出了一種基於圖論（Graph-based）的全新架構，不僅在早期階段同時優化模組配置與腳位分配，更具備精確計算 Feedthrough 數量與未放置腳位（Unplaced pins）的能力。
Piano 的核心技術奠基於精密建構的**幾何解析圖（Geometric Resolution Graph, GRes​）**。不同於傳統將區塊視為孤立的實體，Piano 在確定初始合法佈局後，會依據模組間的幾何相鄰關係動態生成圖結構。在 $G_{Res}$ 圖中，每一個節點（Node）代表一個實體模組（Module），而節點之間的邊（Edge）則代表了相鄰模組間實際共享的物理邊界長度或可用的通道空間，並以此定義邊的權重為可用空間（PinSpace）。
這個機制的威力在於它與 ICCAD 競賽中要求的**「重疊線段（Overlap Segment, OS）」與「引導點（Guiding Point, G）」計算邏輯達到了令人驚豔的完美契合**。競賽規則明確定義，Total Wirelength 是透過加總路徑中所有相鄰 Overlap Segment 的中點（Guiding Point）距離來計算的。在 Piano 的圖論模型中，兩個節點之間連線的權重與物理意義，本質上就已經精確捕捉了 OS 的幾何資訊。
當 $G_{Res}$ 建構完成後，Piano 放棄了傳統的 HPWL，轉而採用**最短路徑演算法（Shortest Path Algorithm）**來進行腳位分配與全域繞線預測。針對需要連接的網路（Net），Piano 會在衍生的遮罩圖（$G_{mask}$）中，尋找連接來源與目的模組的最短圖論路徑。如果這條路徑跨越了不相鄰的節點，演算法便能立即判定這是一次必須執行的訊號穿透（Feedthrough），並自動將該資訊記錄下來以計算後續的面積懲罰。此外，為了處理如 Edge Block 必須貼齊特定邊界的強約束，Piano 在漸進式優化階段引入了三種專門設計的局部算子（Local Operators），在維護圖論合法性的前提下微調區塊位置，並結合空白空間消除策略來提升整體的佈局緊密度。

### 深度優缺點剖析與實作評估
將 Piano 應用於本競賽，其最大的優點在於**「所見即所得」的精確成本計算能力**。透過圖論最短路徑搜尋，Piano 能夠在內部最佳化過程中，完美模擬官方評測系統在計算直線型（Straight）、L型與 Z型連線時的運作邏輯。它能以近乎零誤差的精確度區分該條連線應該走外部的 Channel 還是以 Feedthrough 穿透 Soft Block。由於圖論中的每條邊都實質反映了兩兩相鄰的邊界，這使得 Piano 在最終輸出符合競賽格式要求（明確定義 `NumChannels` 與 `CHANNEL` 座標的 `.cfg` 檔）時，具備了無與倫比的優勢。
然而，Piano 面臨著極為嚴峻的**時間複雜度詛咒（Time Complexity Curse）**。ICCAD 測試案例（Testcase）中的 Netlist 規模往往高達數萬至數十萬條連線（例如官方範例中的 BLK03 甚至有單筆連線達 1500 條 nets）。如果在模擬退火的每一步微調中，區塊的位置改變都必須觸發 $G_{Res}$ 圖的重建，並重新對數萬條 net 執行 Dijkstra 或 A* 最短路徑搜尋，其運算時間將呈指數級暴增。這會導致演算法每秒能評估的狀態數量（States evaluated per second）驟降，極可能在競賽規定的 2 小時（Runtime limit）內無法收斂至可接受的最佳解。
**實作難度與資源消耗**：
Piano 的實作難度為**極高（Very High）**。動態維護幾何圖論結構並處理大量離散圖的增刪改查，是程式設計上的極大考驗。這類動態圖論演算法難以有效向量化（Vectorize），因此極不適合放入 GPU 運算。它極度仰賴 CPU 的單執行緒效能（Single-thread IPC）、分支預測（Branch Prediction）準確率以及龐大的快取階層（Cache Hierarchy）。因此，高階的多核心 CPU 是支撐此演算法的唯一選擇。

## 柒、 跨演算法綜合評比與混合流（Hybrid Flow）建構策略
面對聯發科技提出的此一複合性 EDA 難題，單一演算法往往難以兼顧「極速生成初始解」、「突破局部最佳解」與「精確全域繞線」三大需求。為求在競賽中拔得頭籌，我們必須深入剖析上述三種前沿架構的雷達圖特徵，並為其設計出一套協同作戰的混合架構（Hybrid Flow）。
以下為三種候選架構在競賽具體要求下的效能雷達對比表：

| 評量維度與競賽契合度 | FTAFP (SCB-Tree + SA) | Flora (Mask-based + Reshape) | Piano (Graph-based Routing) |
| --- | --- | --- | --- |
| 核心技術底層邏輯 | 寬裕度計算、雙階段模擬退火 | 張量遮罩矩陣運算、局部形狀重塑 | 相鄰圖論建構、最短路徑搜尋 |
| 對付固定輪廓限制 (Fixed-outline) | 極佳 (拓樸嚴格保證邊界不違規) | 良 (需靠邊界收縮迭代處理) | 優 (局部算子可鎖定特殊區位) |
| Edge Block 之 12 區位邊界約束 | 極佳 (可透過虛擬節點強制綁定) | 中 (Position Mask 容易設定但易碎) | 優 (局部算子可凍結固定節點) |
| 處理 Feedthrough 階躍面積懲罰 | 極佳 (SA 成本函數易於無縫嵌入) | 尚可 (需依賴事後的形狀動態拉伸) | 優 (最短路徑權重可直接疊加懲罰) |
| Channel 與 Guiding Point 契合度 | 尚可 (需事後額外步驟切割垂直通道) | 弱 (傾向消除空白，易導致通道容量溢出) | 完美 (圖的相鄰邊即為 Overlap Segment) |
| 運算時間與收斂效率 (Runtime) | 優 (拓樸更新時間僅為 O(1)) | 優 (矩陣運算可透過 GPU 瘋狂加速) | 弱 (動態圖論更新與反覆尋路極度費時) |
| 工程實作挑戰性 | 中高 (指標與樹狀操作繁複) | 高 (張量空間操作與顯存記憶體管理) | 極高 (動態圖演算法與記憶體池最佳化困難) |
| 競賽策略之角色定位建議 | 中樞核心 (Baseline 探勘框架) | 前鋒部隊 (極速生成優質初始佈局) | 收尾狙擊 (精確繞線計算與通道輸出) |
基於上述深度評析，我們強烈建議**摒棄單打獨鬥的思維，轉而建構一套「異質混合流（Heterogeneous Hybrid Flow）」演算法系統**，具體的三階段作戰策略如下：

1. **階段一：基於 GPU 加速的 Flora 張量初始佈局（0~5 分鐘）**
在程式啟動的初始階段，時間最為寶貴。捨棄低效的隨機擺放，直接利用 Flora 的 Wiremask 與 Position Mask 概念，將畫布網格化並送入 GPU。此階段暫不考慮精確的 Feedthrough 面積膨脹，僅追求在最短時間內找出符合 Edge Block 與 Hard Block 位置約束的「拓樸粗解」。GPU 的強大算力能在幾分鐘內過濾掉數千萬種劣質組合，提供後續階段一個極高品質的起跑點。
2. **階段二：以 FTAFP 為骨幹的大規模平行模擬退火（5~90 分鐘）**
承接高質量的初始解後，將佈局狀態轉換為 FTAFP 的 SCB-Tree 結構。利用 64 核心 CPU 啟動多重馬可夫鏈（Multiple Markov Chains）進行高度平行的模擬退火。由於 SCB-Tree 具備 $O(1)$ 的 Slack 計算能力，能保證每次攝動（Perturbation）都不會違反晶片輪廓限制。在此階段，全面啟動 25 nets/um 的通道密度檢查與 20%~100% 的 Feedthrough 轉換率面積計算模組。透過 SA 熱力學的跳脫機制，全力在非線性的成本空間中探勘出面積與線長（$\alpha$ 權重）最優的幾何配置。
3. **階段三：Piano 圖論收斂與通道精確輸出（90~120 分鐘）**
當 SA 的溫度降至臨界點，佈局趨於穩定後，啟動 Piano 的 Graph-based 核心。將最終的物理佈局幾何化，嚴格按照官方定義沿 X 軸進行「垂直切割（Vertical Slicing）」以生成所有通道（$CH1, CH2 \dots$）的矩形座標。建構解析圖 $G_{Res}$，並執行精確的最短路徑搜尋，不僅能完美計算出所有 Guiding Points 的座標與 HPWL 分段線長，更確保生成的 routing pattern 與 feedthrough 設定完全符合 `.cfg` 的嚴格語法格式規範，規避任何可能導致直接 Fail 的格式錯誤風險。

## 捌、 基於異質硬體架構之運算資源分配與平行化實作藍圖
建構上述如此龐大且精密的三階段混合流系統，單純依賴循序編程（Sequential Programming）是絕對無法在 2 小時的極限時間內完成的。充分利用並榨乾使用者現有配備的每一滴算力（Mac mini M4 16GB、NVIDIA L4 GPU 伺服器、64 核心 Intel Xeon 伺服器），實施「異質協同運算（Heterogeneous Collaborative Computing）」，是決定勝負的最後一哩路。

### 1. Mac mini M4 16GB：敏捷開發測試與視覺化分析中心

- **硬體微架構特性**：Apple M4 晶片以其極端強悍的單核心效能（Single-thread Performance）與統一記憶體架構（Unified Memory）聞名。然而，16GB 的記憶體總量成為了其處理海量節點圖論或超大維度張量的致命傷，這註定它無法成為運算的重載核心。
- **競賽專屬任務配置建議**：
  - **I/O Parser 與合法性驗證引擎開發**：M4 平台極其適合進行敏捷的 C++ 或 Python 前端開發。實作高效的解析器（Parser）來讀取官方提供的複雜 `.csv` 測試案例，包含處理寬高比範圍（ASPECT RATIO RANGE）、Edge Block 限定位置（如 TLLT、RB 等字串解析）、以及連線矩陣（Connection Matrix）的讀取與記憶體內建構。
  - **規則驗證器（Legalization Checker）**：撰寫一個獨立的 Validator 模組。在伺服器回傳結果前，先在本地端驗證是否發生「Block overlap」、「Routing open」、「Channel overflow (>25 nets/um)」或「Outline constraint violation」等直接導致零分的 Fail conditions。M4 快速的編譯速度（Compile time）能讓除錯循環大幅縮短。
  - **開發客製化 GUI 視覺化工具**：實作一個輕量級的 Floorplan Viewer（可基於 ImGui 或 Qt）。讀取輸出的 `.cfg` 檔，即時於螢幕上渲染出粉紅色的 Hard Block、藍色的 Soft Block、淺綠色的 Channel 以及紫色的實際繞線路徑。在早期演算法調適階段，視覺化是唯一能幫助開發者察覺「演算法產生奇異繞線（如無謂的 Z-shape detour）」或「通道分配過度集中」的利器。

### 2. NVIDIA L4 GPU 伺服器：張量巨量運算與 AI 加速探勘

- **硬體微架構特性**：NVIDIA L4 基於優異的 Ada Lovelace 架構，具備專注於推論與張量運算的 Tensor Cores，且擁有 24GB 的高頻寬 GDDR6 記憶體，是進行大規模矩陣乘法與深度學習模型推論的絕佳戰場。
- **競賽專屬任務配置建議**：
  - **實作 Flora 演算法的並行化張量遮罩**：混合流的第一階段極度依賴 Flora 的效能。利用 CUDA C++ 或 PyTorch 框架，將晶片輪廓離散化為巨大的 2D 張量。將 Position Mask（布林矩陣）與 Wiremask（浮點數成本矩陣）完全載入 GPU VRAM 中。利用 GPU 單指令多資料流（SIMD）的特性，同時並行運算千萬個潛在放置點的 HPWL 增量，並利用 Reduction 演算法瞬間找出全局最佳的初始放置區位。
  - **機器學習輔助探勘啟發式模型（可選進階策略）**：近年學術界熱衷於強化學習輔助 EDA（RL-EDA）。開發者可利用過往的開源 Floorplan 資料集預先訓練一個小型的圖神經網路（GNN）或多層感知機（MLP）。在伺服器上透過 GPU 推論，輸入目前的區塊相對位置，神經網路能在一毫秒內預測出「該配置下，Feedthrough 數量超越 6000（即觸發 80% 面積懲罰）的機率分佈」。將此高維度的預測結果傳回 CPU 端的 SA 模組作為快速啟發式評估函數（Heuristic Cost Function），可大幅省去耗時的實際路徑模擬搜尋時間。

### 3. 64 核心 Intel Xeon 伺服器：巨量平行運算與全域繞線中樞

- **硬體微架構特性**：64 核心提供了令人敬畏的多執行緒（Multi-threading）並行吞吐能力。伺服器等級的 CPU 更配有龐大的 L3 快取（Cache）與巨量 RAM（通常 > 256GB）。這使其成為處理高複雜度邏輯分支、龐大記憶體佔用且難以被 GPU 向量化的圖論與拓樸演算法的終極武器。
- **競賽專屬任務配置建議**：
  - **極致平行化的多重馬可夫鏈模擬退火（Parallel Tempering SA）**：實作 FTAFP 的雙階段退火時，單執行緒的 SA 極易在非線性的面積懲罰下卡在深谷中。利用 Xeon 的 64 個實體核心，搭配 MPI（Message Passing Interface）或 OpenMP 函式庫，同時啟動 64 個獨立的 SA 探勘執行緒。每個執行緒設定不同的冷卻率（Cooling Rate）與初始亂數種子。最重要的是實作「狀態交換機制（Replica Exchange）」，定期讓低溫與高溫的執行緒交換目前找到的最佳 SCB-Tree 拓樸解。這能以極其暴力的算力優勢，徹底粉碎傳統演算法無法跨越的局部最佳解障壁。
  - **多執行緒併發通道劃分與全域繞線（Multi-threaded Channel Routing）**：在混合流進入第三階段，啟動 Piano 的圖論繞線時，將面臨巨大的運算瓶頸。此時，Xeon 伺服器的多核優勢將再次展現。演算法可先掃描全域的 Connection Matrix，依據物理空間分佈與 Net 之間的互斥性，建構相依圖（Dependency Graph）。隨後，將晶片劃分為多個無重疊的繞線區域（Routing Regions），利用 64 個核心以 OpenMP 執行並行 A* 演算法或多商品流（Multicommodity Flow）網路運算。這能將原本需要數十分鐘的圖論繞線收斂時間，強行壓縮至數分鐘之內。
  - **高頻率成本計算與面積合法化（Legalization Engine）**：在 SA 的每一次微調中，系統都必須反覆呼叫面積計算函式。Xeon 伺服器負責承載這些最耗費 CPU 資源的計算邏輯，包含即時動態提取 Soft Block 的面積與連線數量、套用公式計算 $\Delta L = (\text{Net Counts} / 25) \times \text{Conversion Rate} / 2$、驗證通道是否有觸發 Channel Wire Density Limit 的致命違規（$\le 25 \text{ nets/um}$）、並精算 Guiding Point ($G_{m,n}$) 之間的曼哈頓距離（Manhattan Distance）。充沛的核心數確保這些高頻發生的底層邏輯判斷不會成為拖垮整體系統執行時間的效能瓶頸。

## 玖、 總結與技術風險管控
2026 ICCAD CAD Contest Problem E (MediaTek) 「具備全域繞線能力之早期平面規劃」，精準擊中了現代物理設計左移趨勢下的最痛點。其核心技術挑戰不僅在於如何在固定的晶片輪廓下配置各式異質區塊，更在於必須以極高的演算法智慧，完美平衡軟體區塊的穿透面積膨脹懲罰（Feedthrough Area Penalty）與外部通道的繞線寬度消耗（Channel Width Consumption）。
綜觀全球最前沿的 EDA 研發成果，單一的傳統拓樸或純解析型演算法已無法應對此等非線性的多目標約束問題。本報告深入剖析並遴選出了三大頂尖學術架構：專注拓樸寬裕度與退火的 FTAFP、擅長網格張量與形狀重塑的 Flora、以及精準掌控圖論腳位與引導點繞線的 Piano。為了在競賽的 2 小時極限時間內斬獲最佳成績，我們提出了一套前所未有的**「異質混合流（Heterogeneous Hybrid Flow）」**策略。
透過整合 Flora 的張量遮罩進行極速初始化，以 FTAFP 的 SCB-Tree 為主幹執行具備穿透感知能力的大規模平行模擬退火，最終由 Piano 的幾何圖論模型精確收斂全域繞線並劃分通道引導點，此一混合架構完美涵蓋了競賽的所有評分面向。
在實戰部署上，必須嚴格落實**「異質協同運算」**方針。充分利用 Mac mini M4 的敏捷特性進行 I/O 與合法性驗證工具鏈開發；解放 NVIDIA L4 GPU 的張量算力以極速推演初始遮罩佈局；最終將最龐大、最複雜的平行退火與多執行緒圖論繞線決策，全數託付給 64 核心 Intel Xeon 伺服器的巨量吞吐能力。此一策略性的演算法藍圖與極致的硬體資源配置，將不僅確保開發團隊能順利生成完全符合競賽複雜規格的物理佈局方案，更具備了問鼎 2026 ICCAD 全球總決賽桂冠的絕對潛力。

---

Source: https://gemini.google.com/u/0/app/7c759d57946b2d05?hl=zh-TW&pageId=none
Exported at: 2026-04-29T08:19:30.477Z