# Windows 注音輸入法開發調查報告

> 日期：2026-11-15
> 目標：做一套操作和微軟注音一樣、但 (1) 不會讓舊版軟體卡死，(2) 選字更聰明，(3) 可以移除預設詞庫的注音輸入法。

---

## 0. 結論摘要

1. **市面上沒有一套能同時滿足三個條件的現成方案。** 最接近的有兩個開源專案：
   - **新酷音 windows-chewing-tsf**：成熟，有在維護，有簽章的正式版。但它的操作邏輯是「酷音」那一套，不是微軟注音。
   - **小麥注音 Windows 版 (openvanilla/win-mcbopomofo)**：MIT 授權，選字引擎很好。但官方明說**不發布安裝檔、不收 PR、不收 issue**，只能自己編譯。
2. **舊版軟體卡死的原因，微軟有官方說明（至少一部分）：** Windows 10 2004 起，新版微軟 IME 用了新版 TSF，會往應用程式的訊息佇列送 TSF 專用的視窗訊息。舊程式如果自己寫 message filter，用 `PeekMessage`/`GetMessage` 把訊息拿掉但沒交給 `DispatchMessage`，TSF 就永遠等不到處理完成，程式就卡死。勾「使用舊版 IME」等於換回不依賴這些訊息的舊架構，所以就好了。
3. **建議做法：** 自己寫一個「傳統、in-process、絕不阻塞」的 TSF 輸入法外殼，選字核心則參考或重用 MIT 授權的小麥注音引擎（Gramambular + 使用者覆寫模型），詞庫改成**分層、每層可以單獨開關**的架構。
4. **先花一小時做個驗證：** 在會卡死的那支舊軟體上試裝新酷音（和自己編譯的小麥注音）。如果不卡，就證實「第三方傳統 TSF 輸入法可以避開這個問題」，路線就確定了。

---

## 1. 現有方案比較

| 方案 | 架構 | 授權 | 操作像微軟注音嗎 | 自動選字／學習 | 自訂詞庫 | 能移除預設詞庫嗎 | 狀態 |
|---|---|---|---|---|---|---|---|
| 微軟注音（新版） | 新 TSF＋TextInputHost | 閉源 | — | 有，但學習效果有限 | 有 | **不能** | 部分舊程式會卡死 |
| 微軟注音（舊版 IME 相容模式） | 傳統 TSF | 閉源 | 是 | 有 | 有 | 不能 | 微軟說只是「暫時的解法」 |
| **新酷音 windows-chewing-tsf** | TSF；核心 libchewing 已大量改用 Rust | libchewing 是 LGPL-2.1 | 部分像（酷音的操作邏輯） | 有，智慧選詞＋自動學習 | 有詞庫編輯器，CSV 匯入匯出 | 沒查到這個選項（只確定使用者詞會優先於系統詞） | 活躍，最新穩定版 26.1.3.0（2026-01），專案移到 Codeberg |
| **小麥注音 win-mcbopomofo** | TSF：Client DLL＋背景 Server 程序（跨程序 IPC），C++ | MIT | 不像（小麥注音邏輯） | 有，自動選字，macOS 版有使用者覆寫模型 | macOS 版有使用者詞彙、排除詞彙（Windows 版沒確認） | macOS 版能排除單一詞，不能整包移除 | **只有原始碼**，不發安裝檔、不收 PR/issue |
| PIME | C++ TSF DLL＋Python/Node 後端（跨程序） | LGPL | 內建的「注音」是傳統逐字選 | 依模組而定 | 依模組而定 | 依模組而定 | 1.3.0 穩定版，更新很慢 |
| 小狼毫 Weasel (RIME)＋注音方案* | TSF＋背景伺服器 | GPL-3.0 | 不像 | 有使用者詞頻學習 | YAML 設定 | **可以**（改方案設定），但對一般使用者太難 | 活躍 |

\* RIME 這一列是根據我原本就知道的資訊，這次沒有重新查證。

