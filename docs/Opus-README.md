# 微新注音（IdealismBopomofo）

一套操作跟微軟注音一樣的 Windows 注音輸入法，目標是：

1. **不會讓舊版軟體卡死**（主要測試對象：3ds Max）
2. 選字更聰明，會學習使用者習慣；自訂詞庫要很方便
3. 可以移除預設詞庫

調查和設計請看 [docs/01-survey.md](docs/01-survey.md)。

## 功能（1.0.0）

- 傳統 TSF 文字服務，完全 in-process、同步處理按鍵，打字時不呼叫任何其他程序，也不會等待
- 標準（大千）鍵盤排列；整句自動選字（小麥注音的 Gramambular 2 引擎與詞庫，MIT 授權）
- 候選字窗（按鍵依[微軟官方說明](https://support.microsoft.com/zh-tw/windows/microsoft-%E7%B9%81%E9%AB%94%E4%B8%AD%E6%96%87%E8%BC%B8%E5%85%A5%E6%B3%95-ef596ca5-aff7-4272-b34b-0ac7c2631a38)）：`↓` 叫出，`1`–`9` 選字，`↑↓` 移動（移過最後一列就換頁）、Enter 確定，空白鍵／PageDown 下一頁，PageUp 上一頁，Esc 關閉
- 展開檢視：候選字窗開著時按 `→` 或 Tab 展開成多欄表格，`←→` 換欄，數字鍵選目前那一欄；在第一欄按 `←` 收合
- 選字對象：游標**後面**的字或詞；選完後游標往後跳過選的字數。游標在最後面時選最後一個字，按 `↓` 時游標會先往前移一格。`←→`、Home、End 移動游標
- 候選字窗的第一個選項固定是目前顯示的字（詞），其他的依分數（含學習結果）排在後面；單字的候選字最後一項是注音本身（例如 ㄏㄠˇ），用來直接打出注音
- 組字中按空白鍵是打出空格，不會叫出候選字窗
- 全形標點：`Ctrl+符號鍵`（`Ctrl+,` ，、`Ctrl+.` 。、`Ctrl+'` 、、`Ctrl+;` ；、`Ctrl+[` 「、`Ctrl+Shift+/` ？ 等），**中文和英文模式都有效**。Ctrl+字母和 Ctrl+數字不受影響
- 中文模式下也可以先按 `` ` `` 再按符號鍵（`` ` `` `,` 出 ，）
- 內碼輸入：`` ` `` U + Unicode + 空白（`` ` ``U4e00 空白 → 一），`` ` `` B + 四碼 Big5（`` ` ``BA440 → 一）
- 符號鍵盤：`Ctrl+Alt+,` 打開，按鍵盤上對應的鍵輸入符號（可以連續輸入），PageUp／PageDown 換頁，Esc 或再按一次 `Ctrl+Alt+,` 關閉。開著時其他鍵照常：沒在組字時 Backspace、Enter、方向鍵、空白、Ctrl 快捷鍵都交給程式，組字中則用來編輯組字區。有標點、括號、數學、箭頭與圖形、序號、希臘字母、單位與貨幣、框線、平假名、片假名、注音符號共 13 頁
- 候選字窗沒開時按 `↑`：直接換成下一個候選字（不開候選字窗），連按繼續往下換
- `Shift+符號鍵` 和 `[ ] ' \ =` 這些鍵照鍵盤上印的打出半形（`Shift+/` 是 `?`）；組字中打的半形符號和 Shift+字母會留在組字區，跟句子一起送出
- `Shift+Space` 切換全／半形：全形時空白、符號、英文字母和數字都打出全形（中文和英文模式都有效），工作列圖示變成橘色
- Shift 單按切換中／英（按著 Shift 點滑鼠不算），工作列有中／英圖示（可以點）。程式把輸入法關掉時（例如 3ds Max），按 Shift 或點圖示會直接回到中文；CapsLock 和 Shift+字母直接輸入英文
- 學習（存在 `%APPDATA%\IdealismIME\learning.txt`，所有程式共用，永久保存）：
  - **常選的字往前排**：送出句子時統計你實際用的字。用得多的字在候選字窗裡往前移，分數夠接近時會變成預設字。半衰期 30 天，久沒用會慢慢退回去
  - **自動學新詞**：你連續三次把同樣幾個字（2–4 個字）湊在一起，就會變成一個詞，之後自動選出來。在學到的詞的位置改選別的字詞，這個詞就會被取消
  - **前後文記憶**：在同樣的前後文改選過的字，下次自動選（短期記憶，半衰期 1.5 小時，各程式各自記）
  - **遺忘**：候選字窗裡按 `Ctrl+Delete`，就忘掉對反白那個字詞學到的東西
  - 學習資料是純文字檔，可以直接看、直接改；**刪掉 `learning.txt` 就全部清除**
  - 寫檔一律在背景執行緒做，打字的路徑上只動記憶體
- 分層詞庫：
  - **詞庫包**可以個別停用：基礎字庫（必要）、一般詞彙、符號與表情（打注音時出現的 emoji、國旗、⑧ 這類符號）
  - **自訂詞**（`user-phrases.txt`）永遠排在最前面；**排除詞**（`excluded-phrases.txt`）不會再出現，可以只排除某個讀音下的詞，或不寫注音就所有讀音都排除
  - 檔案格式同小麥注音：一行一筆 `詞 ㄉㄨˊ-ㄧㄣ`
  - 改了檔案不用重開程式：切回打字的程式時會在背景重新載入
- **輸入法整合器**（工作列「中／英」按鈕**按右鍵**，或開始功能表）：永遠在最上層、點它不會搶走焦點，字會直接送到正在打字的程式
  - 手寫：在格子裡寫一個字，寫完一筆就辨識（用 Windows 內建的繁體中文手寫辨識），按候選字送出；滑鼠移到候選字上會顯示它的**所有讀音**和字碼，送出後讀音還會留著。右鍵或「退回一筆」可以擦掉最後一筆
  - 符號：中文標點、全形英數、拉丁字母（含重音）、國際音標、希臘、西里爾、平假名、片假名、注音、數學、上下標、箭頭、序號、單位、貨幣、幾何圖形、雜項符號、框線、表情符號共 19 類
- **設定程式**「注音輸入法設定」（整合器右上角的「設定…」，或開始功能表）：
  - 詞庫包：勾選開關
  - 自訂詞／排除詞：新增、修改、刪除、搜尋、匯入、匯出（UTF-8、UTF-16、Big5 文字檔，**可以直接匯入微軟注音匯出的詞典**）。新增時自動填常用讀音並列出破音字；注音也可以用英文鍵盤打大千按鍵（`cl3` = ㄏㄠˇ）
  - 學習記錄：看學到的詞和常用字詞，選錯的可以「忘記」或「改成自訂詞」，也可以全部清除
- 小麥注音詞庫裡的日期巨集（`MACRO@…`）會原樣出現在候選字裡，拆詞庫包時已拿掉

- UILess 模式：全螢幕遊戲等自己畫輸入法介面的程式，會從輸入法拿到候選字清單（和符號鍵盤）自己顯示，輸入法不再蓋一個視窗上去

還沒做：其他鍵盤排列、聯想詞、程式碼簽章、ARM64（暫緩）。

## 安裝

執行 `Idealism-Setup-<版本>.exe`（需要系統管理員權限），然後在輸入法清單（Win+Space）選「微新注音」。

- 每次安裝都放進 `C:\Program Files\IdealismIME\<建置時間>\` 這個新資料夾，所以 3ds Max 等程式開著也可以直接更新；**已經開著的程式要重開才會用新版**（工作列的中／英按鈕屬於檔案總管：登出再登入，或重新啟動檔案總管）
- 舊版資料夾會自動清掉；還在使用中的檔案在重新開機後刪除
- 從改名前的版本（`ZhuyinIME` 資料夾）升級：安裝程式會清掉 `C:\Program Files\ZhuyinIME`；個人資料要自己把 `%APPDATA%\ZhuyinIME` 裡的檔案搬到 `%APPDATA%\IdealismIME`（格式相同，舊的學習記錄也讀得懂）
- 移除：設定 > 應用程式 > 微新注音。會問要不要一併刪除學習記錄和自訂詞（`%APPDATA%\IdealismIME`），預設保留
- 自己畫輸入法介面的遊戲（Scaleform，例如 World of Warships）要把輸入法加進遊戲的 `ime_config.xml`：`imeName` 是「微新注音」，`imeGuid` 是 `{84EF98CE-13EF-4565-8250-EA8A42727F5D}`，`Tag` 用 `GFxIME_Ch_Trad_NewPhonetic`

## 編譯

需要 Visual Studio 2026（C++ 桌面開發、x86/x64 工具鏈）、Python 3.9+（建置詞庫用）、Inno Setup 6（做安裝檔用）。

```bash
git clone <repo> && cd <repo>
```

```bash
pwsh -NoProfile -File tools/setup_third_party.ps1
```

```bash
pwsh -NoProfile -File tools/package.ps1
```

`setup_third_party.ps1` 取得小麥注音（git submodule）並套上 `patches/mcbopomofo-msvc.patch`。`package.ps1` 會先跑 `tools/build.ps1`：建置詞庫（`data\data.txt` 再拆成 `data\packs.txt` 和各詞庫包）、x64 和 x86 的 `IdealismTip.dll`、設定程式、整合器，並執行全部測試；再用 Inno Setup 做出 `dist\Idealism-Setup-<版本>.exe`。版本號在 `src/Version.h`。

開發中想直接裝剛編好的版本，也可以用系統管理員身分執行 `tools/install.ps1`／`tools/uninstall.ps1`。

## 專案結構

```
src/engine/   注音引擎層（純 C++，不依賴 TSF）：音節、組字、候選字、學習
src/tip/      TSF 文字服務 DLL：COM 註冊、按鍵、組字、候選字窗、中／英按鈕
src/settings/ 設定程式（Win32）：詞庫包、自訂詞、排除詞、學習記錄
src/pad/      輸入法整合器（Win32）：手寫辨識、符號表
src/compat/   讓小麥注音引擎在 MSVC 編譯的空白 POSIX 標頭
tests/        engine_test（引擎與學習）、tip_smoke（TSF 啟動／按鍵／停用／背景寫檔／UILess）、pad_test（手寫辨識）
tools/        建置、打包（package.ps1）、詞庫建置與拆包（split_packs.py）、開發用安裝、圖示產生腳本
installer/    Inno Setup 安裝腳本
third_party/  McBopomofo（小麥注音，MIT）：Source/Engine、Source/Data
patches/      對 McBopomofo 的修改（目前只有 MSVC 相容性修正）
docs/         調查與設計文件
```

## 第三方元件

- [McBopomofo 小麥注音](https://github.com/openvanilla/McBopomofo)（MIT License）：選字引擎與詞庫。
  `third_party/McBopomofo` 是 sparse clone，只含 `Source/Engine` 和 `Source/Data`；
  重新取得後要套用 `patches/mcbopomofo-msvc.patch`。它的授權聲明在 [`THIRD-PARTY-NOTICES.txt`](../THIRD-PARTY-NOTICES.txt)，安裝檔也會一起裝進安裝資料夾。

## 授權

[MIT License](../LICENSE)，Copyright (c) 2026 lkk9898969。

可以自由使用、修改、散佈（包括商業用途），只要保留版權和授權聲明。本軟體不提供任何擔保。

散佈時（原始碼或安裝檔）要同時附上 `LICENSE` 和 `THIRD-PARTY-NOTICES.txt`：後者是小麥注音的 MIT 授權聲明，涵蓋選字引擎和詞庫資料。Inno Setup 安裝檔會自動把這兩個檔案裝進安裝資料夾（`LICENSE.txt`、`THIRD-PARTY-NOTICES.txt`）。

選字詞庫完全來自小麥注音。使用者自己匯入的詞典（例如從微軟注音匯出的）只存在 `%APPDATA%\IdealismIME`，不屬於本專案，也不會放進原始碼或安裝檔。
