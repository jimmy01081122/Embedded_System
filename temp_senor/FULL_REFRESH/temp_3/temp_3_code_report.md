# 程式碼檢查報告



## 目錄

- [1. 專案概覽]
- [2. 8 MHz 降頻]
- [3. 溫度偵測方法]
- [4. E-paper 顯示與 BUSY timeout]
- [5. 低功耗設計]
- [6. 實機測試建議]


## 1. 專案概覽

### 主要檔案與用途

- `Src/main.c`
  - 主流程、8 MHz clock 設定、ADC 溫度讀取、UART clock check、低功耗 peripheral disable、button stop 流程。
- `Src/epaper_1in02d.c`
  - 1.02 inch E-paper 初始化、SPI command/data 傳送、frame buffer 繪圖、字串顯示、白屏、sleep。
- `Inc/epaper_1in02d.h`
  - E-paper 尺寸、frame buffer 大小、BUSY/RST/DC/CS 腳位定義。
- `Src/stm32f3xx_hal_msp.c`
  - ADC1、SPI1、USART2 的 HAL MSP init/deinit。
- `Src/stm32f3xx_it.c`
  - SysTick handler 與 PC13 EXTI handler。
- `temp.ioc`
  - CubeMX 設定檔。依目前程式判斷，部分設定與 `Src/main.c` 已不完全一致。
- `Debug/temp.elf`、`Debug/temp.list`、`Debug/temp.map`
  - 既有 build 產物。因本次已修改 source，這些檔案需要重新編譯後才代表最新程式。

### 主流程

1. `HAL_Init()`。
2. `SystemClock_Config()` 將系統 clock 設為 HSI 8 MHz。
3. 初始化 GPIO、USART2、SPI1、ADC1。
4. 設定未使用 GPIO 為 analog no pull。
5. 若 `ENABLE_CLOCK_CHECK == 1`，透過 UART 輸出 clock 檢查結果。
6. 關閉 UART、SPI、ADC 以降低 idle power。
7. 進入主迴圈：
   - 檢查 PC13 button。
   - 讀取 MCU internal temperature sensor。
   - 格式化為 `MCU Temp: xx.xx C`。
   - 初始化 SPI 與 E-paper。
   - 顯示溫度。
   - E-paper sleep。
   - 關閉 SPI。
   - 使用 `__WFI()` 等待 5 秒，期間輪詢 button。
8. 若偵測到 button：
   - 初始化 SPI。
   - `EPD_Init()`。
   - `EPD_ClearWhite()`。
   - `EPD_Sleep()`。
   - 關閉 SPI。
   - 進入永久 STOP mode。

## 2. 8 MHz 降頻

- `SystemClock_Config()` 使用 HSI：
  - `RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI`
  - `RCC_OscInitStruct.HSIState = RCC_HSI_ON`
  - `RCC_OscInitStruct.PLL.PLLState = RCC_PLL_OFF`
- SYSCLK 來源為 HSI：
  - `RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_HSI`
- bus divider：
  - AHB = DIV1
  - APB1 = DIV1
  - APB2 = DIV1
- Flash latency：
  - `FLASH_LATENCY_0`
- 期望值：
  - `EXPECT_SYSCLK_HZ = 8000000`
  - `EXPECT_HCLK_HZ = 8000000`
  - `EXPECT_PCLK1_HZ = 8000000`
  - `EXPECT_PCLK2_HZ = 8000000`

## 3. 溫度偵測方法

- 使用 ADC1。
- 使用 STM32 internal temperature sensor：
  - `ADC_CHANNEL_TEMPSENSOR`
- 使用 VREFINT：
  - `ADC_CHANNEL_VREFINT`
- 使用校正值：
  - `TEMPSENSOR_CAL1_ADDR`
  - `TEMPSENSOR_CAL2_ADDR`
  - `VREFINT_CAL_ADDR`
- 溫度計算方式：
  - 先用 VREFINT 補償 raw temperature ADC value。
  - 再用 TS_CAL1 與 TS_CAL2 做內插。
- ADC sample time：
  - `ADC_SAMPLETIME_601CYCLES_5`
- 平均濾波：
  - `ADC_AVERAGE_COUNT = 4`
- 顯示格式：
  - `MCU Temp: xx.xx C`

### Note

- internal temperature sensor 量到的是 MCU die temperature，不是環境溫度。

- ADC 平均次數 = 4 。
  - 影響：功耗低、速度快，但讀值可能有抖動。


## 4. E-paper 顯示與 BUSY timeout