**判斷：**
- 新酷音適合拿來**驗證相容性**和**參考 TSF 實作**，但要改成微軟注音的操作邏輯，等於要動到核心行為；授權也是 copyleft。
- 小麥注音的**選字引擎和資料是 MIT 授權**，最適合重用。但 Windows 版是 Client/Server 跨程序架構，這正是要小心的卡頓來源（見第 2 節），而且上游不收貢獻，只能 fork 後自己維護。
- 所以「完全不造輪子」做不到，但可以**只造外殼，引擎和資料盡量重用**。

---

## 2. 為什麼舊版軟體會卡死？我們要怎麼避開？

### 2.1 已知原因（有官方文件）

微軟的 KB〈A user-defined application using message filters may become unresponsive〉：

- 影響 Windows 10 2004 / 20H2 / 21H1 / 21H2（文件說 Windows 11 沒觀察到，但實際上 Win11 也有其他 IME 卡死問題）。
- 2004 引入新版 TSF，會用到額外的視窗訊息。
- 程式的 message filter 用 `PeekMessage`/`GetMessage` **移除訊息但沒有 `DispatchMessage`**，TSF 就無法完成處理，程式停止回應。
- 微軟給的兩個解法：改程式（舊軟體沒辦法改），或開「使用舊版 IME」（暫時的解法）。

其他有記錄的情況：
- Windows 11 22H2：用快速鍵切換 IME 模式時，載入某些 TSF 元件的程式會無回應（後來 KB5020044 修掉）。
- Excel 在新版中／日文 IME 下用滑鼠拖曳，會出錯、卡住或關閉。

這些問題的共同點是：**新版 IME 依賴「應用程式會正常泵送和派送訊息」，還有一些跨程序的非同步流程**（候選窗是 TextInputHost 這類外部程序畫的）。舊程式（VB6、Delphi、舊版 MFC、自製訊息迴圈、遊戲、Java AWT 等）常常不符合這個假設。

### 2.2 設計原則（這是本專案最重要的部分）

| # | 原則 | 做法 |
|---|---|---|
| 1 | **用傳統 TSF TIP，不依賴新版 TSF 的訊息機制** | 實作 `ITfTextInputProcessorEx` 和 `ITfKeyEventSink` 等傳統介面。IMM32 舊程式會透過 Windows 的 CUAS 相容層使用我們。 |
| 2 | **按鍵處理全程同步、in-process** | 選字引擎和詞庫直接放在 TIP DLL 裡（記憶體映射詞庫檔）。**按鍵路徑上不能有任何跨程序呼叫**，這點跟 win-mcbopomofo、PIME、RIME 的 Client/Server 架構不同。 |
| 3 | **Edit session 盡量用 `TF_ES_SYNC`** | 在 key event sink 裡要求同步的 edit session。如果被排入非同步（回傳 `TF_S_ASYNC`），**絕對不等待**，狀態機要能接受「晚一點才套用」。 |
| 4 | **UI 執行緒上不做任何會阻塞的事** | 學習資料寫檔、詞庫重新編譯都丟到背景執行緒。所有鎖都要有逾時。 |
| 5 | **候選窗自己畫** | 在 in-proc 建一個 Win32 layered window（`WS_EX_NOACTIVATE \| WS_EX_TOPMOST \| WS_EX_TOOLWINDOW`），不搶焦點、不依賴外部程序。另外支援 UILess（`ITfCandidateListUIElement`），讓全螢幕遊戲自己畫候選字。 |
| 6 | **每個程式可以設不同的相容設定檔** | 讓使用者對特定程式（依 exe 名稱）切換成「不顯示行內組字、直接送出文字」、「關閉某些 TSF 功能」等保守模式。萬一還是遇到怪程式，使用者自己就能解決，不用等改版。 |
| 7 | **完整的平台支援** | 要出 x86 / x64 / ARM64 三種 DLL（32 位元的舊程式只會載入 32 位元的 TIP）。註冊 `GUID_TFCAT_TIPCAP_IMMERSIVESUPPORT`、`SYSTRAYSUPPORT`、`UIELEMENTENABLED`、`COMLESS`、`INPUTMODECOMPARTMENT` 等類別。DLL 和詞庫檔要開 `ALL APPLICATION PACKAGES` 的讀取權限，否則 Store app / AppContainer 讀不到。 |
| 8 | **一定要有相容性測試清單** | 記事本、Word、Excel（含拖曳）、Chrome、Edge、VS Code、Windows Terminal、Store app、Java Swing、VB6/Delphi 舊程式、遊戲（全螢幕）、**以及你手上會卡死的那幾支程式**。 |

