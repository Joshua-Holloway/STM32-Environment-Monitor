/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : EnviroNode main program
  ******************************************************************************
  */
/* USER CODE END Header */

#include "main.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "ssd1306.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */
typedef enum
{
    DISPLAY_BOTH = 0,
    DISPLAY_TEMP,
    DISPLAY_HUMIDITY
} DisplayMode;
/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
#define DHT_READ_INTERVAL_MS 2200U
#define BUTTON_DEBOUNCE_MS   200U
/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */
/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
I2C_HandleTypeDef hi2c1;
UART_HandleTypeDef huart2;

/* USER CODE BEGIN PV */
static volatile DisplayMode display_mode = DISPLAY_BOTH;
static volatile uint8_t display_dirty = 1U;
static volatile uint32_t last_button_tick = 0U;
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_USART2_UART_Init(void);
static void MX_I2C1_Init(void);
/* USER CODE BEGIN PFP */
/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */
static void DWT_Delay_Init(void)
{
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CYCCNT = 0U;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
}

static void delay_us(uint32_t us)
{
    uint32_t start = DWT->CYCCNT;
    uint32_t ticks = us * (HAL_RCC_GetHCLKFreq() / 1000000U);

    while ((DWT->CYCCNT - start) < ticks)
    {
    }
}

static void DHT_SetOutput(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    GPIO_InitStruct.Pin = DHT_DATA_Pin;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;

    HAL_GPIO_Init(DHT_DATA_GPIO_Port, &GPIO_InitStruct);
}

static void DHT_SetInput(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    GPIO_InitStruct.Pin = DHT_DATA_Pin;
    GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
    GPIO_InitStruct.Pull = GPIO_NOPULL;

    HAL_GPIO_Init(DHT_DATA_GPIO_Port, &GPIO_InitStruct);
}

static uint8_t DHT_WaitForState(GPIO_PinState state, uint32_t timeout_us)
{
    uint32_t start = DWT->CYCCNT;
    uint32_t timeout_ticks = timeout_us * (HAL_RCC_GetHCLKFreq() / 1000000U);

    while (HAL_GPIO_ReadPin(DHT_DATA_GPIO_Port, DHT_DATA_Pin) != state)
    {
        if ((DWT->CYCCNT - start) > timeout_ticks)
        {
            return 0U;
        }
    }

    return 1U;
}

static uint8_t DHT22_Read(int16_t *temperature, uint16_t *humidity)
{
    uint8_t data[5] = {0};

    DHT_SetOutput();

    HAL_GPIO_WritePin(DHT_DATA_GPIO_Port, DHT_DATA_Pin, GPIO_PIN_RESET);
    HAL_Delay(2U);

    HAL_GPIO_WritePin(DHT_DATA_GPIO_Port, DHT_DATA_Pin, GPIO_PIN_SET);
    delay_us(30U);

    DHT_SetInput();

    /* DHT22 acknowledgement: LOW -> HIGH -> LOW. */
    if (!DHT_WaitForState(GPIO_PIN_RESET, 100U))
    {
        return 0U;
    }

    if (!DHT_WaitForState(GPIO_PIN_SET, 100U))
    {
        return 0U;
    }

    if (!DHT_WaitForState(GPIO_PIN_RESET, 100U))
    {
        return 0U;
    }

    /* Read 40 data bits. */
    for (uint8_t i = 0U; i < 40U; i++)
    {
        if (!DHT_WaitForState(GPIO_PIN_SET, 100U))
        {
            return 0U;
        }

        /* A 0 has a short HIGH pulse; a 1 has a long HIGH pulse. */
        delay_us(40U);

        if (HAL_GPIO_ReadPin(DHT_DATA_GPIO_Port, DHT_DATA_Pin) == GPIO_PIN_SET)
        {
            data[i / 8U] |= (uint8_t)(1U << (7U - (i % 8U)));
        }

        if (!DHT_WaitForState(GPIO_PIN_RESET, 100U))
        {
            return 0U;
        }
    }

    /* Fifth byte is the checksum of the first four bytes. */
    if ((uint8_t)(data[0] + data[1] + data[2] + data[3]) != data[4])
    {
        return 0U;
    }

    *humidity = ((uint16_t)data[0] << 8) | data[1];

    uint16_t raw_temperature =
        ((uint16_t)(data[2] & 0x7FU) << 8) | data[3];

    if ((data[2] & 0x80U) != 0U)
    {
        *temperature = -(int16_t)raw_temperature;
    }
    else
    {
        *temperature = (int16_t)raw_temperature;
    }

    return 1U;
}

