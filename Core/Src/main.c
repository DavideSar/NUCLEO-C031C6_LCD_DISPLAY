/* USER CODE BEGIN Header */

/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include <stdio.h>
#include <string.h>

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */

/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
/* ── I2C bit-bang pin (PB8=SCL, PB9=SDA) ───────────────────────────────── */
#define I2C_SCL_Pin        LL_GPIO_PIN_8
#define I2C_SCL_GPIO_Port  GPIOB
#define I2C_SDA_Pin        LL_GPIO_PIN_9
#define I2C_SDA_GPIO_Port  GPIOB

/* ── GPIO helpers LCD ───────────────────────────────────────────────────── */
#define RESET(c)    LL_GPIO_ResetOutputPin(c##_GPIO_Port, c##_Pin)
#define SET(c)      LL_GPIO_SetOutputPin(c##_GPIO_Port, c##_Pin)
#define TOGGLE(c)   LL_GPIO_TogglePin(c##_GPIO_Port, c##_Pin)
#define DIR_OUT(c)  LL_GPIO_SetPinMode(c##_GPIO_Port, c##_Pin, LL_GPIO_MODE_OUTPUT)
#define DIR_IN(c)   LL_GPIO_SetPinMode(c##_GPIO_Port, c##_Pin, LL_GPIO_MODE_INPUT)
#define READ(c)     LL_GPIO_IsInputPinSet(c##_GPIO_Port, c##_Pin)

/* ── HTS221 ─────────────────────────────────────────────────────────────── */
#define HTS221_ADDR_W       0xBE
#define HTS221_ADDR_R       0xBF
#define HTS221_WHO_AM_I     0x0F
#define HTS221_CTRL_REG1    0x20
#define HTS221_STATUS_REG   0x27
#define HTS221_HUM_OUT_L    0x28
#define HTS221_TEMP_OUT_L   0x2A
#define HTS221_H0_rH_x2     0x30
#define HTS221_H1_rH_x2     0x31
#define HTS221_T0_degC_x8   0x32
#define HTS221_T1_degC_x8   0x33
#define HTS221_T1_T0_MSB    0x35
#define HTS221_H0_T0_OUT_L  0x36
#define HTS221_H1_T0_OUT_L  0x3A
#define HTS221_T0_OUT_L     0x3C
#define HTS221_T1_OUT_L     0x3E
#define HTS221_MULTI        0x80

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
/* USER CODE BEGIN PV */
volatile uint8_t lcd_running = 1;
/* USER CODE END PV */

UART_HandleTypeDef huart2;

static int16_t H0_T0_out, H1_T0_out, T0_out, T1_out;
static float   H0_rh, H1_rh, T0_degC, T1_degC;

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_USART2_UART_Init(void);

/* USER CODE BEGIN PFP */


/* LCD */
void Delay_uS(int uSTim);
void setLcdDataPort(uint8_t nibble);
void lcdSendNibble(uint8_t nibble);
int  lcdCheckBusy(void);
void lcdSendCmd(char cmd);
void lcdSendChar(char data);
void lcdSetCursor(uint8_t row, uint8_t col);
void lcdPrint(const char *str);
void lcdInit(void);

/* I2C bit-bang */
static void    I2C_Start(void);
static void    I2C_Stop(void);
static uint8_t I2C_WriteByte(uint8_t byte);
static uint8_t I2C_ReadByte(uint8_t ack);

/* HTS221 */
static uint8_t HTS221_ReadReg(uint8_t reg);
static void    HTS221_WriteReg(uint8_t reg, uint8_t val);
static void    HTS221_ReadRegs(uint8_t reg, uint8_t *buf, uint8_t len);
static void    HTS221_Init(void);
static float   HTS221_ReadTemperature(void);
static float   HTS221_ReadHumidity(void);

/* ══════════════════════════════════════════════════════════════════════════
   LCD DRIVER (4-bit HD44780)
   ══════════════════════════════════════════════════════════════════════════ */
void Delay_uS(int uSTim)
{
    volatile int count = uSTim * 12;
    while (count--);
}

