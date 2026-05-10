#include "stm32f4xx.h"
#include <stdio.h>
#include <stdint.h>


#define AMG8833_ADDR_W 0xD2
#define AMG8833_ADDR_R 0xD3

// If AD0 connected to GND:
// #define AMG8833_ADDR_W 0xD0
// #define AMG8833_ADDR_R 0xD1

void GPIO_Init(void);
void I2C1_Init(void);
void SPI1_Init(void);
void USART2_Init(void);
void TIM6_Init(void);
void TFT_Init(void);
void TFT_WriteCmd(uint8_t cmd);
void TFT_WriteData(uint8_t data);
void TFT_SetWindow(uint8_t x0, uint8_t y0, uint8_t x1, uint8_t y1);
void I2C_WriteReg(uint8_t dev_addr, uint8_t reg_addr, uint8_t data);
void I2C_ReadRegs(uint8_t dev_addr, uint8_t reg_addr, uint8_t *data, uint16_t len);
void UART_SendChar(char c);
void UART_SendString(char* str);
void delay_simple(volatile uint32_t count);

volatile uint8_t frame_ready = 0;

int16_t raw_temps[64];
int16_t interp_temps[1024];
uint16_t color_palette[256];

void delay_simple(volatile uint32_t count)
{
    while(count--);
}

void Generate_Palette(void)
{
    for(int i = 0; i < 256; i++)
    {
        uint8_t r = 0, g = 0, b = 0;

        if(i < 85)
        {
            b = 255 - (i * 3);
            g = i * 3;
        }
        else if(i < 170)
        {
            g = 255;
            r = (i - 85) * 3;
        }
        else
        {
            r = 255;
            g = 255 - ((i - 170) * 3);
        }

        color_palette[i] = ((r & 0xF8) << 8) |
                           ((g & 0xFC) << 3) |
                           (b >> 3);
    }
}

void Bilinear_Interpolate(int16_t *in, int16_t *out)
{
    int scale = 4;

    for(int y = 0; y < 32; y++)
    {
        for(int x = 0; x < 32; x++)
        {
            int gx = x / scale;
            int gy = y / scale;

            int rx = x % scale;
            int ry = y % scale;

            int gx2 = (gx < 7) ? gx + 1 : 7;
            int gy2 = (gy < 7) ? gy + 1 : 7;

            int16_t q11 = in[gy * 8 + gx];
            int16_t q21 = in[gy * 8 + gx2];
            int16_t q12 = in[gy2 * 8 + gx];
            int16_t q22 = in[gy2 * 8 + gx2];

            int un_x = scale - rx;
            int un_y = scale - ry;

            int val = (q11 * un_x * un_y +
                       q21 * rx * un_y +
                       q12 * un_x * ry +
                       q22 * rx * ry) / (scale * scale);

            out[y * 32 + x] = val;
        }
    }
}

// --- UART CSV Sending ---
// sprintf was not used for float.
// Because printf's float support is disabled in STM32, only the comma can be seen in TeraTerm.
void Send_UART_CSV(int16_t *temps)
{
    char buffer[32];

    UART_SendString("--- NEW FRAME ---\r\n");

    for(int i = 0; i < 64; i++)
    {
        int16_t raw = temps[i];

        int temp_x100 = raw * 25;   // 1 raw unit = 0.25 C, yani x100 için 25
        int whole = temp_x100 / 100;
        int frac = temp_x100 % 100;

        if(frac < 0)
            frac = -frac;

        sprintf(buffer, "%d.%02d", whole, frac);
        UART_SendString(buffer);

        if((i + 1) % 8 == 0)
            UART_SendString("\r\n");
        else
            UART_SendString(",");
    }

    UART_SendString("\r\n");
}