static void UART_Write(const char *text)
{
    HAL_UART_Transmit(
        &huart2,
        (uint8_t *)text,
        (uint16_t)strlen(text),
        HAL_MAX_DELAY
    );
}

static void UART_PrintReading(int16_t temperature, uint16_t humidity)
{
    char message[100];
    int32_t t = temperature;

    if (t < 0)
    {
        t = -t;
        snprintf(
            message,
            sizeof(message),
            "Temperature: -%ld.%ld C | Humidity: %u.%u %%\r\n",
            (long)(t / 10),
            (long)(t % 10),
            humidity / 10U,
            humidity % 10U
        );
    }
    else
    {
        snprintf(
            message,
            sizeof(message),
            "Temperature: %ld.%ld C | Humidity: %u.%u %%\r\n",
            (long)(t / 10),
            (long)(t % 10),
            humidity / 10U,
            humidity % 10U
        );
    }

    UART_Write(message);
}

static void FormatTemperature(char *buffer, size_t buffer_size, int16_t temperature)
{
    int32_t t = temperature;

    if (t < 0)
    {
        t = -t;
        snprintf(buffer, buffer_size, "-%ld.%ld C", (long)(t / 10), (long)(t % 10));
    }
    else
    {
        snprintf(buffer, buffer_size, "%ld.%ld C", (long)(t / 10), (long)(t % 10));
    }
}

static void FormatHumidity(char *buffer, size_t buffer_size, uint16_t humidity)
{
    snprintf(buffer, buffer_size, "%u.%u %%", humidity / 10U, humidity % 10U);
}

static uint8_t CenteredX(const char *text, uint8_t scale)
{
    uint16_t width = (uint16_t)strlen(text) * 6U * scale;

    if (width >= SSD1306_WIDTH)
    {
        return 0U;
    }

    return (uint8_t)((SSD1306_WIDTH - width) / 2U);
}