void setLcdDataPort(uint8_t nibble)
{
    if (nibble & 0x01) SET(LCD_DB4); else RESET(LCD_DB4);
    if (nibble & 0x02) SET(LCD_DB5); else RESET(LCD_DB5);
    if (nibble & 0x04) SET(LCD_DB6); else RESET(LCD_DB6);
    if (nibble & 0x08) SET(LCD_DB7); else RESET(LCD_DB7);
}

void lcdSendNibble(uint8_t nibble)
{
    SET(LCD_EN);
    Delay_uS(1);
    setLcdDataPort(nibble & 0x0F);
    Delay_uS(1);
    RESET(LCD_EN);
}

int lcdCheckBusy(void)
{
    uint8_t busy;
    int timeout = 0;
    DIR_IN(LCD_DB4); DIR_IN(LCD_DB5);
    DIR_IN(LCD_DB6); DIR_IN(LCD_DB7);
    RESET(LCD_RS); SET(LCD_RW);
    do {
        Delay_uS(1); SET(LCD_EN); Delay_uS(1);
        busy = READ(LCD_DB7);
        RESET(LCD_EN);
        Delay_uS(1); SET(LCD_EN); Delay_uS(1); RESET(LCD_EN);
    } while (busy && (++timeout < 200));
    RESET(LCD_RW);
    DIR_OUT(LCD_DB4); DIR_OUT(LCD_DB5);
    DIR_OUT(LCD_DB6); DIR_OUT(LCD_DB7);
    return (busy != 0);
}

void lcdSendCmd(char cmd)
{
    lcdCheckBusy();
    RESET(LCD_RS); RESET(LCD_RW);
    lcdSendNibble((cmd >> 4) & 0x0F);
    lcdSendNibble( cmd       & 0x0F);
}

void lcdSendChar(char data)
{
    lcdCheckBusy();
    SET(LCD_RS); RESET(LCD_RW);
    lcdSendNibble((data >> 4) & 0x0F);
    lcdSendNibble( data       & 0x0F);
}

void lcdSetCursor(uint8_t row, uint8_t col)
{
    uint8_t addr = (row == 0) ? (0x00 + col) : (0x40 + col);
    lcdSendCmd(0x80 | addr);
}

void lcdPrint(const char *str)
{
    while (*str) lcdSendChar(*str++);
}

void lcdInit(void)
{
    RESET(LCD_EN); RESET(LCD_RS); RESET(LCD_RW);
    LL_mDelay(50);
    setLcdDataPort(0x03); LL_mDelay(5);
    setLcdDataPort(0x03); Delay_uS(150);
    setLcdDataPort(0x03); Delay_uS(150);
    setLcdDataPort(0x02); Delay_uS(150);
    lcdSendCmd(0x28);
    lcdSendCmd(0x08);
    lcdSendCmd(0x01); LL_mDelay(2);
    lcdSendCmd(0x06);
    lcdSendCmd(0x0C);
}

/* ══════════════════════════════════════════════════════════════════════════
   I2C BIT-BANG
   PB8=SCL, PB9=SDA — open-drain con pull-up interno abilitato in MX_GPIO_Init
   Il secondo I2C_Start() genera il Repeated START richiesto dall'HTS221
   ══════════════════════════════════════════════════════════════════════════ */
#define I2C_DELAY() do { for(volatile int _i=0;_i<8;_i++); } while(0)

static void I2C_Start(void)
{
    LL_GPIO_SetOutputPin(I2C_SDA_GPIO_Port, I2C_SDA_Pin);
    LL_GPIO_SetOutputPin(I2C_SCL_GPIO_Port, I2C_SCL_Pin);
    I2C_DELAY();
    LL_GPIO_ResetOutputPin(I2C_SDA_GPIO_Port, I2C_SDA_Pin);
    I2C_DELAY();
    LL_GPIO_ResetOutputPin(I2C_SCL_GPIO_Port, I2C_SCL_Pin);
    I2C_DELAY();
}

