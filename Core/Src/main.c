/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : HD44780 LCD driver (4-bit mode) for NUCLEO-C031C6
  *                   LCD: LCD-016N002M-TTI-ET (16x2)
  ******************************************************************************
  */
/* USER CODE END Header */

#include "main.h"

/* USER CODE BEGIN PD */
#define RESET(c)    LL_GPIO_ResetOutputPin(c##_GPIO_Port, c##_Pin)
#define SET(c)      LL_GPIO_SetOutputPin(c##_GPIO_Port, c##_Pin)
#define TOGGLE(c)   LL_GPIO_TogglePin(c##_GPIO_Port, c##_Pin)
#define DIR_OUT(c)  LL_GPIO_SetPinMode(c##_GPIO_Port, c##_Pin, LL_GPIO_MODE_OUTPUT)
#define DIR_IN(c)   LL_GPIO_SetPinMode(c##_GPIO_Port, c##_Pin, LL_GPIO_MODE_INPUT)
#define READ(c)     LL_GPIO_IsInputPinSet(c##_GPIO_Port, c##_Pin)
/* USER CODE END PD */

/* USER CODE BEGIN PFP */
void SystemClock_Config(void);
static void MX_GPIO_Init(void);

void Delay_uS(int uSTim);
void setLcdDataPort(uint8_t nibble);
void lcdPulseEnable(void);
void lcdSendNibble(uint8_t nibble);
void lcdSendCmd(char cmd);
void lcdSendChar(char data);
int  lcdCheckBusy(void);
void lcdSetCursor(uint8_t row, uint8_t col);
void lcdPrint(const char *str);
void lcdInit(void);
/* USER CODE END PFP */

/* USER CODE BEGIN 0 */

/**
 * @brief  Microsecond busy-wait delay.
 *         Calibrated for 8 MHz HSE (SYSCLK = 8 MHz, ~8 cycles/us).
 *         Adjust the multiplier if you change the clock source.
 */
void Delay_uS(int uSTim)
{
    volatile int count = uSTim * 2;   /* ~4 cycles/iteration at 8 MHz */
    while (count--);
}

/**
 * @brief  Write the lower 4 bits of 'nibble' to LCD data lines DB4–DB7.
 *         Bit0 → DB4, Bit1 → DB5, Bit2 → DB6, Bit3 → DB7
 */
void setLcdDataPort(uint8_t nibble)
{
    if (nibble & 0x01) SET(LCD_DB4); else RESET(LCD_DB4);
    if (nibble & 0x02) SET(LCD_DB5); else RESET(LCD_DB5);
    if (nibble & 0x04) SET(LCD_DB6); else RESET(LCD_DB6);  /* BUG FIX: was 0x03 */
    if (nibble & 0x08) SET(LCD_DB7); else RESET(LCD_DB7);
}

/**
 * @brief  Generate one EN pulse (high → low).
 *         HD44780 requires EN high for ≥ 230 ns; at 8 MHz one Delay_uS(1) ≈ 1 µs.
 */
void lcdPulseEnable(void)
{
    Delay_uS(1);
    SET(LCD_EN);
    Delay_uS(1);
    RESET(LCD_EN);
    Delay_uS(1);
}

/**
 * @brief  Put a nibble on the bus and clock it in.
 */
void lcdSendNibble(uint8_t nibble)
{
    setLcdDataPort(nibble & 0x0F);
    lcdPulseEnable();
}

/**
 * @brief  Poll the busy flag (DB7).
 *         Returns 0 when the LCD is ready, 1 on timeout.
 */
int lcdCheckBusy(void)
{
    uint8_t busy;
    int timeout = 0;

    /* Switch data lines to input */
    DIR_IN(LCD_DB4); DIR_IN(LCD_DB5);
    DIR_IN(LCD_DB6); DIR_IN(LCD_DB7);

    RESET(LCD_RS);   /* Instruction register */
    SET(LCD_RW);     /* Read mode           */

    do {
        Delay_uS(1);
        SET(LCD_EN);
        Delay_uS(1);
        busy = READ(LCD_DB7);   /* BF is on DB7 */
        RESET(LCD_EN);
        /* Clock out (and discard) the low nibble */
        Delay_uS(1);
        SET(LCD_EN);
        Delay_uS(1);
        RESET(LCD_EN);
    } while (busy && (++timeout < 200));

    /* Restore bus to write mode */
    RESET(LCD_RW);   /* BUG FIX: was SET — must return to write */
    DIR_OUT(LCD_DB4); DIR_OUT(LCD_DB5);
    DIR_OUT(LCD_DB6); DIR_OUT(LCD_DB7);

    return (busy != 0);   /* BUG FIX: function was missing a return */
}