int main(void)
{
    GPIO_Init();
    I2C1_Init();
    SPI1_Init();
    USART2_Init();

    Generate_Palette();

    TFT_Init();

    UART_SendString("AMG8833 Thermal Camera Started\r\n");

    I2C_WriteReg(AMG8833_ADDR_W, 0x00, 0x00);
    delay_simple(50000);

    I2C_WriteReg(AMG8833_ADDR_W, 0x01, 0x3F);
    delay_simple(50000);

    I2C_WriteReg(AMG8833_ADDR_W, 0x02, 0x00);
    delay_simple(50000);

    TIM6_Init();

    int max_temp = -1000;
    int max_x = 0;
    int max_y = 0;

    while(1)
    {
        if(frame_ready)
        {
            frame_ready = 0;

            uint8_t raw_data[128];

            I2C_ReadRegs(AMG8833_ADDR_W, 0x80, raw_data, 128);

            for(int i = 0; i < 64; i++)
            {
                int16_t temp = (raw_data[i * 2 + 1] << 8) | raw_data[i * 2];

                if(temp & (1 << 11))
                    temp |= 0xF000;

                raw_temps[i] = temp;
            }

            Bilinear_Interpolate(raw_temps, interp_temps);

            TFT_SetWindow(0, 0, 127, 127);

            max_temp = -1000;

            for(int y = 0; y < 32; y++)
            {
                for(int dy = 0; dy < 4; dy++)
                {
                    for(int x = 0; x < 32; x++)
                    {
                        int16_t t = interp_temps[y * 32 + x];

                        if(t > max_temp)
                        {
                            max_temp = t;
                            max_x = x * 4;
                            max_y = y * 4;
                        }

                        int lut_index = (t - 80) * 255 / (160 - 80);

                        if(lut_index < 0)
                            lut_index = 0;

                        if(lut_index > 255)
                            lut_index = 255;

                        uint16_t color = color_palette[lut_index];

                        for(int dx = 0; dx < 4; dx++)
                        {
                            TFT_WriteData(color >> 8);
                            TFT_WriteData(color & 0xFF);
                        }
                    }
                }
            }

            Send_UART_CSV(raw_temps);
        }
    }
}

void GPIO_Init(void)
{
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOAEN |
                    RCC_AHB1ENR_GPIOBEN |
                    RCC_AHB1ENR_GPIOCEN;

    GPIOB->MODER &= ~((3 << (6 * 2)) | (3 << (7 * 2)));
    GPIOB->MODER |=  ((2 << (6 * 2)) | (2 << (7 * 2)));

    GPIOB->OTYPER |= GPIO_OTYPER_OT6 | GPIO_OTYPER_OT7;

    GPIOB->OSPEEDR |= (3 << (6 * 2)) | (3 << (7 * 2));

    GPIOB->PUPDR &= ~((3 << (6 * 2)) | (3 << (7 * 2)));
    GPIOB->PUPDR |=  ((1 << (6 * 2)) | (1 << (7 * 2)));

    GPIOB->AFR[0] &= ~((0xF << (6 * 4)) | (0xF << (7 * 4)));
    GPIOB->AFR[0] |=  ((4 << (6 * 4)) | (4 << (7 * 4)));

    GPIOA->MODER &= ~((3 << (5 * 2)) | (3 << (7 * 2)));
    GPIOA->MODER |=  ((2 << (5 * 2)) | (2 << (7 * 2)));

    GPIOA->OSPEEDR |= (3 << (5 * 2)) | (3 << (7 * 2));

    GPIOA->AFR[0] &= ~((0xF << (5 * 4)) | (0xF << (7 * 4)));
    GPIOA->AFR[0] |=  ((5 << (5 * 4)) | (5 << (7 * 4)));

    GPIOA->MODER &= ~(3 << (2 * 2));
    GPIOA->MODER |=  (2 << (2 * 2));

    GPIOA->AFR[0] &= ~(0xF << (2 * 4));
    GPIOA->AFR[0] |=  (7 << (2 * 4));

    GPIOC->MODER &= ~((3 << (4 * 2)) | (3 << (5 * 2)));
    GPIOC->MODER |=  ((1 << (4 * 2)) | (1 << (5 * 2)));
}

void TIM6_Init(void)
{
    RCC->APB1ENR |= RCC_APB1ENR_TIM6EN;

    TIM6->PSC = 8399;
    TIM6->ARR = 999;

    TIM6->DIER |= TIM_DIER_UIE;
    NVIC_EnableIRQ(TIM6_DAC_IRQn);

    TIM6->CR1 |= TIM_CR1_CEN;
}

void TIM6_DAC_IRQHandler(void)
{
    if(TIM6->SR & TIM_SR_UIF)
    {
        TIM6->SR &= ~TIM_SR_UIF;
        frame_ready = 1;
    }
}

void SPI1_Init(void)
{
    RCC->APB2ENR |= RCC_APB2ENR_SPI1EN;

    SPI1->CR1 = 0;

    SPI1->CR1 |= SPI_CR1_MSTR;
    SPI1->CR1 |= SPI_CR1_BR_0;
    SPI1->CR1 |= SPI_CR1_SSM;
    SPI1->CR1 |= SPI_CR1_SSI;

    SPI1->CR1 |= SPI_CR1_SPE;
}

void TFT_WriteCmd(uint8_t cmd)
{
    GPIOC->BSRR = GPIO_BSRR_BR5;

    while(!(SPI1->SR & SPI_SR_TXE));
    *((volatile uint8_t *)&SPI1->DR) = cmd;

    while(SPI1->SR & SPI_SR_BSY);
}

