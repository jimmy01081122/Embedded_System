# basic_temp 報告

## 目錄

- [1. 專案概覽](#1-專案概覽)
- [2. 溫度偵測方法](#2-溫度偵測方法)
- [3. 系統優化設計](#3-系統優化設計)
- [4. E-paper 顯示設計](#4-e-paper-顯示設計)
- [5. Code Review](#5-code-review)
- [6. 基本版結論](#6-基本版結論)

---

## 1. 專案概覽

分析目錄：`/home/a/embedded_system_STM32/basic_temp`

### 主要檔案與用途

- `Src/main.c`
  - 系統初始化、ADC 溫度讀取、UART 輸出、E-paper 更新、按鈕中斷停止流程。
- `Src/epaper_1in02d.c`
  - 1.02 inch e-Paper driver，包含 reset、command/data 傳送、BUSY wait、frame buffer、字型繪製、清白屏、進入 sleep。
- `Inc/epaper_1in02d.h`
  - E-paper 邏輯解析度、controller RAM 尺寸、buffer size、腳位定義與 driver API 宣告。
- `Inc/main.h`
  - CubeMX 產生的板上按鈕、USART2、SWD 腳位 macro。
- `Src/stm32f3xx_hal_msp.c`
  - ADC1、SPI1、USART2 MSP init/deinit；設定 SPI1 SCK/MOSI 與 USART2 TX/RX GPIO alternate function。
- `Src/stm32f3xx_it.c`
  - SysTick 與 EXTI15_10 interrupt handler；PC13 按鈕中斷會呼叫 HAL GPIO EXTI handler。
- `f2.ioc`
  - CubeMX 設定。目標板為 `NUCLEO-F303RE`，MCU 為 `STM32F303RETx`。
- `STM32F303RE_FLASH.ld`
  - Flash/RAM linker script。
- `.project`、`.cproject`、`.mxproject`
  - TrueSTUDIO / CubeMX 專案設定。
- `f2/`
  - 內含一份與根目錄 `Src/`、`Inc/`、`f2.ioc` 相同的巢狀專案副本。依目前檔案比對判斷，它是重複副本，不是另一套功能實作。

### 使用到的周邊

- `main.c`
  - 有使用。所有應用流程集中於此檔。
- E-paper driver
  - 有使用。`main.c` 呼叫 `EPD_Init()`、`EPD_ClearWhite()`、`EPD_ShowString()`、`EPD_Sleep()`。
- GPIO
  - 有使用。PC13 作為按鈕 EXTI；PA0/PA1/PA4/PA6 作為 E-paper BUSY/RST/DC/CS；PA5/PA7 作為 SPI1；PA2/PA3 作為 USART2。
- ADC
  - 有使用。ADC1 讀取 internal temperature sensor 與 VREFINT。
- SPI
  - 有使用。SPI1 以 master mode 傳送 e-Paper command/data。
- UART
  - 有使用。USART2 以 115200 baud 輸出狀態與溫度字串。

### 程式主流程

1. `HAL_Init()` 初始化 HAL、SysTick。
2. `SystemClock_Config()` 設定 HSI + PLL，系統時脈為 72 MHz。
3. 初始化 GPIO、USART2、SPI1、ADC1。
4. 對 ADC1 做 single-ended calibration。
5. UART 印出啟動訊息。
6. 初始化 e-Paper，清白屏，顯示 `MCU Temp: --.-- C`。
7. 進入無限迴圈：
   - 若 PC13 按鈕中斷已設定 `g_epd_stop_request`，清白屏、e-Paper sleep，然後用 `HAL_Delay(1000)` 永久等待。
   - 讀取 MCU internal temperature sensor 與 VREFINT。
   - 格式化為 `MCU Temp: xx.xx C`。
   - UART 輸出並刷新 e-Paper。
   - 再次檢查按鈕。
   - 使用 `Delay_Check_Button(5000)` 等待約 5 秒，等待期間每 10 ms 檢查一次按鈕。

---

## 2. 溫度偵測方法

### 實作判斷

- 是否使用 STM32 Internal Temperature Sensor：有。程式呼叫 `ADC_Read_Channel_Average(ADC_CHANNEL_TEMPSENSOR, ADC_AVERAGE_COUNT)`。
- ADC instance：ADC1。
- 是否讀取 VREFINT：有。程式呼叫 `ADC_Read_Channel_Average(ADC_CHANNEL_VREFINT, ADC_AVERAGE_COUNT)`。
- 是否使用校正值：
  - `TEMPSENSOR_CAL1_ADDR = 0x1FFFF7B8`
  - `TEMPSENSOR_CAL2_ADDR = 0x1FFFF7C2`
  - `VREFINT_CAL_ADDR = 0x1FFFF7BA`
  - 依 STM32F303RE 常用 factory calibration 位址判斷，這些位址合理。
- 溫度校正點：
  - `TEMP_CAL1_C = 30.0f`
  - `TEMP_CAL2_C = 110.0f`
- VREFINT 補償：
  - `ts_corrected = ts_raw * vref_cal / vref_raw`
  - 依目前程式判斷，有用 VREFINT 補償 VDDA 變動。
- 溫度公式：
  - `temperature = ((ts_corrected - ts_cal1) * (110 - 30)) / (ts_cal2 - ts_cal1) + 30`
  - 依目前程式判斷，公式方向正確。
- ADC sample time：
  - `ADC_SAMPLETIME_601CYCLES_5`
  - 對 internal temperature sensor 而言屬於長 sample time，通常足夠。
- 平均濾波：
  - 有。`ADC_AVERAGE_COUNT = 16U`。
  - 每筆讀取間隔 `HAL_Delay(2)`。
- 小數點後兩位：
  - 有。`Format_Temperature_String()` 以四捨五入方式格式化為 `%ld.%02ld`。
- 量測對象：
  - 量測的是 MCU die / junction 溫度，不是環境溫度。

### 問題 / 影響 / 建議修正

- 問題：溫度顯示格式為 `MCU Temp: xx.xx C`，但沒有在畫面上標示這是 MCU 晶片溫度。
  - 影響：使用者可能誤以為是環境溫度。
  - 建議修正：實驗報告與 UI 說明中明確寫為 MCU internal temperature / die temperature。

- 問題：ADC error path 直接進入 `Error_Handler()`，沒有任何錯誤碼、UART 訊息或 e-Paper 錯誤顯示。
  - 影響：ADC timeout、calibration 失敗、channel config 失敗時，現場只能看到系統卡死。
  - 建議修正：至少在錯誤前輸出錯誤來源，或回傳錯誤狀態並顯示 `ADC Error`。

- 問題：`HAL_ADC_PollForConversion(&hadc1, 10)` timeout 寫死為 10 ms。
  - 影響：依目前設定通常足夠，但若 clock 設定被改、debugger 暫停、ADC 狀態異常，會直接卡進 error handler。
  - 建議修正：用 macro 命名 timeout，並在錯誤處理中記錄是哪一個 channel 失敗。

- 問題：平均讀取 TS 與 VREFINT 分成兩批讀取，中間存在時間差。
  - 影響：對慢變的 VDDA 影響很小；若供電快速波動，補償精度會下降。
  - 建議修正：需要更嚴謹時可改為 scan mode 或連續交錯取樣 TS/VREFINT。

- 問題：沒有額外驗證 factory calibration address 對目前晶片型號是否永遠正確。
  - 影響：若移植到非 STM32F303RE 或不同系列，溫度會錯。
  - 建議修正：使用 HAL/CMSIS header 已定義的 calibration macro；或以 `#if defined(STM32F303xE)` 保護。

### 可能誤差來源

- Internal temperature sensor 量到 MCU die 溫度，受 CPU 負載、PLL 72 MHz、UART/SPI/ADC 活動、板上 ST-LINK 與周邊熱源影響。
- Factory calibration 是晶片內部校正，不代表高精度環境溫度計。
- VDDA 雖用 VREFINT 補償，但 ADC noise、VREFINT calibration tolerance、供電 ripple 仍會造成誤差。
- e-Paper refresh 與 UART transmit 會讓 MCU 活動時間增加，依目前程式判斷可能使晶片溫度略高於低功耗版本。
- 需要實機驗證：實際溫度數值準確度、長時間漂移、Nucleo 板供電狀態下的自熱程度。

### 優點、缺點、風險與改善建議

- 優點：
  - 使用 TS_CAL1/TS_CAL2 與 VREFINT_CAL，方法比固定 VDDA 或未校正公式可靠。
  - 16 次平均可降低顯示跳動。
  - 以整數方式格式化兩位小數，避免 `printf` float support 的 toolchain 問題。
- 缺點：
  - 16 次平均加上 `HAL_Delay(2)`，每輪 TS + VREF 約增加 64 ms blocking delay。
  - 未區分 ADC channel 失敗來源。
  - 未在 UI 說明晶片溫度與環境溫度差異。
- 風險：
  - 若 CubeMX 重新生成覆蓋手寫區外變更，ADC 或 clock 設定可能改變。
  - 若移植到其他 STM32 型號，硬編碼校正位址可能錯誤。
- 改善建議：
  - 用型號條件編譯或官方 macro 管理 calibration address。
  - 將 ADC timeout、平均次數、sample time macro 化並加註解。
  - 在報告中校正描述為 MCU die temperature。

---

## 3. 系統優化設計

### 實作判斷

- 低功耗策略：基本上沒有。
- 是否關閉 UART：沒有。USART2 初始化後持續啟用。
- 是否關閉 SPI：沒有。SPI1 初始化後持續啟用。
- 是否關閉 ADC：沒有。ADC1 初始化後持續啟用，只在每次 single conversion 後 `HAL_ADC_Stop()`。
- 是否使用 WFI：沒有。
- 是否使用 STOP mode：沒有。
- 是否使用 Sleep mode：沒有使用 MCU sleep；只有 e-Paper `EPD_Sleep()` 在按鈕停止時被呼叫。
- 是否降低 clock 或 PLL：沒有。系統跑 HSI + PLL 72 MHz。
- 是否避免不必要 e-Paper refresh：沒有。每 5 秒一定刷新一次，即使字串相同。
- 是否有按鈕後停止流程：有。按鈕後清白屏、e-Paper sleep，然後永遠 `HAL_Delay(1000)`。
- idle 是否仍高功耗：是。等待期間使用 `HAL_Delay()`，SysTick 持續運作，UART/SPI/ADC clock 仍保持。

### 問題 / 影響 / 建議修正

- 問題：每 5 秒等待使用 `HAL_Delay()` / polling。
  - 影響：CPU 進入一般 HAL delay 等待，功耗不低。
  - 建議修正：改用 `__WFI()` 搭配 SysTick 或 RTC wakeup；若實驗要求低功耗，應使用 STOP mode 或至少 Sleep mode。

- 問題：UART、SPI、ADC 初始化後未 DeInit 或 disable。
  - 影響：idle 期間週邊 clock 與 GPIO 狀態仍消耗功率。
  - 建議修正：讀取或刷新完成後關閉 ADC/SPI；若不需要 UART debug，初始化後關閉 USART2 並將 PA2/PA3 改為 analog no pull。

- 問題：按鈕停止後仍以 `HAL_Delay(1000)` 永久等待。
  - 影響：不是低功耗停止，SysTick 與 MCU clock 仍持續工作。
  - 建議修正：清白屏與 e-Paper sleep 後，suspend SysTick 並進入 STOP mode 或 infinite WFI。

- 問題：系統時脈固定 72 MHz。
  - 影響：對 5 秒更新一次的溫度顯示而言過高，增加 MCU active power 與自熱。
  - 建議修正：若 SPI 更新速度可接受，改用 HSI 8 MHz 或較低 PLL。

- 問題：每 5 秒都完整 e-Paper refresh。
  - 影響：e-Paper refresh 是耗時且耗電動作，且可能增加面板壽命壓力。
  - 建議修正：若實驗允許，可在顯示字串未變時跳過刷新；若實驗要求每 5 秒更新顯示，則不可跳過，只能優化 refresh 前後的 idle 功耗。

---

## 4. E-paper 顯示設計

### 腳位與 SPI 配置

依 `epaper_1in02d.h`、`main.c`、`.ioc` 判斷：

- SPI1 SCK：PA5。
- SPI1 MOSI：PA7。
- EPD BUSY：PA0 input。
- EPD RST：PA1 output，初始 high。
- EPD DC：PA4 output，初始 low。
- EPD CS：PA6 output，初始 high。
- MISO 未使用，符合單向寫入 e-Paper 的使用方式。

### Driver 功能

- `EPD_Init()`：有。
- `EPD_ClearWhite()`：有。
- `EPD_ShowString()`：有。
- `EPD_Sleep()`：有。
- `EPD_ShowTestPattern()`：有宣告與定義，但主流程未呼叫。

### BUSY 判斷

- 程式註解指出 `BUSY = Low` 代表 busy，`BUSY = High` 代表 idle。
- `EPD_WaitUntilIdle()` 在 `HAL_GPIO_ReadPin(...) == GPIO_PIN_RESET` 時等待，符合註解邏輯。
- 需要實機驗證：實際 1.02 inch e-Paper 模組的 BUSY polarity 是否與此 driver 假設一致。

### Frame buffer 與座標

- 邏輯顯示座標：`EPD_LOG_WIDTH = 128`、`EPD_LOG_HEIGHT = 80`。
- Controller RAM：`EPD_RAM_WIDTH = 80`、`EPD_RAM_HEIGHT = 128`。
- Buffer size：`80 * 128 / 8 = 1280 bytes`，尺寸合理。
- `EPD_Init()` 中 TRES 設定為 HRES 80、VRES 128，與 buffer macro 一致。
- `EPD_ROTATE_MODE = 1` 將 128x80 landscape 映射到 80x128 controller RAM。
- `EPD_ShowString()` 在 `(8, 36)` 繪製文字。字串 `MCU Temp: xx.xx C` 約 17 字元，5x7 字型加 1 pixel spacing 約 102 pixels，放入 128 width 合理。

### 更新週期與按鈕流程

- 開機後：`EPD_Init()` → `EPD_ClearWhite()` → `EPD_ShowString("MCU Temp: --.-- C")`。
- 每輪：`EPD_ShowString(text)`，約每 5 秒一次。
- 按鈕後：`EPD_ClearWhite()` → `EPD_Sleep()` → 永久 delay。
- 按鈕後白屏流程沒有重新 `EPD_Init()`，但基本版在正常運作期間沒有呼叫 `EPD_Sleep()`，所以按鈕當下 e-Paper 尚未 deep sleep；依目前程式判斷白屏命令有機會正常送出。

### 問題 / 影響 / 建議修正

- 問題：`EPD_WaitUntilIdle()` 沒有 timeout。
  - 影響：BUSY 腳位接錯、面板未供電、BUSY polarity 相反時，程式會永久卡死。
  - 建議修正：加入 timeout，失敗時回傳錯誤並讓主流程顯示或輸出錯誤。

- 問題：e-Paper command 序列與 magic number 缺少 datasheet 來源標註。
  - 影響：後續維護者難以確認 `0x00/0x4F`、`0x17/0xA5`、`0x61/0x50/0x80` 是否正確。
  - 建議修正：將 command macro 化，例如 `EPD_CMD_PSR`、`EPD_CMD_TRES`，並註明面板/controller datasheet 版本。

- 問題：driver 所有 SPI transmit 都使用 `HAL_MAX_DELAY`。
  - 影響：SPI 錯誤或硬體異常時可能永久 blocking。
  - 建議修正：改用有限 timeout，並回傳 status。

- 問題：`EPD_Sleep()` 送 deep sleep 後沒有 `EPD_WaitUntilIdle()`。
  - 影響：依目前程式問題不一定會出現，但若 deep sleep 需要 busy settle，可能太早進入後續流程。
  - 建議修正：需要實機驗證；若 datasheet 要求，sleep command 後加入 BUSY wait 或規定延遲。

- 問題：5x7 字型只支援目前字串需要的部分字元。
  - 影響：其他訊息可能顯示為空白。
  - 建議修正：若未來要顯示錯誤或單位符號，補齊字型或明確限制 API。

---

## 5. Code Review

### 明確 bug

- 未發現會直接導致 C 語法錯誤的漏分號或大括號錯位。
- 未發現 `HAL_GPIO_EXTI_Callback` 重複定義。
- 未發現函式先呼叫後未宣告造成 implicit declaration；基本版使用函式定義順序避免此問題。

### 潛在 bug

- 問題：`main.c` 中 `ADC_HandleTypeDef hadc1;` 已是全域定義，後面 USER CODE PV 又 `extern ADC_HandleTypeDef hadc1;`。
  - 影響：不會造成重複定義，但在同一個 C 檔內 `extern` 自己的全域變數是多餘且混亂。
  - 建議修正：移除同檔內的 `extern hadc1/hspi1/huart2`。

- 問題：`EPD_WaitUntilIdle()` 無 timeout。
  - 影響：硬體異常時整個系統永久卡死。
  - 建議修正：加入 timeout 與錯誤處理。

- 問題：按鈕停止後無 debounce。
  - 影響：按鈕 bounce 對單一 `g_epd_stop_request` flag 影響有限，但仍可能在 e-Paper refresh 中途觸發停止請求，直到下一次檢查才處理。
  - 建議修正：在 callback 中只設 flag 可接受；若要更乾淨，可加入軟體 debounce 時間。

- 問題：按鈕停止流程若發生在 `EPD_ShowString()` 或 `EPD_ClearWhite()` 期間，不會中斷 SPI 傳輸或 BUSY wait。
  - 影響：按鈕反應可能延遲到 e-Paper refresh 完成。
  - 建議修正：在 e-Paper wait 迴圈中支援可取消或 timeout。

### 可維護性問題

- 問題：大量應用邏輯寫在 `main.c`，缺少 `temperature.c`、`power.c`、`app.c` 等模組化分層。
  - 影響：後續新增低功耗或錯誤處理時，`main.c` 會快速膨脹。
  - 建議修正：將溫度讀取、顯示、狀態機分離。

- 問題：e-Paper driver 內註解出現亂碼。
  - 影響：維護者無法可靠閱讀原始說明。
  - 建議修正：統一檔案編碼為 UTF-8 或改成 ASCII 英文註解。

- 問題：magic number 多。
  - 影響：校正位址、e-Paper command、更新週期、ADC 平均次數、timeout 意義不明。
  - 建議修正：以具名 macro 或 enum 管理，並加資料來源註解。

- 問題：根目錄與 `f2/` 有重複專案。
  - 影響：後續修改可能改錯副本。
  - 建議修正：保留單一專案根目錄，或在 README 明確標示哪一份是主專案。

### HAL 使用

- ADC：
  - `HAL_ADCEx_Calibration_Start()` 在初始化後呼叫，合理。
  - 每次讀 channel 前重新 `HAL_ADC_ConfigChannel()`，可行。
  - 每次 conversion 後 `HAL_ADC_Stop()`，合理。
- SPI：
  - e-Paper command/data 使用 blocking `HAL_SPI_Transmit()`，簡單但缺少 timeout 控制。
- UART：
  - `HAL_UART_Transmit()` 使用 `HAL_MAX_DELAY`，debug 方便但故障時可能卡死。
- GPIO EXTI：
  - ISR 只設 flag，這是正確方向。

### 未使用函式與變數

- `EPD_ShowTestPattern()` 未被主流程使用。
- `PA0` 被註解為 input，實際作為 EPD BUSY，合理但 `main.h` 沒有以 EPD 名稱呈現。
- `extern ADC_HandleTypeDef hadc1/hspi1/huart2` 在同檔多餘。

### Blocking delay 與功耗問題

- `ADC_Read_Channel_Average()` 中每次 sample 後 `HAL_Delay(2)`。
- `Delay_Check_Button()` 每 10 ms `HAL_Delay(10)`。
- 按鈕停止後永久 `HAL_Delay(1000)`。
- e-Paper reset 與 sleep 使用固定 `HAL_Delay()`。
- 影響：低功耗表現差，且 refresh / wait 期間反應時間受 blocking 流程限制。

---

## 6. 基本版結論

- 功能是否符合要求：
  - 大致符合「讀取 STM32 internal temperature sensor 並顯示於 e-Paper」。
- 溫度偵測是否可信：
  - 依目前程式判斷，使用 TS_CAL1/TS_CAL2/VREFINT_CAL 與 VREFINT 補償，計算方法可信。
  - 數值代表 MCU 晶片溫度，不代表環境溫度。
  - 實際準確度需要實機驗證。
- E-paper 顯示是否完整：
  - 基本功能完整，有 init、清白屏、顯示字串、sleep。
  - BUSY wait 無 timeout 是主要風險。
- 按鈕停止功能是否完整：
  - 功能上可停止更新並白屏，但停止後仍用 `HAL_Delay()`，不是低功耗停止。
- 功耗是否有改善空間：
  - 有很大改善空間。UART/SPI/ADC 未關閉，MCU 未進 WFI/STOP，clock 仍 72 MHz。
- 建議修改優先順序：
  - 1. 替 e-Paper BUSY wait 與 SPI/UART/ADC blocking 操作加入 timeout 與錯誤處理。
  - 2. 按鈕停止後改為 e-Paper sleep + suspend SysTick + STOP mode 或 WFI。
  - 3. 關閉不需要的 UART，並在 idle 關閉 ADC/SPI。
  - 4. 將 magic number macro 化並補資料來源註解。
  - 5. 移除重複巢狀專案或明確標示主專案。
