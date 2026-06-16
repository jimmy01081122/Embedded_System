/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
  ******************************************************************************
  ** This notice applies to any and all portions of this file
  * that are not between comment pairs USER CODE BEGIN and
  * USER CODE END. Other portions of this file, whether 
  * inserted by the user or by software development tools
  * are owned by their respective copyright owners.
  *
  * COPYRIGHT(c) 2026 STMicroelectronics
  *
  * Redistribution and use in source and binary forms, with or without modification,
  * are permitted provided that the following conditions are met:
  *   1. Redistributions of source code must retain the above copyright notice,
  *      this list of conditions and the following disclaimer.
  *   2. Redistributions in binary form must reproduce the above copyright notice,
  *      this list of conditions and the following disclaimer in the documentation
  *      and/or other materials provided with the distribution.
  *   3. Neither the name of STMicroelectronics nor the names of its contributors
  *      may be used to endorse or promote products derived from this software
  *      without specific prior written permission.
  *
  * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
  * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
  * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
  * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
  * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
  * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
  * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
  * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
  * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
  * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
  *
  ******************************************************************************
  */
/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include "stm32f3xx_hal.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include "epaper_1in02d.h"

ADC_HandleTypeDef hadc1;
SPI_HandleTypeDef hspi1;
UART_HandleTypeDef huart2;

volatile uint8_t g_epd_stop_request = 0;

void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_USART2_UART_Init(void);
static void MX_SPI1_Init(void);
static void MX_ADC1_Init(void);

#ifndef TEMPSENSOR_CAL1_ADDR
#define TEMPSENSOR_CAL1_ADDR ((uint16_t*) ((uint32_t)0x1FFFF7B8))
#endif

#ifndef TEMPSENSOR_CAL2_ADDR
#define TEMPSENSOR_CAL2_ADDR ((uint16_t*) ((uint32_t)0x1FFFF7C2))
#endif

#ifndef VREFINT_CAL_ADDR
#define VREFINT_CAL_ADDR ((uint16_t*) ((uint32_t)0x1FFFF7BA))
#endif

#define TEMP_CAL1_C        30.0f
#define TEMP_CAL2_C        110.0f
#define ADC_AVERAGE_COUNT  4U



#define ENABLE_CLOCK_CHECK  0

#define EXPECT_SYSCLK_HZ    8000000UL
#define EXPECT_HCLK_HZ      8000000UL
#define EXPECT_PCLK1_HZ     8000000UL
#define EXPECT_PCLK2_HZ     8000000UL

/*
 * 若老師要求每 5 秒一定要刷新 E-paper，保持 0。
 * 若允許畫面內容不變時不刷新，可改 1。
 */
#define SKIP_EPD_REFRESH_IF_TEXT_SAME  0

/*
 * 只用來確認 8 MHz clock。
 * 功耗量測時建議保持 0。
 */
#define ENABLE_UART_CLOCK_DEBUG  0

static uint32_t ADC_Read_Channel(uint32_t channel);
static uint32_t ADC_Read_Channel_Average(uint32_t channel, uint32_t count);
static float MCU_Read_Temperature(void);
static float MCU_Read_Temperature_Power(void);
static void Format_Temperature_String(float temp, char *buf, uint32_t size);

static void UART_Disable_For_Power(void);
static void UART_PrintLine(const char *text);
static void UART_Print_Clock_Info(void);
static void SPI_Disable_For_Power(void);
static void ADC_Disable_For_Power(void);
static void LowPower_Delay_Check_Button(uint32_t delay_ms);
static void Enter_Final_Stop(void);


static void Clock_Check_And_Report(void);
static uint8_t Clock_Is_8MHz(void);