### 已確認內容

- 邏輯顯示尺寸：
  - `EPD_LOG_WIDTH = 128`
  - `EPD_LOG_HEIGHT = 80`
- controller RAM：
  - `EPD_RAM_WIDTH = 80`
  - `EPD_RAM_HEIGHT = 128`
- frame buffer：
  - `80 * 128 / 8 = 1280 bytes`
- 腳位：
  - BUSY = PA0
  - RST = PA1
  - DC = PA4
  - CS = PA6
  - SCK = PA5
  - MOSI = PA7
- SPI transmit timeout：
  - `EPD_SPI_TIMEOUT_MS = 100`
- BUSY timeout：
  - `EPD_BUSY_TIMEOUT_MS = 10000`
- BUSY 邏輯：
  - Low = busy
  - High = idle

`EPD_WaitUntilIdle()` 已由無限等待改成 timeout 等待：
```c
static void EPD_WaitUntilIdle(void)
{
    uint32_t start_tick;

    start_tick = HAL_GetTick();

    while (HAL_GPIO_ReadPin(EPD_BUSY_GPIO_Port, EPD_BUSY_Pin) == GPIO_PIN_RESET)
    {
        if ((HAL_GetTick() - start_tick) >= EPD_BUSY_TIMEOUT_MS)
        {
            _Error_Handler(__FILE__, __LINE__);
        }

        HAL_Delay(10);
    }
}
```

### Note

- BUSY timeout 後直接進 `_Error_Handler()`，為降低功號，不顯示原因。
- `EPD_SPI_TIMEOUT_MS = 100` 目前 SPI prescaler 是 16。
- `EPD_ROTATE_MODE = 1` 是目前手動指定方向。

## 5. 低功耗設計

### 已確認內容

- UART：
  - 初始化後用於 clock check。
  - 接著 `HAL_UART_DeInit()`。
  - 關閉 USART2 clock。
  - PA2/PA3 設為 analog no pull。
- SPI：
  - E-paper 更新前重新 init。
  - 更新後 `HAL_SPI_DeInit()`。
  - 關閉 SPI1 clock。
  - PA5/PA7 設成 output low，避免 E-paper input 浮接。
- ADC：
  - 讀溫度前重新 init。
  - 每次讀取前做 ADC calibration。
  - 讀完後 `HAL_ADC_DeInit()`。
  - 關閉 ADC12 clock。
- GPIO：
  - 未使用腳位設為 analog no pull。
  - 保留 PA0/PA1/PA4/PA5/PA6/PA7/PA13/PA14 與 PC13。
- 等待：
  - 5 秒等待使用 `__WFI()`。
- 停止：
  - button 觸發後白屏、E-paper sleep、關閉 peripheral，進入 STOP mode。



## 6. 實機測試建議

### 降頻確認

1. 保持 `ENABLE_CLOCK_CHECK = 1`。
2. 燒錄後查看 UART。
3. 預期輸出：

```text
Clock check start
SYSCLK: 8000000 Hz
HCLK:   8000000 Hz
PCLK1:  8000000 Hz
PCLK2:  8000000 Hz
Clock result: PASS, 8 MHz confirmed
Clock check end
```

4. 確認後將 `ENABLE_CLOCK_CHECK` 改回 `0` 再量測功耗。

### E-paper 測試

- 確認每 5 秒更新一次。
- 確認畫面顯示 `MCU Temp: xx.xx C`。
- 拔掉或故意接錯 BUSY 腳時，程式應在約 10 秒後進 `_Error_Handler()`，不再永久卡在 BUSY wait。
- 若遇到 E-paper 更新失敗，優先檢查：
  - BUSY = PA0
  - RST = PA1
  - DC = PA4
  - CS = PA6
  - SCK = PA5
  - MOSI = PA7

### Button 測試

- 按下 Nucleo blue button 後，預期流程：
  - 設定 stop request。
  - 重新 init SPI。
  - E-paper init。
  - 清白屏。
  - E-paper sleep。
  - 關閉 SPI。
  - 進入永久 STOP mode。

### 功耗測試

- 第一次測試：`ENABLE_CLOCK_CHECK = 1`，確認 clock。
- 第二次測試：`ENABLE_CLOCK_CHECK = 0`，量測實際低功耗。
- 若量測整塊 Nucleo：
  - ST-LINK、power LED、LDO、外接 E-paper 模組都會影響總功耗。
  - 程式只能降低 MCU 與 peripheral 的耗電，不能消除整塊板子的固定耗電。