> ⚠️ 要老實說的限制：第三方 TIP 一樣會經過系統的 msctf／CUAS。如果卡死的根源在 TSF 本身（而不是新版微軟 IME 的特殊機制），我們也會受影響。這就是為什麼第 0 節建議**先用新酷音在你那支軟體上驗證**。新酷音是傳統 TSF＋in-proc 的實作，如果它不會卡，我們的路線就站得住腳。

---

## 3. 聰明選字的設計

### 3.1 基礎：整句自動選字

- 把注音音節切成格狀圖（lattice），每條路徑用 **unigram + bigram 語言模型**打分，用 Viterbi 找最佳句子。
- 這正是小麥注音的 Gramambular 2 演算法（MIT，C++）和新酷音採用的思路，可以直接參考或重用。

### 3.2 使用者習慣學習（分三層）

| 層 | 學什麼 | 怎麼用 |
|---|---|---|
| A. 個人詞頻 | 每個「讀音 → 字詞」被選了幾次，加上**時間衰減**（例如半衰期約一週） | 候選字排序：`score = log P_base + α·log(1 + 衰減後次數)`，常選的往前排 |
| B. 上下文覆寫 | 「前一兩個詞＋這個讀音」時使用者改選了什麼（參考小麥注音的 User Override Model：LRU 加指數衰減） | 下次遇到同樣的前文，自動選使用者上次選的那個，而不是語言模型的預設 |
| C. 新詞偵測 | 使用者連續好幾次手動把相同的幾個字湊成一串 | 達到門檻（例如 3 次）就自動當成候選詞加入使用者詞庫，並可提示使用者確認 |

另外再加上：
- **遺忘機制**：在候選窗按快速鍵（例如 Ctrl+Del），就「不要再把這個字詞往前排」，這個操作同時會寫入排除清單（見第 4 節）。
- **完全在本機、可以一鍵清除**，學習資料用純文字或 SQLite 存放，使用者看得到、改得動。

### 3.3 方便的自訂詞庫

- 在組字中選取一段文字，按快速鍵就能「加入詞庫」，不用開設定視窗。
- 圖形化詞庫編輯器：搜尋、排序、批次刪除、CSV／TXT 匯入匯出。
- 支援**相容匯入微軟注音的使用者自建詞彙**（微軟注音可以匯出），降低換輸入法的門檻。

---

## 4. 可以移除預設詞庫：分層詞庫架構

```
優先權（高 → 低）
┌──────────────────────────────┐
│ 排除清單 (Exclusion)          │  永遠不出現的「讀音＋字詞」
├──────────────────────────────┤
│ 使用者詞庫 (User)             │  自己加的詞＋自動學到的新詞
├──────────────────────────────┤
│ 詞庫包 (Phrase packs) 可開關   │  一般詞彙／成語／地名／人名／專業術語／網路用語…
├──────────────────────────────┤
│ 基礎字庫 (Base chars) 必要     │  單字＋讀音（不能關，否則打不出字）
└──────────────────────────────┘
```

- **每個詞庫包都能單獨停用或刪除**，這就解決了「預設詞庫是負加成卻拿不掉」的問題。
- 使用者改設定後，在背景把啟用的各層編譯成一個二進位查詢結構（trie/FST）。輸入法只做記憶體映射讀取，不影響打字速度。
- 排除清單可以精確到「某個讀音的某個詞」：只把讀音 ㄕˋ ㄐㄧㄢˋ 下面那個你永遠不用的怪詞拿掉，同讀音的「事件」「試鍵」等其他詞完全不受影響。