static void GPIO_Unused_To_Analog_For_Power(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    /*
     * 保留：
     * PA0 BUSY
     * PA1 RST
     * PA4 DC
     * PA5 SCK
     * PA6 CS
     * PA7 MOSI
     * PA13 SWDIO
     * PA14 SWCLK
     *
     * PA2/PA3 已在 UART_Disable_For_Power() 設為 analog。
     * 所以這裡只處理 PA8~PA12, PA15 等未用腳。
     */
    GPIO_InitStruct.Pin =
        GPIO_PIN_8  |
        GPIO_PIN_9  |
        GPIO_PIN_10 |
        GPIO_PIN_11 |
        GPIO_PIN_12 |
        GPIO_PIN_15;

    GPIO_InitStruct.Mode = GPIO_MODE_ANALOG;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

    /*
     * GPIOB 若未使用，可全設 analog。
     * 若你的板上或專案有用 PB 腳，請排除。
     */
    GPIO_InitStruct.Pin =
        GPIO_PIN_0  |
        GPIO_PIN_1  |
        GPIO_PIN_2  |
        GPIO_PIN_3  |
        GPIO_PIN_4  |
        GPIO_PIN_5  |
        GPIO_PIN_6  |
        GPIO_PIN_7  |
        GPIO_PIN_8  |
        GPIO_PIN_9  |
        GPIO_PIN_10 |
        GPIO_PIN_11 |
        GPIO_PIN_12 |
        GPIO_PIN_13 |
        GPIO_PIN_14 |
        GPIO_PIN_15;

    GPIO_InitStruct.Mode = GPIO_MODE_ANALOG;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

    /*
     * GPIOC 保留 PC13 Button。
     * 其他 PC 腳若未使用可設 analog。
     */
    GPIO_InitStruct.Pin =
        GPIO_PIN_0  |
        GPIO_PIN_1  |
        GPIO_PIN_2  |
        GPIO_PIN_3  |
        GPIO_PIN_4  |
        GPIO_PIN_5  |
        GPIO_PIN_6  |
        GPIO_PIN_7  |
        GPIO_PIN_8  |
        GPIO_PIN_9  |
        GPIO_PIN_10 |
        GPIO_PIN_11 |
        GPIO_PIN_12 |
        GPIO_PIN_14 |
        GPIO_PIN_15;

    GPIO_InitStruct.Mode = GPIO_MODE_ANALOG;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);
}

static uint8_t Button_IsPressed(void);
static void Request_Stop_If_Button_Pressed(void);

void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin)
{
    if (GPIO_Pin == GPIO_PIN_13)
    {
        g_epd_stop_request = 1;
    }
}

static uint8_t Button_IsPressed(void)
{
    /*
     * Nucleo B1 / USER button 一般為 PC13，按下為 Low。
     */
#ifdef B1_Pin
    if (HAL_GPIO_ReadPin(B1_GPIO_Port, B1_Pin) == GPIO_PIN_RESET)
#else
    if (HAL_GPIO_ReadPin(GPIOC, GPIO_PIN_13) == GPIO_PIN_RESET)
#endif
    {
        return 1U;
    }

    return 0U;
}

static void Request_Stop_If_Button_Pressed(void)
{
    if (Button_IsPressed())
    {
        g_epd_stop_request = 1;
    }
}

static void UART_Disable_For_Power(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    HAL_UART_DeInit(&huart2);
    __HAL_RCC_USART2_CLK_DISABLE();

    /*
     * PA2 USART2_TX, PA3 USART2_RX -> Analog No Pull
     */
    GPIO_InitStruct.Pin = GPIO_PIN_2 | GPIO_PIN_3;
    GPIO_InitStruct.Mode = GPIO_MODE_ANALOG;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);
}


static void UART_PrintLine(const char *text)
{
#if ENABLE_CLOCK_CHECK
    HAL_UART_Transmit(&huart2, (uint8_t *)text, strlen(text), HAL_MAX_DELAY);
    HAL_UART_Transmit(&huart2, (uint8_t *)"\r\n", 2, HAL_MAX_DELAY);
#else
    (void)text;
#endif
}
static uint8_t Clock_Is_8MHz(void)
{
    uint32_t sysclk;
    uint32_t hclk;
    uint32_t pclk1;
    uint32_t pclk2;

    sysclk = HAL_RCC_GetSysClockFreq();
    hclk   = HAL_RCC_GetHCLKFreq();
    pclk1  = HAL_RCC_GetPCLK1Freq();
    pclk2  = HAL_RCC_GetPCLK2Freq();

    if ((sysclk == EXPECT_SYSCLK_HZ) &&
        (hclk   == EXPECT_HCLK_HZ) &&
        (pclk1  == EXPECT_PCLK1_HZ) &&
        (pclk2  == EXPECT_PCLK2_HZ))
    {
        return 1U;
    }

    return 0U;
}