/**
 * @brief  Send an 8-bit command in two 4-bit nibbles (high nibble first).
 *         RS = 0 (instruction).
 */
void lcdSendCmd(char cmd)
{
    lcdCheckBusy();
    RESET(LCD_RS);
    RESET(LCD_RW);
    lcdSendNibble((cmd >> 4) & 0x0F);   /* High nibble */
    lcdSendNibble( cmd       & 0x0F);   /* Low  nibble */
}

/**
 * @brief  Send an 8-bit data byte (character) in two 4-bit nibbles.
 *         RS = 1 (data).
 */
void lcdSendChar(char data)
{
    lcdCheckBusy();
    SET(LCD_RS);
    RESET(LCD_RW);
    lcdSendNibble((data >> 4) & 0x0F);  /* High nibble */
    lcdSendNibble( data       & 0x0F);  /* Low  nibble */
}

/**
 * @brief  Move cursor to (row, col).
 *         row: 0 = first line, 1 = second line.
 *         col: 0–15.
 */
void lcdSetCursor(uint8_t row, uint8_t col)
{
    uint8_t addr = (row == 0) ? (0x00 + col) : (0x40 + col);
    lcdSendCmd(0x80 | addr);
}

/**
 * @brief  Print a null-terminated string at the current cursor position.
 */
void lcdPrint(const char *str)
{
    while (*str)
        lcdSendChar(*str++);
}

/**
 * @brief  HD44780 4-bit initialisation sequence (from datasheet Figure 24).
 *         Must be called AFTER MX_GPIO_Init().
 */
void lcdInit(void)
{
    RESET(LCD_EN);
    RESET(LCD_RS);
    RESET(LCD_RW);

    LL_mDelay(50);              /* > 40 ms after Vcc rises to 2.7 V */

    /* ── Step 1: send 0x03 three times to guarantee 8-bit reset ── */
    setLcdDataPort(0x03);
    lcdPulseEnable();
    LL_mDelay(5);               /* > 4.1 ms */

    setLcdDataPort(0x03);
    lcdPulseEnable();
    Delay_uS(150);              /* > 100 µs */

    setLcdDataPort(0x03);
    lcdPulseEnable();
    Delay_uS(150);

    /* ── Step 2: switch to 4-bit interface ── */
    setLcdDataPort(0x02);
    lcdPulseEnable();
    Delay_uS(150);

    /* ── Step 3: configure with full 8-bit commands (two nibbles each) ── */
    lcdSendCmd(0x28);           /* Function set:  4-bit bus, 2 lines, 5×8 font */
    lcdSendCmd(0x08);           /* Display OFF */
    lcdSendCmd(0x01);           /* Clear display */
    LL_mDelay(2);               /* Clear takes > 1.52 ms */
    lcdSendCmd(0x06);           /* Entry mode: increment address, no display shift */
    lcdSendCmd(0x0C);           /* Display ON, cursor OFF, blink OFF */
}

/* USER CODE END 0 */

int main(void)
{
    /* USER CODE BEGIN 1 */
    /* USER CODE END 1 */

    SystemClock_Config();
    /* Configure SysTick for LL_mDelay (1 ms tick) */
    LL_Init1msTick(SystemCoreClock);
    LL_SetSystemCoreClock(SystemCoreClock);
    MX_GPIO_Init();

    /* USER CODE BEGIN 2 */

    lcdInit();                          /* Initialise LCD first          */

    lcdPrint("DAVIDE");

    /* USER CODE END 2 */

    while (1){}
}

/* ── Clock & GPIO init (unchanged from CubeMX output) ────────────────────── */

void SystemClock_Config(void)
{
    /* Set Flash latency to 1 wait state (required at 8 MHz with Vcc < 3.6 V) */
    LL_FLASH_SetLatency(LL_FLASH_LATENCY_1);
    while (LL_FLASH_GetLatency() != LL_FLASH_LATENCY_1);

    /* Enable HSE and wait until ready */
    LL_RCC_HSE_Enable();
    while (!LL_RCC_HSE_IsReady());

    /* SYSCLK = HSE (8 MHz), AHB /1, APB1 /1 */
    LL_RCC_SetSysClkSource(LL_RCC_SYS_CLKSOURCE_HSE);
    while (LL_RCC_GetSysClkSource() != LL_RCC_SYS_CLKSOURCE_STATUS_HSE);

    LL_RCC_SetAHBPrescaler(LL_RCC_SYSCLK_DIV_1);
    LL_RCC_SetAPB1Prescaler(LL_RCC_APB1_DIV_1);

    /* Update the global SystemCoreClock variable */
    LL_SetSystemCoreClock(8000000U);
}

