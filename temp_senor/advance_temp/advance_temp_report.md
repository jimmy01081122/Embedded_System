# advance_temp 報告

## 目錄

- [1. 專案概覽](#1-專案概覽)
- [2. 溫度偵測方法](#2-溫度偵測方法)
- [3. 系統優化設計](#3-系統優化設計)
- [4. E-paper 顯示設計](#4-e-paper-顯示設計)
- [5. Code Review](#5-code-review)
- [6. 優化版結論](#6-優化版結論)

---

## 1. 專案概覽

分析目錄：`/home/a/embedded_system_STM32/advance_temp`

### 主要檔案與用途

- `Src/main.c`
  - 系統初始化、ADC 溫度讀取、週邊低功耗關閉、WFI 等待、STOP mode、按鈕白屏停止流程。
- `Src/epaper_1in02d.c`
  - e-Paper driver。與基本版相比，功能類似，但字型表較精簡，測試圖樣文字不同。
- `Inc/epaper_1in02d.h`
  - E-paper 解析度、buffer size、腳位 macro、API 宣告。
- `Inc/main.h`
  - CubeMX 產生的按鈕、USART2、SWD 腳位定義。
- `Src/stm32f3xx_hal_msp.c`
  - ADC1、SPI1、USART2 MSP init/deinit。與基本版相同。
- `Src/stm32f3xx_it.c`
  - SysTick 與 EXTI15_10 handler。與基本版相同。
- `f2.ioc`
  - CubeMX 設定。與基本版相同：ADC1 temperature sensor / VREFINT、SPI1、USART2、PC13 EXTI。
- `STM32F303RE_FLASH.ld`
  - Linker script。
- `.project`、`.cproject`、`.mxproject`
  - TrueSTUDIO / CubeMX 專案設定。


### 和基本版相比的主要差異

- 增加 `UART_Disable_For_Power()`。
- 增加 `SPI_Disable_For_Power()`。
- 增加 `ADC_Disable_For_Power()`。
- 溫度讀取改由 `MCU_Read_Temperature_Power()` 包裝，讀取前初始化 ADC，讀取後關閉 ADC。
- e-Paper 每次更新前重新 `MX_SPI1_Init()` 與 `EPD_Init()`，更新後 `EPD_Sleep()` 並關閉 SPI。
- 5 秒等待改用 `__WFI()`，但 SysTick 仍每 1 ms 喚醒。
- 按鈕後會清白屏、e-Paper sleep、關閉周邊，最後進入 STOP mode。
- `ADC_AVERAGE_COUNT` 從 16 降為 4，降低取樣耗時與 active time。
- `SKIP_EPD_REFRESH_IF_TEXT_SAME` 設為 0，表示目前仍每 5 秒刷新，不跳過相同字串。

### 主流程

1. `HAL_Init()`。
2. `SystemClock_Config()` 設定 HSI + PLL 72 MHz。
3. 初始化 GPIO、USART2、SPI1、ADC1。
4. 啟用 PWR clock。
5. 關閉 UART。
6. 初始化 e-Paper 後立即 `EPD_Sleep()`。
7. 再次啟用 PWR clock、再次關閉 UART。
8. 關閉 SPI 與 ADC。
9. 進入無限迴圈：
   - 若按鈕已觸發，重新啟用 SPI，`EPD_Init()` → `EPD_ClearWhite()` → `EPD_Sleep()`，關閉 SPI，進入 final STOP。
   - 初始化 ADC、校正、讀溫度、關閉 ADC。
   - 格式化為 `MCU Temp: xx.xx C`。
   - 重新啟用 SPI，`EPD_Init()` → `EPD_ShowString(text)` → `EPD_Sleep()`，關閉 SPI。
   - 使用 `LowPower_Delay_Check_Button(5000)` 等待 5 秒，等待中執行 `__WFI()`。

---

## 2. 溫度偵測方法

### 實作判斷

- ADC instance：ADC1。
- Internal Temperature Sensor：有使用 `ADC_CHANNEL_TEMPSENSOR`。
- VREFINT 補償：有使用 `ADC_CHANNEL_VREFINT`。
- 校正值位址：
  - `TEMPSENSOR_CAL1_ADDR = 0x1FFFF7B8`
  - `TEMPSENSOR_CAL2_ADDR = 0x1FFFF7C2`
  - `VREFINT_CAL_ADDR = 0x1FFFF7BA`
- 公式：
  - 先以 `ts_raw * vref_cal / vref_raw` 對 VDDA 變化補償。
  - 再用 30 C 與 110 C 兩點線性內插。
- 平均次數：
  - `ADC_AVERAGE_COUNT = 4U`。
  - 相比基本版 16 次，active time 較低，但抗雜訊能力較弱。
- ADC sample time：
  - `ADC_SAMPLETIME_601CYCLES_5`，對內部溫度感測器合理。
- 讀取後是否關閉 ADC：
  - 有。`MCU_Read_Temperature_Power()` 呼叫 `ADC_Disable_For_Power()`。
- 是否每 5 秒重新讀取：
  - 有。每輪 loop 都重新讀取一次，之後 WFI 等待 5 秒。
- 顯示格式：
  - `Format_Temperature_String()` 產生 `MCU Temp: xx.xx C`。
- 溫度計算與功耗取捨：
  - 4 次平均降低功耗與延遲，但溫度值可能比基本版更跳。
  - 每次讀取前重新 ADC init + calibration，增加短暫 active time，但 idle 時 ADC 可完全關閉



## 3. 系統優化設計

### UART

- 是否真的關閉：
  - 有。`UART_Disable_For_Power()` 呼叫 `HAL_UART_DeInit(&huart2)` 與 `__HAL_RCC_USART2_CLK_DISABLE()`。
- USART2 clock 是否 disabled：
  - 有。
- PA2 / PA3 是否改成低功耗狀態：
  - 有。設定為 `GPIO_MODE_ANALOG`、`GPIO_NOPULL`。
- 問題 / 影響 / 建議修正：
  - 問題：`UART_Disable_For_Power()` 在 startup 連續呼叫兩次。
  - 影響：功能上多半無害，但顯示流程混亂，表示初始化/關閉狀態管理不乾淨。
  - 建議修正：只保留一次，或建立明確的 `App_LowPowerInit()`。

### SPI

- SPI 是否在 e-Paper 更新後關閉：
  - 有。每次 `EPD_ShowString()` 或 `EPD_ClearWhite()` 後呼叫 `SPI_Disable_For_Power()`。
- PA5 / PA7 關閉 SPI 後是否避免浮接：
  - 有。先將 PA5/PA7 寫 low，再設定為 output push-pull low。
- E-paper 控制腳狀態：
  - CS 設 high，DC 設 low，RST 設 high。
- 風險：
  - `SPI_Disable_For_Power()` 呼叫 `HAL_SPI_DeInit()` 後又手動 `__HAL_RCC_SPI1_CLK_DISABLE()`，MSP deinit 已經關閉 clock，重複但通常無害。
  - PA5/PA7 output low 是否符合特定 e-Paper 模組 sleep 時序，需要實機驗證；若面板端有 pull-up 或 level shifting，可能造成微小漏電。

### ADC

- 是否在使用後 DeInit 或 disable：
  - 有。`ADC_Disable_For_Power()` 呼叫 `HAL_ADC_DeInit()` 與 `__HAL_RCC_ADC12_CLK_DISABLE()`。
- 風險：
  - `HAL_ADC_MspDeInit()` 已 disable ADC12 clock，函式又手動 disable 一次，重複但通常無害。
  - 每輪重新 `MX_ADC1_Init()`，若未來 ADC MSP 加入 GPIO 或 DMA 設定，需確保 deinit/init 成對完整。

### WFI / STOP / SysTick

- 是否使用 WFI 等待 5 秒：
  - 有。`LowPower_Delay_Check_Button()` 在 while loop 中呼叫 `__WFI()`。
- 是否使用 STOP mode：
  - 有，但只在按鈕停止後的永久停止流程使用。
- 是否每 5 秒等待期間進 STOP：
  - 沒有。一般週期等待只是 WFI sleep，SysTick 每 1 ms 喚醒。
- 是否有 SysTick 持續喚醒造成耗電：
  - 有。`LowPower_Delay_Check_Button()` 依賴 `HAL_GetTick()`，SysTick 必須持續跑，因此 WFI 約每 1 ms 被 SysTick 喚醒。
- 按鈕後是否確實進入永久停止狀態：
  - 依目前程式判斷，是。`Enter_Final_Stop()` suspend tick 後在 while loop 中反覆進 STOP mode。
  - 需要實機驗證：PC13 EXTI 仍啟用，按鈕或雜訊可喚醒 STOP，但程式會回到 while loop 再進 STOP。
- 是否有 PLL / System clock 降頻：
  - 沒有。仍為 HSI + PLL 72 MHz。
- 是否避免溫度未變時刷新 e-Paper：
  - 目前沒有。`SKIP_EPD_REFRESH_IF_TEXT_SAME = 0`，所以每 5 秒一定 refresh。
  - 若改為 1，會降低功耗，但可能違反「每 5 秒更新畫面」的實驗要求。若要求是每 5 秒量測與刷新，不能啟用跳過刷新；若要求是每 5 秒檢查溫度，顯示可不變，則可接受。

### 低功耗設計可能破壞功能處

- e-Paper deep sleep 後必須 reset/init 才能再接收命令。
  - 優化版每次更新前都有 `EPD_Init()`，符合這個要求。
- SPI DeInit 後必須重新 Init 才能使用。
  - 優化版每次 e-Paper 操作前都有 `MX_SPI1_Init()`。
- ADC DeInit 後必須重新 Init 與校正。
  - 優化版每次讀取前有 `MX_ADC1_Init()` 與 calibration。
- 風險：這些手寫流程都在 `main.c`，CubeMX 不知道完整狀態機；未來重新 generate 或修改 init 順序容易破壞。

### 優化、功能確認

1. 按鈕後進入 STOP mode
2. UART 關閉與 PA2/PA3 analog：有效，尤其 Nucleo USART2 接 ST-LINK VCP 時。
3. SPI 更新後關閉、PA5/PA7 固定低
4. ADC 讀完後 DeInit：有效，但 ADC active 時間短，節省幅度可能小於 UART/clock。
5. WFI 等待 5 秒：可能有效但有限，因 SysTick 每 1 ms 喚醒。

---

## 4. E-paper 顯示設計

### 更新與 sleep 流程

- 每次更新前是否 Init：
  - 有。主迴圈中每次 `EPD_ShowString()` 前都呼叫 `MX_SPI1_Init()` 與 `EPD_Init()`。
- 更新後是否 Sleep：
  - 有。每次顯示後呼叫 `EPD_Sleep()`。
- 按鈕後白屏流程：
  - 是 `MX_SPI1_Init()` → `EPD_Init()` → `EPD_ClearWhite()` → `EPD_Sleep()` → `SPI_Disable_For_Power()` → STOP。
  - 符合「Init → ClearWhite → Sleep → STOP」。
- 是否有 `EPD_Sleep()` 後未重新 Init 導致 `ClearWhite` 無效風險：
  - 按鈕流程有重新 `EPD_Init()`
  - Startup 階段 `EPD_Init()` 後直接 `EPD_Sleep()`，未清白屏是為了先讓面板睡眠，不是顯示流程。

### BUSY wait

- `EPD_WaitUntilIdle()` 使用 `HAL_Delay(10)` polling，不使用 WFI。
- 問題 / 影響 / 建議修正：
  - 問題：refresh busy 期間 CPU active delay。
  - 影響：e-Paper refresh 是耗時動作，這段功耗仍偏高。
  - 建議修正：可在 BUSY wait 內使用 `__WFI()` 或帶 timeout 的低功耗等待；但要注意 SysTick 與 EXTI 行為。

### Frame buffer 與座標

- Buffer size：`80 * 128 / 8 = 1280 bytes`，正確。
- TRES：HRES 80、VRES 128，與 RAM macro 一致。
- 邏輯座標：128 x 80 landscape。
- Rotation：`EPD_ROTATE_MODE = 1`。依目前程式判斷，能將邏輯座標轉成 controller RAM 座標。
- 字串位置：`EPD_DrawString5x7(8, 36, text)`，`MCU Temp: xx.xx C` 可放入 128x80。
- 需要實機驗證：rotation 是否符合實際面板方向；若方向錯，程式註解也表示可調整 `EPD_ROTATE_MODE`。

### 每 5 秒更新

- 目前每輪都讀溫度並 refresh e-Paper，等待 5 秒。
- `SKIP_EPD_REFRESH_IF_TEXT_SAME` 設為 0，所以不會跳過刷新。
- 若改成 1：
  - 優點：溫度字串相同時可省下 e-Paper refresh 功耗。
  - 風險：若實驗明確要求「每 5 秒更新畫面」，跳過刷新會違反要求。
  - 建議：報告中若主張「每 5 秒更新」，應保持 0；若主張「每 5 秒量測，畫面只在變更時刷新」，才可設為 1。

### 問題 / 影響 / 建議修正

- 問題：`EPD_WaitUntilIdle()` 沒有 timeout。
  - 影響：BUSY 腳錯誤、面板未接、面板故障時會永久卡住，且不會進低功耗。
  - 建議修正：加入 timeout，回傳錯誤，必要時停止刷新並進入安全低功耗狀態。

- 問題：e-Paper driver 的 magic number 缺少正式命名。
  - 影響：難以 review command sequence 是否正確。
  - 建議修正：建立 command macro 與 datasheet reference comment。

- 問題：`EPD_Sleep()` 使用 `HAL_Delay(100)`，未確認 BUSY。
  - 影響：如果面板 deep sleep 有忙碌期，可能過早關閉 SPI 或進 STOP。
  - 建議修正：需要實機驗證；若 datasheet 要求，sleep 後加入 BUSY wait。

---

## 5. Code Review

### 明確 bug

- 未發現 C 語法層級的漏分號或大括號錯位。
- 未發現重複變數定義。
- 未發現 `HAL_GPIO_EXTI_Callback` 重複定義。
- 未發現 implicit declaration；優化版已為主要 static functions 放 prototype。

### 潛在 bug

- 問題：`UART_Disable_For_Power()` 被呼叫兩次。
  - 影響：多半不致命，但表示初始化流程不乾淨。
  - 建議修正：保留一次即可。

- 問題：`ADC_Disable_For_Power()`、`SPI_Disable_For_Power()` 在 final stop 中可能對已 DeInit 的 peripheral 再 DeInit。
  - 影響：HAL 通常可承受，但狀態機不夠清楚。
  - 建議修正：用 state flag 或集中化 power manager 避免重複 deinit。

- 問題：STOP mode 醒來後沒有重新配置 clock。
  - 影響：目前 final stop loop 醒來後只是再進 STOP，不繼續執行功能，因此可接受。若未來 STOP 用於週期性 wakeup，必須在醒來後重新 `SystemClock_Config()`。
  - 建議修正：若引入 RTC wakeup 週期更新，STOP wake 後重建 PLL/system clock。

- 問題：`LowPower_Delay_Check_Button()` 用 `__WFI()` 但沒有處理 tickless。
  - 影響：SysTick 每 1 ms 喚醒，節能有限。
  - 建議修正：若追求低功耗，用 RTC/LPTIM wakeup 搭配 suspend tick；或接受目前 WFI 作為簡化版本。

- 問題：`EPD_WaitUntilIdle()` 與 SPI transmit 無 timeout。
  - 影響：硬體異常時卡死，且可能卡在 active power 狀態。
  - 建議修正：所有硬體 wait 加 timeout。

### 低功耗相關 bug / 風險

- 問題：一般 5 秒等待未進 STOP mode。
  - 影響：低功耗效果有限。
  - 建議修正：用 RTC wakeup 每 5 秒喚醒，讀 ADC、刷新 e-Paper，再回 STOP。

- 問題：SysTick 持續喚醒。
  - 影響：WFI sleep 被 1 kHz interrupt 打斷，平均功耗不會接近 STOP。
  - 建議修正：若保留 WFI，至少評估 `HAL_SuspendTick()` 搭配 timer/EXTI 喚醒。

- 問題：SWD 腳 PA13/PA14 與 PB3 保留 debug 功能。
  - 影響：方便開發，但量測最低功耗時可能增加漏電或 debug 相關功耗。
  - 建議修正：正式量測時視需求關閉 debug 或改 BOOT 後 GPIO analog；但會降低除錯便利性。

- 問題：GPIOC/GPIOF/GPIOA/GPIOB clock 沒有在 idle 關閉。
  - 影響：相較核心與週邊功耗可能較小，但仍不是極限低功耗。
  - 建議修正：在 STOP 前可配置未用 GPIO 為 analog no pull，並評估是否關閉不必要 GPIO clock。

### CubeMX 相容性

- 問題：低功耗邏輯在 `main.c` 中部分不在明顯 USER CODE 區塊風格內，且改動集中在 CubeMX 產生檔。
  - 影響：重新 generate 可能覆蓋或打亂手寫流程。
  - 建議修正：把低功耗函式移到獨立 `app_power.c/.h`，`main.c` 只在 USER CODE 區塊呼叫。

- 問題：`.ioc` 與實際低功耗狀態不同。
  - 影響：CubeMX 仍認為 USART2/SPI1/ADC1 是長期啟用 peripheral。
  - 建議修正：在 README 或程式註解中記錄 runtime deinit/reinit 設計。

### Clock configuration 風險

- 系統仍使用 PLL 72 MHz，沒有降頻。
- ADC clock 使用 `RCC_ADC12PLLCLK_DIV1`，ADC12 output 72 MHz。
- 依目前程式判斷可運作，但低功耗與自熱不是最佳。
- 若改低頻，要重新驗證 SPI baud rate、ADC sampling/conversion timeout、SysTick。

### E-paper refresh 中途進 STOP 的風險

- 目前主流程只在檢查到 `g_epd_stop_request` 時進入按鈕流程，不會在 `EPD_ShowString()` 中途直接進 STOP。
- ISR 只設 flag，不在中斷內進 STOP，這是正確的。
- 風險是按鈕反應需等目前 refresh 完成，因 driver 沒有可取消 wait。

---

## 6. 優化版結論

- 功能是否符合要求：
  - 依目前程式判斷，符合讀取 MCU internal temperature sensor、顯示到 e-Paper、每 5 秒更新、按鈕白屏停止。
- 功耗優化是否有效：
  - 有效，但不是極限低功耗。
  - 已關閉 UART/SPI/ADC，使用 WFI 等待，按鈕後進 STOP。
  - 一般週期等待仍被 SysTick 1 ms 喚醒，且系統 clock 仍 72 MHz。
- 還有哪些功耗無法避免：
  - Nucleo 板 ST-LINK、regulator、LED、外接 e-Paper 模組 leakage、接線與 debug 介面。
- 是否建議繼續降頻到 HSI 8 MHz：
  - 建議。對 5 秒更新一次的應用，72 MHz 沒有必要。降到 HSI 8 MHz 可降低 active power 與 MCU 自熱。
  - 需要重新驗證 SPI 更新時間、ADC timeout、e-Paper command timing。
- 是否建議使用 RTC wakeup 或保持目前 WFI：
  - 若目標是展示「有低功耗概念」，目前 WFI 可接受。
  - 若目標是實際降低平均功耗，建議改 RTC wakeup + STOP mode，每 5 秒喚醒一次。
- 建議修改優先順序：
  - 1. 替 EPD BUSY wait、SPI transmit、ADC poll 加 timeout。
  - 2. 移除重複 UART disable，整理 power state。
  - 3. 將 5 秒等待改為 RTC wakeup + STOP mode，醒來後重建 clock。
  - 4. 降頻到 HSI 8 MHz 或較低 PLL。
  - 5. 將低功耗流程移出 CubeMX 產生檔，降低 regenerate 風險。