static void Clock_Check_And_Report(void)
{
#if ENABLE_CLOCK_CHECK
    char line[64];

    UART_PrintLine("Clock check start");

    snprintf(line, sizeof(line),
             "SYSCLK: %lu Hz",
             (unsigned long)HAL_RCC_GetSysClockFreq());
    UART_PrintLine(line);

    snprintf(line, sizeof(line),
             "HCLK:   %lu Hz",
             (unsigned long)HAL_RCC_GetHCLKFreq());
    UART_PrintLine(line);

    snprintf(line, sizeof(line),
             "PCLK1:  %lu Hz",
             (unsigned long)HAL_RCC_GetPCLK1Freq());
    UART_PrintLine(line);

    snprintf(line, sizeof(line),
             "PCLK2:  %lu Hz",
             (unsigned long)HAL_RCC_GetPCLK2Freq());
    UART_PrintLine(line);

    if (Clock_Is_8MHz())
    {
        UART_PrintLine("Clock result: PASS, 8 MHz confirmed");
    }
    else
    {
        UART_PrintLine("Clock result: FAIL, not 8 MHz");
    }

    UART_PrintLine("Clock check end");
#endif
}

static void UART_Print_Clock_Info(void)
{
#if ENABLE_UART_CLOCK_DEBUG
    char line[48];

    UART_PrintLine("Clock check");

    snprintf(line, sizeof(line), "SYSCLK: %lu Hz", (unsigned long)HAL_RCC_GetSysClockFreq());
    UART_PrintLine(line);

    snprintf(line, sizeof(line), "HCLK:   %lu Hz", (unsigned long)HAL_RCC_GetHCLKFreq());
    UART_PrintLine(line);

    snprintf(line, sizeof(line), "PCLK1:  %lu Hz", (unsigned long)HAL_RCC_GetPCLK1Freq());
    UART_PrintLine(line);

    snprintf(line, sizeof(line), "PCLK2:  %lu Hz", (unsigned long)HAL_RCC_GetPCLK2Freq());
    UART_PrintLine(line);
#endif
}

static void SPI_Disable_For_Power(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    HAL_SPI_DeInit(&hspi1);
    __HAL_RCC_SPI1_CLK_DISABLE();

    /*
     * Keep E-paper control pins stable.
     */
    HAL_GPIO_WritePin(EPD_CS_GPIO_Port, EPD_CS_Pin, GPIO_PIN_SET);
    HAL_GPIO_WritePin(EPD_DC_GPIO_Port, EPD_DC_Pin, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(EPD_RST_GPIO_Port, EPD_RST_Pin, GPIO_PIN_SET);

    /*
     * PA5 SCK, PA7 MOSI -> output low.
     * Avoid floating E-paper input pins.
     */
    HAL_GPIO_WritePin(GPIOA, GPIO_PIN_5 | GPIO_PIN_7, GPIO_PIN_RESET);

    GPIO_InitStruct.Pin = GPIO_PIN_5 | GPIO_PIN_7;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);
}

static void ADC_Disable_For_Power(void)
{
    HAL_ADC_DeInit(&hadc1);
    __HAL_RCC_ADC12_CLK_DISABLE();
}

static uint32_t ADC_Read_Channel(uint32_t channel)
{
    ADC_ChannelConfTypeDef sConfig = {0};
    uint32_t value;

    sConfig.Channel = channel;
    sConfig.Rank = ADC_REGULAR_RANK_1;
    sConfig.SingleDiff = ADC_SINGLE_ENDED;
    sConfig.SamplingTime = ADC_SAMPLETIME_601CYCLES_5;
    sConfig.OffsetNumber = ADC_OFFSET_NONE;
    sConfig.Offset = 0;

    if (HAL_ADC_ConfigChannel(&hadc1, &sConfig) != HAL_OK)
    {
        _Error_Handler(__FILE__, __LINE__);
    }

    if (HAL_ADC_Start(&hadc1) != HAL_OK)
    {
        _Error_Handler(__FILE__, __LINE__);
    }

    if (HAL_ADC_PollForConversion(&hadc1, 10) != HAL_OK)
    {
        _Error_Handler(__FILE__, __LINE__);
    }

    value = HAL_ADC_GetValue(&hadc1);

    HAL_ADC_Stop(&hadc1);

    return value;
}