static void I2C_Stop(void)
{
    LL_GPIO_ResetOutputPin(I2C_SDA_GPIO_Port, I2C_SDA_Pin);
    I2C_DELAY();
    LL_GPIO_SetOutputPin(I2C_SCL_GPIO_Port, I2C_SCL_Pin);
    I2C_DELAY();
    LL_GPIO_SetOutputPin(I2C_SDA_GPIO_Port, I2C_SDA_Pin);
    I2C_DELAY();
}

static uint8_t I2C_WriteByte(uint8_t byte)
{
    for (int i = 7; i >= 0; i--) {
        if ((byte >> i) & 1) LL_GPIO_SetOutputPin(I2C_SDA_GPIO_Port, I2C_SDA_Pin);
        else                 LL_GPIO_ResetOutputPin(I2C_SDA_GPIO_Port, I2C_SDA_Pin);
        I2C_DELAY();
        LL_GPIO_SetOutputPin(I2C_SCL_GPIO_Port, I2C_SCL_Pin);
        I2C_DELAY();
        LL_GPIO_ResetOutputPin(I2C_SCL_GPIO_Port, I2C_SCL_Pin);
        I2C_DELAY();
    }
    LL_GPIO_SetOutputPin(I2C_SDA_GPIO_Port, I2C_SDA_Pin);
    I2C_DELAY();
    LL_GPIO_SetOutputPin(I2C_SCL_GPIO_Port, I2C_SCL_Pin);
    I2C_DELAY();
    uint8_t nack = LL_GPIO_IsInputPinSet(I2C_SDA_GPIO_Port, I2C_SDA_Pin);
    LL_GPIO_ResetOutputPin(I2C_SCL_GPIO_Port, I2C_SCL_Pin);
    I2C_DELAY();
    return nack;
}

static uint8_t I2C_ReadByte(uint8_t ack)
{
    uint8_t data = 0;
    LL_GPIO_SetOutputPin(I2C_SDA_GPIO_Port, I2C_SDA_Pin);
    for (int i = 7; i >= 0; i--) {
        I2C_DELAY();
        LL_GPIO_SetOutputPin(I2C_SCL_GPIO_Port, I2C_SCL_Pin);
        I2C_DELAY();
        if (LL_GPIO_IsInputPinSet(I2C_SDA_GPIO_Port, I2C_SDA_Pin)) data |= (1 << i);
        LL_GPIO_ResetOutputPin(I2C_SCL_GPIO_Port, I2C_SCL_Pin);
        I2C_DELAY();
    }
    if (ack) LL_GPIO_ResetOutputPin(I2C_SDA_GPIO_Port, I2C_SDA_Pin);
    else     LL_GPIO_SetOutputPin(I2C_SDA_GPIO_Port, I2C_SDA_Pin);
    I2C_DELAY();
    LL_GPIO_SetOutputPin(I2C_SCL_GPIO_Port, I2C_SCL_Pin);
    I2C_DELAY();
    LL_GPIO_ResetOutputPin(I2C_SCL_GPIO_Port, I2C_SCL_Pin);
    I2C_DELAY();
    LL_GPIO_SetOutputPin(I2C_SDA_GPIO_Port, I2C_SDA_Pin);
    return data;
}

/* ══════════════════════════════════════════════════════════════════════════
   HTS221 DRIVER
   ══════════════════════════════════════════════════════════════════════════ */
static uint8_t HTS221_ReadReg(uint8_t reg)
{
    I2C_Start();
    I2C_WriteByte(HTS221_ADDR_W);
    I2C_WriteByte(reg);
    I2C_Start();                   /* Repeated START */
    I2C_WriteByte(HTS221_ADDR_R);
    uint8_t val = I2C_ReadByte(0); /* NACK sull'ultimo byte */
    I2C_Stop();
    return val;
}

static void HTS221_WriteReg(uint8_t reg, uint8_t val)
{
    I2C_Start();
    I2C_WriteByte(HTS221_ADDR_W);
    I2C_WriteByte(reg);
    I2C_WriteByte(val);
    I2C_Stop();
}