static void MX_GPIO_Init(void)
{
    LL_GPIO_InitTypeDef GPIO_InitStruct = {0};

    LL_IOP_GRP1_EnableClock(LL_IOP_GRP1_PERIPH_GPIOC);
    LL_IOP_GRP1_EnableClock(LL_IOP_GRP1_PERIPH_GPIOF);
    LL_IOP_GRP1_EnableClock(LL_IOP_GRP1_PERIPH_GPIOA);
    LL_IOP_GRP1_EnableClock(LL_IOP_GRP1_PERIPH_GPIOB);

    LL_GPIO_ResetOutputPin(INTERNAL_LED_GPIO_Port, INTERNAL_LED_Pin);
    LL_GPIO_ResetOutputPin(LCD_DB4_GPIO_Port, LCD_DB4_Pin);
    LL_GPIO_ResetOutputPin(LCD_DB5_GPIO_Port, LCD_DB5_Pin);
    LL_GPIO_ResetOutputPin(LCD_DB6_GPIO_Port, LCD_DB6_Pin);
    LL_GPIO_ResetOutputPin(LCD_DB7_GPIO_Port, LCD_DB7_Pin);
    LL_GPIO_ResetOutputPin(LCD_EN_GPIO_Port,  LCD_EN_Pin);
    LL_GPIO_ResetOutputPin(LCD_RW_GPIO_Port,  LCD_RW_Pin);
    LL_GPIO_ResetOutputPin(LCD_RS_GPIO_Port,  LCD_RS_Pin);

    GPIO_InitStruct.Mode  = LL_GPIO_MODE_INPUT;
    GPIO_InitStruct.Pull  = LL_GPIO_PULL_NO;
    GPIO_InitStruct.Pin   = USER_BUTTON_Pin;
    LL_GPIO_Init(USER_BUTTON_GPIO_Port, &GPIO_InitStruct);

    GPIO_InitStruct.Mode       = LL_GPIO_MODE_ALTERNATE;
    GPIO_InitStruct.Speed      = LL_GPIO_SPEED_FREQ_LOW;
    GPIO_InitStruct.OutputType = LL_GPIO_OUTPUT_PUSHPULL;
    GPIO_InitStruct.Pull       = LL_GPIO_PULL_NO;
    GPIO_InitStruct.Alternate  = LL_GPIO_AF_1;
    GPIO_InitStruct.Pin = VCP_USART2_TX_Pin;
    LL_GPIO_Init(VCP_USART2_TX_GPIO_Port, &GPIO_InitStruct);
    GPIO_InitStruct.Pin = VCP_USART2_RX_Pin;
    LL_GPIO_Init(VCP_USART2_RX_GPIO_Port, &GPIO_InitStruct);

    /* All LCD + LED pins as push-pull output */
    GPIO_InitStruct.Mode       = LL_GPIO_MODE_OUTPUT;
    GPIO_InitStruct.Speed      = LL_GPIO_SPEED_FREQ_LOW;
    GPIO_InitStruct.OutputType = LL_GPIO_OUTPUT_PUSHPULL;
    GPIO_InitStruct.Pull       = LL_GPIO_PULL_NO;

    const uint32_t lcdPins[] = {
        LCD_DB4_Pin, LCD_DB5_Pin, LCD_DB6_Pin, LCD_DB7_Pin,
        LCD_EN_Pin,  LCD_RW_Pin,  LCD_RS_Pin,  INTERNAL_LED_Pin
    };
    GPIO_TypeDef *lcdPorts[] = {
        LCD_DB4_GPIO_Port, LCD_DB5_GPIO_Port, LCD_DB6_GPIO_Port, LCD_DB7_GPIO_Port,
        LCD_EN_GPIO_Port,  LCD_RW_GPIO_Port,  LCD_RS_GPIO_Port,  INTERNAL_LED_GPIO_Port
    };
    for (int i = 0; i < 8; i++) {
        GPIO_InitStruct.Pin = lcdPins[i];
        LL_GPIO_Init(lcdPorts[i], &GPIO_InitStruct);
    }
}

void Error_Handler(void)
{
    __disable_irq();
    while (1) {}
}

#ifdef USE_FULL_ASSERT
void assert_failed(uint8_t *file, uint32_t line) {}
#endif