static uint32_t ADC_Read_Channel_Average(uint32_t channel, uint32_t count)
{
    uint32_t i;
    uint64_t sum = 0;

    for (i = 0; i < count; i++)
    {
        sum += ADC_Read_Channel(channel);
        HAL_Delay(1);
    }

    return (uint32_t)(sum / count);
}

static float MCU_Read_Temperature(void)
{
    uint32_t ts_raw;
    uint32_t vref_raw;

    uint16_t ts_cal1 = *TEMPSENSOR_CAL1_ADDR;
    uint16_t ts_cal2 = *TEMPSENSOR_CAL2_ADDR;
    uint16_t vref_cal = *VREFINT_CAL_ADDR;

    float ts_corrected;
    float temperature;

    ts_raw = ADC_Read_Channel_Average(ADC_CHANNEL_TEMPSENSOR, ADC_AVERAGE_COUNT);
    vref_raw = ADC_Read_Channel_Average(ADC_CHANNEL_VREFINT, ADC_AVERAGE_COUNT);

    if ((vref_raw == 0U) || (ts_cal2 == ts_cal1))
    {
        return -999.0f;
    }

    ts_corrected = ((float)ts_raw * (float)vref_cal) / (float)vref_raw;

    temperature =
        ((ts_corrected - (float)ts_cal1) * (TEMP_CAL2_C - TEMP_CAL1_C)) /
        ((float)ts_cal2 - (float)ts_cal1) +
        TEMP_CAL1_C;

    return temperature;
}

static float MCU_Read_Temperature_Power(void)
{
    float temp;

    MX_ADC1_Init();

    if (HAL_ADCEx_Calibration_Start(&hadc1, ADC_SINGLE_ENDED) != HAL_OK)
    {
        _Error_Handler(__FILE__, __LINE__);
    }

    temp = MCU_Read_Temperature();

    ADC_Disable_For_Power();

    return temp;
}

static void Format_Temperature_String(float temp, char *buf, uint32_t size)
{
    int32_t temp_x100;
    int32_t integer_part;
    int32_t decimal_part;

    if (temp >= 0.0f)
    {
        temp_x100 = (int32_t)(temp * 100.0f + 0.5f);
    }
    else
    {
        temp_x100 = (int32_t)(temp * 100.0f - 0.5f);
    }

    integer_part = temp_x100 / 100;
    decimal_part = labs(temp_x100 % 100);

    snprintf(buf, size, "MCU Temp: %ld.%02ld C", integer_part, decimal_part);
}

static void LowPower_Delay_Check_Button(uint32_t delay_ms)
{
    uint32_t start_tick;

    start_tick = HAL_GetTick();

    while ((HAL_GetTick() - start_tick) < delay_ms)
    {
        Request_Stop_If_Button_Pressed();

        if (g_epd_stop_request)
        {
            return;
        }

        __WFI();
    }
}

static void Enter_Final_Stop(void)
{
    ADC_Disable_For_Power();
    SPI_Disable_For_Power();
    UART_Disable_For_Power();

    GPIO_Unused_To_Analog_For_Power();

    HAL_SuspendTick();

    while (1)
    {
        HAL_PWR_EnterSTOPMode(PWR_LOWPOWERREGULATOR_ON, PWR_STOPENTRY_WFI);
    }
}