void TFT_WriteData(uint8_t data)
{
    GPIOC->BSRR = GPIO_BSRR_BS5;

    while(!(SPI1->SR & SPI_SR_TXE));
    *((volatile uint8_t *)&SPI1->DR) = data;

    while(SPI1->SR & SPI_SR_BSY);
}

void TFT_Init(void)
{
    GPIOC->BSRR = GPIO_BSRR_BS4;
    delay_simple(50000);

    GPIOC->BSRR = GPIO_BSRR_BR4;
    delay_simple(50000);

    GPIOC->BSRR = GPIO_BSRR_BS4;
    delay_simple(500000);

    TFT_WriteCmd(0x01);
    delay_simple(500000);

    TFT_WriteCmd(0x11);
    delay_simple(500000);

    TFT_WriteCmd(0x3A);
    TFT_WriteData(0x05);

    TFT_WriteCmd(0x36);
    TFT_WriteData(0xC8);

    TFT_WriteCmd(0x29);
}

void TFT_SetWindow(uint8_t x0, uint8_t y0, uint8_t x1, uint8_t y1)
{
    TFT_WriteCmd(0x2A);
    TFT_WriteData(0x00);
    TFT_WriteData(x0 + 2);
    TFT_WriteData(0x00);
    TFT_WriteData(x1 + 2);

    TFT_WriteCmd(0x2B);
    TFT_WriteData(0x00);
    TFT_WriteData(y0 + 3);
    TFT_WriteData(0x00);
    TFT_WriteData(y1 + 3);

    TFT_WriteCmd(0x2C);
}

void I2C1_Init(void)
{
    RCC->APB1ENR |= RCC_APB1ENR_I2C1EN;

    I2C1->CR1 |= I2C_CR1_SWRST;
    I2C1->CR1 &= ~I2C_CR1_SWRST;

    I2C1->CR2 = 16;
    I2C1->CCR = 80;
    I2C1->TRISE = 17;

    I2C1->CR1 |= I2C_CR1_PE;
}

void I2C_WriteReg(uint8_t dev_addr, uint8_t reg_addr, uint8_t data)
{
    I2C1->CR1 |= I2C_CR1_START;
    while(!(I2C1->SR1 & I2C_SR1_SB));

    I2C1->DR = dev_addr;
    while(!(I2C1->SR1 & I2C_SR1_ADDR));
    (void)I2C1->SR1;
    (void)I2C1->SR2;

    I2C1->DR = reg_addr;
    while(!(I2C1->SR1 & I2C_SR1_TXE));

    I2C1->DR = data;
    while(!(I2C1->SR1 & I2C_SR1_BTF));

    I2C1->CR1 |= I2C_CR1_STOP;
}

void I2C_ReadRegs(uint8_t dev_addr, uint8_t reg_addr, uint8_t *data, uint16_t len)
{
    I2C1->CR1 |= I2C_CR1_START;
    while(!(I2C1->SR1 & I2C_SR1_SB));

    I2C1->DR = dev_addr;
    while(!(I2C1->SR1 & I2C_SR1_ADDR));
    (void)I2C1->SR1;
    (void)I2C1->SR2;

    I2C1->DR = reg_addr;
    while(!(I2C1->SR1 & I2C_SR1_TXE));

    I2C1->CR1 |= I2C_CR1_START;
    while(!(I2C1->SR1 & I2C_SR1_SB));

    I2C1->DR = dev_addr | 0x01;
    while(!(I2C1->SR1 & I2C_SR1_ADDR));

    I2C1->CR1 |= I2C_CR1_ACK;

    (void)I2C1->SR1;
    (void)I2C1->SR2;

    for(uint16_t i = 0; i < len; i++)
    {
        if(i == len - 1)
        {
            I2C1->CR1 &= ~I2C_CR1_ACK;
            I2C1->CR1 |= I2C_CR1_STOP;
        }

        while(!(I2C1->SR1 & I2C_SR1_RXNE));
        data[i] = I2C1->DR;
    }
}

void USART2_Init(void)
{
    RCC->APB1ENR |= RCC_APB1ENR_USART2EN;

    USART2->BRR = 0x008B;

    USART2->CR1 |= USART_CR1_TE;
    USART2->CR1 |= USART_CR1_UE;
}

void UART_SendChar(char c)
{
    while(!(USART2->SR & USART_SR_TXE));
    USART2->DR = c;
}

void UART_SendString(char* str)
{
    while(*str)
    {
        UART_SendChar(*str++);
    }
}