static void Display_Render(int16_t temperature, uint16_t humidity, uint8_t have_reading)
{
    char temperature_text[20];
    char humidity_text[20];
    DisplayMode mode = display_mode;

    SSD1306_Clear();

    if (!have_reading)
    {
        SSD1306_WriteString(22U, 17U, "DHT22", 2U);
        SSD1306_WriteString(31U, 40U, "NO DATA", 1U);
        (void)SSD1306_UpdateScreen();
        return;
    }

    FormatTemperature(temperature_text, sizeof(temperature_text), temperature);
    FormatHumidity(humidity_text, sizeof(humidity_text), humidity);

    if (mode == DISPLAY_TEMP)
    {
        SSD1306_WriteString(CenteredX("TEMPERATURE", 1U), 7U, "TEMPERATURE", 1U);
        SSD1306_WriteString(CenteredX(temperature_text, 3U), 27U, temperature_text, 3U);
    }
    else if (mode == DISPLAY_HUMIDITY)
    {
        SSD1306_WriteString(CenteredX("HUMIDITY", 1U), 7U, "HUMIDITY", 1U);
        SSD1306_WriteString(CenteredX(humidity_text, 3U), 27U, humidity_text, 3U);
    }
    else
    {
        SSD1306_WriteString(0U, 0U, "TEMP", 1U);
        SSD1306_WriteString(0U, 10U, temperature_text, 2U);

        SSD1306_WriteString(0U, 34U, "HUMIDITY", 1U);
        SSD1306_WriteString(0U, 44U, humidity_text, 2U);
    }

    (void)SSD1306_UpdateScreen();
}
/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{
    /* USER CODE BEGIN 1 */
    int16_t temperature = 0;
    uint16_t humidity = 0U;
    uint8_t have_reading = 0U;
    uint8_t oled_ok = 0U;
    uint32_t last_sensor_read = 0U;
    /* USER CODE END 1 */

    HAL_Init();

    /* USER CODE BEGIN Init */
    /* USER CODE END Init */

    SystemClock_Config();

    /* USER CODE BEGIN SysInit */
    /* USER CODE END SysInit */

    MX_GPIO_Init();
    MX_USART2_UART_Init();
    MX_I2C1_Init();

    /* USER CODE BEGIN 2 */
    DWT_Delay_Init();

    /* Leave the DHT22 data line released/high while it is idle. */
    HAL_GPIO_WritePin(DHT_DATA_GPIO_Port, DHT_DATA_Pin, GPIO_PIN_SET);
    DHT_SetInput();

    if (SSD1306_Init(&hi2c1) == HAL_OK)
    {
        oled_ok = 1U;
        SSD1306_Clear();
        SSD1306_WriteString(CenteredX("ENVIRONODE", 1U), 16U, "ENVIRONODE", 1U);
        SSD1306_WriteString(CenteredX("STARTING", 1U), 36U, "STARTING", 1U);
        (void)SSD1306_UpdateScreen();
    }
    else
    {
        UART_Write("SSD1306 init failed\r\n");
    }

    /* Give the DHT22 time after power-up before the first reading. */
    HAL_Delay(1200U);

    if (DHT22_Read(&temperature, &humidity))
    {
        have_reading = 1U;
        UART_PrintReading(temperature, humidity);
    }
    else
    {
        UART_Write("DHT22 initial read failed\r\n");
    }

    last_sensor_read = HAL_GetTick();

    if (oled_ok)
    {
        display_dirty = 0U;
        Display_Render(temperature, humidity, have_reading);
    }
    /* USER CODE END 2 */

    /* Infinite loop */
    /* USER CODE BEGIN WHILE */
    while (1)
    {
        uint32_t now = HAL_GetTick();

        if ((uint32_t)(now - last_sensor_read) >= DHT_READ_INTERVAL_MS)
        {
            int16_t new_temperature;
            uint16_t new_humidity;

            last_sensor_read = now;

            if (DHT22_Read(&new_temperature, &new_humidity))
            {
                temperature = new_temperature;
                humidity = new_humidity;
                have_reading = 1U;
                display_dirty = 1U;
                UART_PrintReading(temperature, humidity);
            }
            else
            {
                /* Keep the last good values on-screen if we already have them. */
                UART_Write("DHT22 read failed\r\n");

                if (!have_reading)
                {
                    display_dirty = 1U;
                }
            }
        }

        if (oled_ok && display_dirty)
        {
            /* Clear first so an interrupt during drawing can set it again. */
            display_dirty = 0U;
            Display_Render(temperature, humidity, have_reading);
        }

        HAL_Delay(5U);
        /* USER CODE END WHILE */

        /* USER CODE BEGIN 3 */
    }
    /* USER CODE END 3 */
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
    RCC_OscInitTypeDef RCC_OscInitStruct = {0};
    RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

    __HAL_RCC_PWR_CLK_ENABLE();
    __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE2);

    RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI;
    RCC_OscInitStruct.HSIState = RCC_HSI_ON;
    RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
    RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
    RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSI;
    RCC_OscInitStruct.PLL.PLLM = 16;
    RCC_OscInitStruct.PLL.PLLN = 336;
    RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV4;
    RCC_OscInitStruct.PLL.PLLQ = 7;

    if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
    {
        Error_Handler();
    }

    RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK |
                                  RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
    RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
    RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
    RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV2;
    RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

    if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_2) != HAL_OK)
    {
        Error_Handler();
    }
}

/**
  * @brief I2C1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_I2C1_Init(void)
{
    /* USER CODE BEGIN I2C1_Init 0 */
    /* USER CODE END I2C1_Init 0 */

    /* USER CODE BEGIN I2C1_Init 1 */
    /* USER CODE END I2C1_Init 1 */

    hi2c1.Instance = I2C1;
    hi2c1.Init.ClockSpeed = 100000;
    hi2c1.Init.DutyCycle = I2C_DUTYCYCLE_2;
    hi2c1.Init.OwnAddress1 = 0;
    hi2c1.Init.AddressingMode = I2C_ADDRESSINGMODE_7BIT;
    hi2c1.Init.DualAddressMode = I2C_DUALADDRESS_DISABLE;
    hi2c1.Init.OwnAddress2 = 0;
    hi2c1.Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;
    hi2c1.Init.NoStretchMode = I2C_NOSTRETCH_DISABLE;

    if (HAL_I2C_Init(&hi2c1) != HAL_OK)
    {
        Error_Handler();
    }

    /* USER CODE BEGIN I2C1_Init 2 */
    /* USER CODE END I2C1_Init 2 */
}