int main(void)
{
    static char last_text[32] = {0};
    char text[32];
    float temp;

    HAL_Init();

    SystemClock_Config();

    MX_GPIO_Init();
    MX_USART2_UART_Init();
    MX_SPI1_Init();
    MX_ADC1_Init();

    __HAL_RCC_PWR_CLK_ENABLE();

    GPIO_Unused_To_Analog_For_Power();

//    UART_Print_Clock_Info();
    Clock_Check_And_Report();
    UART_Disable_For_Power();
    SPI_Disable_For_Power();
    ADC_Disable_For_Power();

    while (1)
    {
        Request_Stop_If_Button_Pressed();

        if (g_epd_stop_request)
        {
            MX_SPI1_Init();

            EPD_Init();
            EPD_ClearWhite();
            EPD_Sleep();

            SPI_Disable_For_Power();

            Enter_Final_Stop();
        }

        temp = MCU_Read_Temperature_Power();
        Format_Temperature_String(temp, text, sizeof(text));

#if SKIP_EPD_REFRESH_IF_TEXT_SAME
        if (strcmp(text, last_text) != 0)
        {
            MX_SPI1_Init();

            EPD_Init();
            EPD_ShowString(text);
            EPD_Sleep();

            SPI_Disable_For_Power();

            strncpy(last_text, text, sizeof(last_text));
            last_text[sizeof(last_text) - 1] = '\0';
        }
#else
        MX_SPI1_Init();

        EPD_Init();
        EPD_ShowString(text);
        EPD_Sleep();

        SPI_Disable_For_Power();

        strncpy(last_text, text, sizeof(last_text));
        last_text[sizeof(last_text) - 1] = '\0';
#endif

        Request_Stop_If_Button_Pressed();

        if (g_epd_stop_request)
        {
            MX_SPI1_Init();

            EPD_Init();
            EPD_ClearWhite();
            EPD_Sleep();

            SPI_Disable_For_Power();

            Enter_Final_Stop();
        }

        LowPower_Delay_Check_Button(5000);
    }
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
    RCC_OscInitTypeDef RCC_OscInitStruct;
    RCC_ClkInitTypeDef RCC_ClkInitStruct;
    RCC_PeriphCLKInitTypeDef PeriphClkInit;

    RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI;
    RCC_OscInitStruct.HSIState = RCC_HSI_ON;
    RCC_OscInitStruct.HSICalibrationValue = 16;
    RCC_OscInitStruct.PLL.PLLState = RCC_PLL_OFF;

    if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
    {
        _Error_Handler(__FILE__, __LINE__);
    }

    RCC_ClkInitStruct.ClockType =
        RCC_CLOCKTYPE_HCLK |
        RCC_CLOCKTYPE_SYSCLK |
        RCC_CLOCKTYPE_PCLK1 |
        RCC_CLOCKTYPE_PCLK2;

    RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_HSI;
    RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
    RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV1;
    RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

    if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_0) != HAL_OK)
    {
        _Error_Handler(__FILE__, __LINE__);
    }

    /*
     * UART clock retained for optional debug only.
     * ADC uses synchronous PCLK clock in MX_ADC1_Init().
     */
    PeriphClkInit.PeriphClockSelection = RCC_PERIPHCLK_USART2;
    PeriphClkInit.Usart2ClockSelection = RCC_USART2CLKSOURCE_PCLK1;

    if (HAL_RCCEx_PeriphCLKConfig(&PeriphClkInit) != HAL_OK)
    {
        _Error_Handler(__FILE__, __LINE__);
    }

    HAL_SYSTICK_Config(HAL_RCC_GetHCLKFreq() / 1000);
    HAL_SYSTICK_CLKSourceConfig(SYSTICK_CLKSOURCE_HCLK);

    HAL_NVIC_SetPriority(SysTick_IRQn, 0, 0);
}

/* ADC1 init function */
static void MX_ADC1_Init(void)
{
    ADC_MultiModeTypeDef multimode;
    ADC_ChannelConfTypeDef sConfig;

    hadc1.Instance = ADC1;
    hadc1.Init.ClockPrescaler = ADC_CLOCK_SYNC_PCLK_DIV1;
    hadc1.Init.Resolution = ADC_RESOLUTION_12B;
    hadc1.Init.ScanConvMode = ADC_SCAN_DISABLE;
    hadc1.Init.ContinuousConvMode = DISABLE;
    hadc1.Init.DiscontinuousConvMode = DISABLE;
    hadc1.Init.ExternalTrigConvEdge = ADC_EXTERNALTRIGCONVEDGE_NONE;
    hadc1.Init.ExternalTrigConv = ADC_SOFTWARE_START;
    hadc1.Init.DataAlign = ADC_DATAALIGN_RIGHT;
    hadc1.Init.NbrOfConversion = 1;
    hadc1.Init.DMAContinuousRequests = DISABLE;
    hadc1.Init.EOCSelection = ADC_EOC_SINGLE_CONV;
    hadc1.Init.LowPowerAutoWait = DISABLE;
    hadc1.Init.Overrun = ADC_OVR_DATA_OVERWRITTEN;

    if (HAL_ADC_Init(&hadc1) != HAL_OK)
    {
        _Error_Handler(__FILE__, __LINE__);
    }

    multimode.Mode = ADC_MODE_INDEPENDENT;

    if (HAL_ADCEx_MultiModeConfigChannel(&hadc1, &multimode) != HAL_OK)
    {
        _Error_Handler(__FILE__, __LINE__);
    }

    sConfig.Channel = ADC_CHANNEL_TEMPSENSOR;
    sConfig.Rank = ADC_REGULAR_RANK_1;
    sConfig.SingleDiff = ADC_SINGLE_ENDED;
    sConfig.SamplingTime = ADC_SAMPLETIME_601CYCLES_5;
    sConfig.OffsetNumber = ADC_OFFSET_NONE;
    sConfig.Offset = 0;

    if (HAL_ADC_ConfigChannel(&hadc1, &sConfig) != HAL_OK)
    {
        _Error_Handler(__FILE__, __LINE__);
    }
}