static void HTS221_ReadRegs(uint8_t reg, uint8_t *buf, uint8_t len)
{
    I2C_Start();
    I2C_WriteByte(HTS221_ADDR_W);
    I2C_WriteByte(reg | HTS221_MULTI); /* bit7=1: auto-increment */
    I2C_Start();                       /* Repeated START */
    I2C_WriteByte(HTS221_ADDR_R);
    for (uint8_t i = 0; i < len; i++)
        buf[i] = I2C_ReadByte(i < (len - 1)); /* ACK su tutti tranne l'ultimo */
    I2C_Stop();
}

static void HTS221_Init(void)
{
    HTS221_WriteReg(HTS221_CTRL_REG1, 0x81); /* PD=1, ODR=1Hz */
    HAL_Delay(10);

    uint8_t b[2];
    H0_rh = HTS221_ReadReg(HTS221_H0_rH_x2) / 2.0f;
    H1_rh = HTS221_ReadReg(HTS221_H1_rH_x2) / 2.0f;
    HTS221_ReadRegs(HTS221_H0_T0_OUT_L, b, 2);
    H0_T0_out = (int16_t)((b[1] << 8) | b[0]);
    HTS221_ReadRegs(HTS221_H1_T0_OUT_L, b, 2);
    H1_T0_out = (int16_t)((b[1] << 8) | b[0]);

    /* T0/T1 sono a 10 bit (unsigned), stored *8
       8 LSB in 0x32/0x33, 2 MSB in 0x35 bit[1:0] e bit[3:2] */
    uint8_t msb = HTS221_ReadReg(HTS221_T1_T0_MSB);
    T0_degC = (float)(((msb & 0x03) << 8) | HTS221_ReadReg(HTS221_T0_degC_x8)) / 8.0f;
    T1_degC = (float)((((msb >> 2) & 0x03) << 8) | HTS221_ReadReg(HTS221_T1_degC_x8)) / 8.0f;
    HTS221_ReadRegs(HTS221_T0_OUT_L, b, 2);
    T0_out = (int16_t)((b[1] << 8) | b[0]);
    HTS221_ReadRegs(HTS221_T1_OUT_L, b, 2);
    T1_out = (int16_t)((b[1] << 8) | b[0]);
}

static float HTS221_ReadTemperature(void)
{
    uint8_t b[2];
    HTS221_ReadRegs(HTS221_TEMP_OUT_L, b, 2);
    int16_t raw = (int16_t)((b[1] << 8) | b[0]);
    return T0_degC + (float)(raw - T0_out) * (T1_degC - T0_degC) / (float)(T1_out - T0_out);
}

static float HTS221_ReadHumidity(void)
{
    uint8_t b[2];
    HTS221_ReadRegs(HTS221_HUM_OUT_L, b, 2);
    int16_t raw = (int16_t)((b[1] << 8) | b[0]);
    float h = H0_rh + (float)(raw - H0_T0_out) * (H1_rh - H0_rh) / (float)(H1_T0_out - H0_T0_out);
    if (h <   0.0f) h =   0.0f;
    if (h > 100.0f) h = 100.0f;
    return h;
}