/**
  * @brief USART2 Initialization Function
  * @param None
  * @retval None
  */
static void MX_USART2_UART_Init(void)
{
    /* USER CODE BEGIN USART2_Init 0 */
    /* USER CODE END USART2_Init 0 */

    /* USER CODE BEGIN USART2_Init 1 */
    /* USER CODE END USART2_Init 1 */

    huart2.Instance = USART2;
    huart2.Init.BaudRate = 115200;
    huart2.Init.WordLength = UART_WORDLENGTH_8B;
    huart2.Init.StopBits = UART_STOPBITS_1;
    huart2.Init.Parity = UART_PARITY_NONE;
    huart2.Init.Mode = UART_MODE_TX_RX;
    huart2.Init.HwFlowCtl = UART_HWCONTROL_NONE;
    huart2.Init.OverSampling = UART_OVERSAMPLING_16;

    if (HAL_UART_Init(&huart2) != HAL_OK)
    {
        Error_Handler();
    }

    /* USER CODE BEGIN USART2_Init 2 */
    /* USER CODE END USART2_Init 2 */
}

/**
  * @brief GPIO Initialization Function
  * @param None
  * @retval None
  */
static void MX_GPIO_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    /* USER CODE BEGIN MX_GPIO_Init_1 */
    /* USER CODE END MX_GPIO_Init_1 */

    __HAL_RCC_GPIOC_CLK_ENABLE();
    __HAL_RCC_GPIOH_CLK_ENABLE();
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();

    HAL_GPIO_WritePin(GPIOA, LD2_Pin | DHT_DATA_Pin, GPIO_PIN_RESET);

    GPIO_InitStruct.Pin = B1_Pin;
    GPIO_InitStruct.Mode = GPIO_MODE_IT_FALLING;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(B1_GPIO_Port, &GPIO_InitStruct);

    GPIO_InitStruct.Pin = LD2_Pin | DHT_DATA_Pin;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

    GPIO_InitStruct.Pin = EXTI_BUTTON_Pin;
    GPIO_InitStruct.Mode = GPIO_MODE_IT_FALLING;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(EXTI_BUTTON_GPIO_Port, &GPIO_InitStruct);

    HAL_NVIC_SetPriority(EXTI15_10_IRQn, 0, 0);
    HAL_NVIC_EnableIRQ(EXTI15_10_IRQn);

    /* USER CODE BEGIN MX_GPIO_Init_2 */
    /* USER CODE END MX_GPIO_Init_2 */
}

/* USER CODE BEGIN 4 */
void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin)
{
    if (GPIO_Pin == B1_Pin)
    {
        HAL_GPIO_TogglePin(LD2_GPIO_Port, LD2_Pin);
    }
    else if (GPIO_Pin == EXTI_BUTTON_Pin)
    {
        uint32_t now = HAL_GetTick();

        if ((uint32_t)(now - last_button_tick) >= BUTTON_DEBOUNCE_MS)
        {
            last_button_tick = now;

            if (display_mode == DISPLAY_BOTH)
            {
                display_mode = DISPLAY_TEMP;
            }
            else if (display_mode == DISPLAY_TEMP)
            {
                display_mode = DISPLAY_HUMIDITY;
            }
            else
            {
                display_mode = DISPLAY_BOTH;
            }

            display_dirty = 1U;
        }
    }
}
/* USER CODE END 4 */

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
    /* USER CODE BEGIN Error_Handler_Debug */
    __disable_irq();
    while (1)
    {
    }
    /* USER CODE END Error_Handler_Debug */
}

#ifdef USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: source line number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
    /* USER CODE BEGIN 6 */
    (void)file;
    (void)line;
    /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