**詞庫資料來源和授權**（要注意）：
- 小麥注音的詞庫：MIT，可以商用和修改，**首選**。
- libchewing 的詞庫：LGPL-2.1。
- 教育部辭典：大多是 CC BY-ND 3.0 TW（**禁止改作**），拆成詞庫包可能有授權問題，使用前要確認。

---

## 5. 要模仿的微軟注音操作（初步清單，實作前要逐項對照實機確認）

- Shift 切換中英文、Ctrl+Space 開關輸入法、Shift+Space 切換全半形
- 行內組字、自動選字；↓ 或 Space 叫出候選窗；1–9 選字；←→ 移動游標改中間的字
- 組字區長度上限、自動送出
- 鍵盤排列：標準、倚天、許氏、IBM、漢語拼音等
- 標點符號和特殊符號輸入（Ctrl+, 等快速鍵、符號表）
- 候選窗的外觀、字型大小、橫式或直式
- 設定項目的名稱和分組盡量跟微軟注音一致，讓使用者不用重新學

---

## 6. 技術路線選項

| 選項 | 內容 | 優點 | 缺點 |
|---|---|---|---|
| A. Fork 新酷音 | 改操作邏輯、加分層詞庫 | 最快有能用的版本；TSF 外殼成熟 | 要改核心行為；copyleft；跟上游會越差越遠 |
| B. Fork win-mcbopomofo | 把 Server 合併回 in-proc，改操作邏輯 | MIT；選字引擎好 | 上游不收 PR；要大改架構 |
| **C. 自建外殼＋重用引擎（建議）** | 自寫 in-proc TSF TIP（C++ 或 Rust + windows-rs），引擎用／移植 Gramambular 2＋小麥詞庫 | 架構完全照第 2 節的原則設計；MIT 乾淨；長期好維護 | 前期工作量最大 |

**建議語言：**
- TSF 外殼：C++（Windows SDK 範例和社群資料最多，除錯最直接）或 Rust（`windows` crate，記憶體安全；新酷音已經走這條路）。
- 引擎：如果要直接重用 Gramambular 2，就用 C++。
- 設定程式：WinUI 3 或 WPF。

---

## 7. 建議的開發階段