/* USER CODE END PFP */
/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{
	  /* USER CODE BEGIN 1 */

	  /* USER CODE END 1 */

	  /* MCU Configuration--------------------------------------------------------*/

	  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
    HAL_Init();

    /* USER CODE BEGIN Init */

    /* USER CODE END Init */

    /* Configure the system clock */
    SystemClock_Config();

    /* USER CODE BEGIN SysInit */

    /* USER CODE END SysInit */

    /* Initialize all configured peripherals */
    MX_GPIO_Init();
    MX_USART2_UART_Init();
    /* USER CODE BEGIN 2 */
    lcdInit();
    lcdSendCmd(0x01); /* Clear display */
    /* USER CODE END 2 */

      /* Infinite loop */
      /* USER CODE BEGIN WHILE */
    HTS221_Init();
    char buff[17];
    char uart_msg[48];

    while (1)
    {
        /* USER CODE END WHILE */

        /* USER CODE BEGIN 3 */
        if (lcd_running == 1)
        {
            float temp = HTS221_ReadTemperature();
            float hum  = HTS221_ReadHumidity();

            int t_int = (int)temp;
            int t_dec = (int)((temp - t_int) * 10);
            int h_int = (int)hum;
            int h_dec = (int)((hum  - h_int) * 10);
            if (t_dec < 0) t_dec = -t_dec;

            lcdSetCursor(0, 0);
            snprintf(buff, sizeof(buff), "Temp:    %3d.%d C", t_int, t_dec);
            lcdPrint(buff);

            lcdSetCursor(1, 0);
            snprintf(buff, sizeof(buff), "Umid:    %3d.%d %%", h_int, h_dec);
            lcdPrint(buff);

            snprintf(uart_msg, sizeof(uart_msg),
                     "Temp: %d.%d C  Umid: %d.%d%%\r\n",
                     t_int, t_dec, h_int, h_dec);
            HAL_UART_Transmit(&huart2, (uint8_t*)uart_msg, strlen(uart_msg), HAL_MAX_DELAY);
        }

        HAL_Delay(1000);
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

  __HAL_FLASH_SET_LATENCY(FLASH_LATENCY_1);

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
  RCC_OscInitStruct.HSEState = RCC_HSE_ON;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_HSE;
  RCC_ClkInitStruct.SYSCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_HCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_APB1_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_1) != HAL_OK)
  {
    Error_Handler();
  }
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
  huart2.Init.OneBitSampling = UART_ONE_BIT_SAMPLE_DISABLE;
  huart2.Init.ClockPrescaler = UART_PRESCALER_DIV1;
  huart2.AdvancedInit.AdvFeatureInit = UART_ADVFEATURE_NO_INIT;
  if (HAL_UART_Init(&huart2) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN USART2_Init 2 */

  /* USER CODE END USART2_Init 2 */

}