/* SPI1 init function */
static void MX_SPI1_Init(void)
{
    hspi1.Instance = SPI1;
    hspi1.Init.Mode = SPI_MODE_MASTER;
    hspi1.Init.Direction = SPI_DIRECTION_2LINES;
    hspi1.Init.DataSize = SPI_DATASIZE_8BIT;
    hspi1.Init.CLKPolarity = SPI_POLARITY_LOW;
    hspi1.Init.CLKPhase = SPI_PHASE_1EDGE;
    hspi1.Init.NSS = SPI_NSS_SOFT;
    hspi1.Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_32;
    hspi1.Init.FirstBit = SPI_FIRSTBIT_MSB;
    hspi1.Init.TIMode = SPI_TIMODE_DISABLE;
    hspi1.Init.CRCCalculation = SPI_CRCCALCULATION_DISABLE;
    hspi1.Init.CRCPolynomial = 7;
    hspi1.Init.CRCLength = SPI_CRC_LENGTH_DATASIZE;
    hspi1.Init.NSSPMode = SPI_NSS_PULSE_ENABLE;

    if (HAL_SPI_Init(&hspi1) != HAL_OK)
    {
        _Error_Handler(__FILE__, __LINE__);
    }
}

/* USART2 init function */
static void MX_USART2_UART_Init(void)
{
    huart2.Instance = USART2;
    huart2.Init.BaudRate = 115200;
    huart2.Init.WordLength = UART_WORDLENGTH_8B;
    huart2.Init.StopBits = UART_STOPBITS_1;
    huart2.Init.Parity = UART_PARITY_NONE;
    huart2.Init.Mode = UART_MODE_TX_RX;
    huart2.Init.HwFlowCtl = UART_HWCONTROL_NONE;
    huart2.Init.OverSampling = UART_OVERSAMPLING_16;
    huart2.Init.OneBitSampling = UART_ONE_BIT_SAMPLE_DISABLE;
    huart2.AdvancedInit.AdvFeatureInit = UART_ADVFEATURE_NO_INIT;

    if (HAL_UART_Init(&huart2) != HAL_OK)
    {
        _Error_Handler(__FILE__, __LINE__);
    }
}

static void MX_GPIO_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStruct;

    __HAL_RCC_GPIOC_CLK_ENABLE();
    __HAL_RCC_GPIOF_CLK_ENABLE();
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();

    HAL_GPIO_WritePin(GPIOA, GPIO_PIN_1 | GPIO_PIN_6, GPIO_PIN_SET);
    HAL_GPIO_WritePin(GPIOA, GPIO_PIN_4, GPIO_PIN_RESET);

    /*
     * B1 / PC13
     */
    GPIO_InitStruct.Pin = B1_Pin;
    GPIO_InitStruct.Mode = GPIO_MODE_IT_FALLING;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(B1_GPIO_Port, &GPIO_InitStruct);

    /*
     * PA0 EPD_BUSY
     */
    GPIO_InitStruct.Pin = GPIO_PIN_0;
    GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

    /*
     * PA1 RST, PA4 DC, PA6 CS
     */
    GPIO_InitStruct.Pin = GPIO_PIN_1 | GPIO_PIN_4 | GPIO_PIN_6;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

    __HAL_GPIO_EXTI_CLEAR_IT(GPIO_PIN_13);

    HAL_NVIC_SetPriority(EXTI15_10_IRQn, 1, 0);
    HAL_NVIC_EnableIRQ(EXTI15_10_IRQn);
}

void _Error_Handler(char *file, int line)
{
    (void)file;
    (void)line;

    while (1)
    {
    }
}

#ifdef USE_FULL_ASSERT
void assert_failed(uint8_t* file, uint32_t line)
{
    (void)file;
    (void)line;
}
#endif