| 階段 | 內容 | 完成標準 |
|---|---|---|
| **M0 驗證** ✅ | 在會卡死的舊軟體上測新酷音、win-mcbopomofo | 確認第三方傳統 TSF 不會卡。**2026-11-15 結果：卡死的程式是 3ds Max；新酷音在 3ds Max 裡用了一陣子都沒有無回應，路線成立。** 同日 M1 外殼在 3ds Max 裡也沒有卡住；以前最常見的觸發點是**按 Ctrl+C 複製文字**（不是打字），M1 下按了好幾次都沒發生。 |
| M1 最小外殼 ✅ | in-proc TSF TIP：註冊、Shift 切換、行內組字、送出文字（先用假引擎） | 在相容性清單上都能打字、不會卡。2026-11-15 完成，3ds Max 通過 |
| M2 注音引擎 ✅ | 標準鍵盤排列＋音節解析＋Viterbi 整句選字＋候選窗 | 能日常打字。2026-11-15 整合小麥注音引擎與詞庫，使用者在 3ds Max 實測沒有卡住 |
| M3 微軟注音操作一致性（進行中） | 照第 5 節逐項對照 | 從微軟注音換過來的使用者不覺得彆扭。2026-11-15 依使用者回報完成：Ctrl+符號鍵與 `` ` ``＋符號鍵出全形標點、Shift+符號出半形、選游標後面的字並往後移、選字框 ↓ 換頁與 → 展開、空白鍵打出空格、Shift+Space 全／半形。之後又完成：符號鍵盤（Ctrl+Alt+,）、`` ` ``U／`` ` ``B 內碼輸入、↑ 換字、匯入微軟注音詞典。未做：聯想詞（微軟預設關閉）、其他鍵盤排列 |
| M4 學習系統（程式完成，待實機測試） | 3.2 節的 A、B、C 三層＋遺忘機制 | 常用字詞明顯往前排。2026-11-15 完成 A（常選字加權）與 C（自動學新詞），存在 learning.txt、各程式共用、背景寫檔；B（前後文覆寫）沿用小麥注音的短期模型，不存檔。遺忘鍵 Ctrl+Delete 只清學習資料，不寫排除清單（排除清單留給 M5） |
| M5 分層詞庫＋設定程式（程式完成，待實機測試） | 第 4 節＋詞庫編輯器＋匯入匯出 | 能停用任何一個詞庫包。2026-11-15 完成：小麥詞庫拆成基礎字庫（必要）／一般詞彙／符號與表情三個包（記憶體映射，可個別停用），自訂詞與排除詞（排除可以限定讀音或全部讀音，學到的詞也擋得住），改檔後背景重新載入；Win32 設定程式（工作列按鈕按右鍵）管理詞庫包、自訂詞、排除詞（匯入匯出）、學習記錄。未做：組字中直接加詞的快速鍵、微軟注音使用者詞彙匯入（格式未確認）、使用者自己加的詞庫包 |
| M6 發布（進行中） | x86/x64/ARM64 安裝程式、程式碼簽章、各程式的相容設定檔 | 可以給別人安裝。2026-11-15 定名「微新注音」1.0.0，Inno Setup 安裝檔（版本資料夾、不要求關閉程式、自動加入輸入法清單、應用程式清單可移除）。同日英文名改為 Idealism（唯心論，取「微新」諧音）：檔名 IdealismTip.dll／IdealismSettings.exe／IdealismPad.exe、資料夾 IdealismIME、安裝檔 Idealism-Setup；CLSID、profile GUID、安裝程式 AppId 不變。未做：程式碼簽章；ARM64 依使用者要求暫緩 |

---

## 參考資料

- 微軟 KB：[A user-defined application using message filters may become unresponsive in Windows 10 2004–21H2](https://learn.microsoft.com/en-us/troubleshoot/windows/win32/application-using-message-filters-unresponsive-win10)
- [Excel may stop or close when using new IME in Windows 11](https://support.microsoft.com/en-us/Support/known-issues/excel-may-stop-or-close-when-using-new-ime-in-windows-11)
- [Windows 11 22H2 IME 造成程式無回應（Winaero）](https://winaero.com/a-windows-11-22h2-bug-in-ime-may-cause-apps-to-become-unresponsive)
- [Revert to a previous version of an IME（微軟）](https://eus.prod.support.services.microsoft.com/en-gb/windows/revert-to-a-previous-version-of-an-ime-input-method-editor-adcc9caa-17cb-44d8-b46e-f5b473b4dd77)
- [windows-chewing-tsf 26.1.3.0 釋出](https://chewing.im/posts/2026-01-25-windows-chewing-tsf-26_1_3_0.html)、[25.8.1.0 釋出公告](https://groups.google.com/g/chewing-devel/c/jL1ndsojRNk)、[遷移至 Codeberg](https://groups.google.com/g/chewing-devel/c/Lx2S0r6p9ug)
- [libchewing 架構 RFC](https://hackmd.io/@sysprog/libchewing-rfc)
- [openvanilla/win-mcbopomofo](https://github.com/openvanilla/win-mcbopomofo)、[小麥注音使用手冊（fcitx5）](https://github.com/openvanilla/fcitx5-mcbopomofo/wiki/%E4%BD%BF%E7%94%A8%E6%89%8B%E5%86%8A)
- [PIME](https://awesome.ecosyste.ms/projects/github.com%2Feasyime%2Fpime)
- TSF 文件：[Text Service Registration](https://learn.microsoft.com/en-gb/windows/win32/tsf/text-service-registration)、[Predefined category values](https://learn.microsoft.com/en-ie/Windows/Win32/tsf/predefined-category-values)、[IME requirements（AppContainer／ACL）](https://learn.microsoft.com/nb-no/windows/apps/develop/input/input-method-editor-requirements)