static void MX_GPIO_Init(void)
{
    LL_GPIO_InitTypeDef GPIO_InitStruct = {0};

    LL_IOP_GRP1_EnableClock(LL_IOP_GRP1_PERIPH_GPIOC);
    LL_IOP_GRP1_EnableClock(LL_IOP_GRP1_PERIPH_GPIOF);
    LL_IOP_GRP1_EnableClock(LL_IOP_GRP1_PERIPH_GPIOA);
    LL_IOP_GRP1_EnableClock(LL_IOP_GRP1_PERIPH_GPIOB);

    /* Reset pin LCD e LED */
    LL_GPIO_ResetOutputPin(INTERNAL_LED_GPIO_Port, INTERNAL_LED_Pin);
    LL_GPIO_ResetOutputPin(LCD_DB4_GPIO_Port, LCD_DB4_Pin);
    LL_GPIO_ResetOutputPin(LCD_EN_GPIO_Port,  LCD_EN_Pin);
    LL_GPIO_ResetOutputPin(LCD_DB7_GPIO_Port, LCD_DB7_Pin);
    LL_GPIO_ResetOutputPin(LCD_DB5_GPIO_Port, LCD_DB5_Pin);
    LL_GPIO_ResetOutputPin(LCD_DB6_GPIO_Port, LCD_DB6_Pin);
    LL_GPIO_ResetOutputPin(LCD_RW_GPIO_Port,  LCD_RW_Pin);
    LL_GPIO_ResetOutputPin(LCD_RS_GPIO_Port,  LCD_RS_Pin);

    /* I2C idle HIGH (open-drain: 1=rilasciato=HIGH grazie al pull-up) */
    LL_GPIO_SetOutputPin(I2C_SCL_GPIO_Port, I2C_SCL_Pin);
    LL_GPIO_SetOutputPin(I2C_SDA_GPIO_Port, I2C_SDA_Pin);

    /* LCD + LED: push-pull */
    GPIO_InitStruct.Mode       = LL_GPIO_MODE_OUTPUT;
    GPIO_InitStruct.Speed      = LL_GPIO_SPEED_FREQ_LOW;
    GPIO_InitStruct.OutputType = LL_GPIO_OUTPUT_PUSHPULL;
    GPIO_InitStruct.Pull       = LL_GPIO_PULL_NO;
    GPIO_InitStruct.Pin = INTERNAL_LED_Pin; LL_GPIO_Init(INTERNAL_LED_GPIO_Port, &GPIO_InitStruct);
    GPIO_InitStruct.Pin = LCD_DB4_Pin;      LL_GPIO_Init(LCD_DB4_GPIO_Port,      &GPIO_InitStruct);
    GPIO_InitStruct.Pin = LCD_EN_Pin;       LL_GPIO_Init(LCD_EN_GPIO_Port,        &GPIO_InitStruct);
    GPIO_InitStruct.Pin = LCD_DB7_Pin;      LL_GPIO_Init(LCD_DB7_GPIO_Port,      &GPIO_InitStruct);
    GPIO_InitStruct.Pin = LCD_DB5_Pin;      LL_GPIO_Init(LCD_DB5_GPIO_Port,      &GPIO_InitStruct);
    GPIO_InitStruct.Pin = LCD_DB6_Pin;      LL_GPIO_Init(LCD_DB6_GPIO_Port,      &GPIO_InitStruct);
    GPIO_InitStruct.Pin = LCD_RW_Pin;       LL_GPIO_Init(LCD_RW_GPIO_Port,        &GPIO_InitStruct);
    GPIO_InitStruct.Pin = LCD_RS_Pin;       LL_GPIO_Init(LCD_RS_GPIO_Port,        &GPIO_InitStruct);

    /* I2C SCL/SDA: open-drain + pull-up interno
       Necessario per il protocollo I2C: il master rilascia la linea
       e il pull-up la riporta HIGH; lo slave può fare clock stretching */
    GPIO_InitStruct.OutputType = LL_GPIO_OUTPUT_OPENDRAIN;
    GPIO_InitStruct.Pull       = LL_GPIO_PULL_UP;
    GPIO_InitStruct.Pin = I2C_SCL_Pin; LL_GPIO_Init(I2C_SCL_GPIO_Port, &GPIO_InitStruct);
    GPIO_InitStruct.Pin = I2C_SDA_Pin; LL_GPIO_Init(I2C_SDA_GPIO_Port, &GPIO_InitStruct);

    /* EXTI PC13 → USER BUTTON */
    LL_EXTI_InitTypeDef EXTI_InitStruct = {0};
    EXTI_InitStruct.Line_0_31   = LL_EXTI_LINE_13;
    EXTI_InitStruct.LineCommand = ENABLE;
    EXTI_InitStruct.Mode        = LL_EXTI_MODE_IT;
    EXTI_InitStruct.Trigger     = LL_EXTI_TRIGGER_RISING;
    LL_EXTI_Init(&EXTI_InitStruct);
    LL_GPIO_SetPinMode(GPIOC, LL_GPIO_PIN_13, LL_GPIO_MODE_INPUT);
    LL_GPIO_SetPinPull(GPIOC, LL_GPIO_PIN_13, LL_GPIO_PULL_NO);
    LL_EXTI_SetEXTISource(LL_EXTI_CONFIG_PORTC, LL_EXTI_CONFIG_LINE13);
    NVIC_SetPriority(EXTI4_15_IRQn, 0);
    NVIC_EnableIRQ(EXTI4_15_IRQn);
}

/* USER CODE BEGIN 4 */
void UserButton_Callback(void)
{
	static uint32_t ultimo = 0;
	uint32_t adesso = HAL_GetTick();
	if ((adesso - ultimo) > 200) {
		lcd_running = !lcd_running;
		LL_GPIO_TogglePin(INTERNAL_LED_GPIO_Port, INTERNAL_LED_Pin);
		ultimo = adesso;
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
  /* User can add his own implementation to report the HAL error return state */
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
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* User can add his own implementation to report the file name and line number,
     ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
