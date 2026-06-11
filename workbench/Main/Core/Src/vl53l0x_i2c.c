/*
 * vl53l0x_i2c.c  -  software (bit-bang) I2C for VL53L0X on STM32H750
 *
 * SCL = TOF_SCL_PORT/PIN (PD11, was ultrasonic TRIG), push-pull master clock.
 * SDA = TOF_SDA_PORT/PIN (PD12, was ultrasonic ECHO), switched in/out per bit.
 * Both with internal pull-up enabled (add external 4.7k to 3.3V if the module
 * has no on-board pull-ups). Logic kept 1:1 with the proven reference; only the
 * pins, MCU header and delay are retargeted to H750. ASCII comments only.
 */
#include "vl53l0x_i2c.h"

/* ---- low level pin helpers (driven from config.h pin macros) ------------- */

static void VL_SDA_OUT(void)
{
    GPIO_InitTypeDef g = {0};
    g.Pin   = TOF_SDA_PIN;
    g.Mode  = GPIO_MODE_OUTPUT_PP;
    g.Pull  = GPIO_PULLUP;
    g.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(TOF_SDA_PORT, &g);
}

static void VL_SDA_IN(void)
{
    GPIO_InitTypeDef g = {0};
    g.Pin   = TOF_SDA_PIN;
    g.Mode  = GPIO_MODE_INPUT;
    g.Pull  = GPIO_PULLUP;
    g.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(TOF_SDA_PORT, &g);
}

static void VL_IIC_SCL(uint8_t bit)
{
    HAL_GPIO_WritePin(TOF_SCL_PORT, TOF_SCL_PIN, bit ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

static void VL_IIC_SDA(uint8_t bit)
{
    HAL_GPIO_WritePin(TOF_SDA_PORT, TOF_SDA_PIN, bit ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

static uint8_t VL_READ_SDA(void)
{
    return (HAL_GPIO_ReadPin(TOF_SDA_PORT, TOF_SDA_PIN) == GPIO_PIN_SET) ? 1u : 0u;
}

/* ~1us-granularity busy wait at 480MHz (timing is non-critical for bit-bang) */
static void i2c_delay(uint32_t us)
{
    volatile uint32_t n = us * 60u;
    while (n--) { __NOP(); }
}

/* ---- I2C init: SCL/SDA as push-pull outputs, idle high -------------------- */
void VL53L0X_i2c_init(void)
{
    GPIO_InitTypeDef g = {0};

    /* enable GPIO clock for the SCL/SDA port(s). PD11/PD12 share GPIOD. */
    __HAL_RCC_GPIOD_CLK_ENABLE();

    g.Mode  = GPIO_MODE_OUTPUT_PP;
    g.Pull  = GPIO_PULLUP;
    g.Speed = GPIO_SPEED_FREQ_LOW;

    g.Pin = TOF_SCL_PIN;
    HAL_GPIO_Init(TOF_SCL_PORT, &g);
    g.Pin = TOF_SDA_PIN;
    HAL_GPIO_Init(TOF_SDA_PORT, &g);

    VL_IIC_SCL(1);
    VL_IIC_SDA(1);
}

/* ---- primitives ---------------------------------------------------------- */

static void VL_IIC_Start(void)
{
    VL_SDA_OUT();
    VL_IIC_SDA(1);
    VL_IIC_SCL(1);
    i2c_delay(4);
    VL_IIC_SDA(0);          /* START: SDA high->low while SCL high */
    i2c_delay(4);
    VL_IIC_SCL(0);
}

static void VL_IIC_Stop(void)
{
    VL_SDA_OUT();
    VL_IIC_SCL(0);
    VL_IIC_SDA(0);
    i2c_delay(4);
    VL_IIC_SCL(1);
    VL_IIC_SDA(1);          /* STOP: SDA low->high while SCL high */
    i2c_delay(4);
}

/* returns 1 on ACK timeout (fail), 0 on ACK ok */
static uint8_t VL_IIC_Wait_Ack(void)
{
    uint16_t t = 0;
    VL_SDA_IN();
    VL_IIC_SDA(1); i2c_delay(1);
    VL_IIC_SCL(1); i2c_delay(1);
    while (VL_READ_SDA())
    {
        if (++t > 250)
        {
            VL_IIC_Stop();
            return 1;
        }
    }
    VL_IIC_SCL(0);
    return 0;
}

static void VL_IIC_Ack(void)
{
    VL_IIC_SCL(0);
    VL_SDA_OUT();
    VL_IIC_SDA(0);
    i2c_delay(2);
    VL_IIC_SCL(1);
    i2c_delay(2);
    VL_IIC_SCL(0);
}

static void VL_IIC_NAck(void)
{
    VL_IIC_SCL(0);
    VL_SDA_OUT();
    VL_IIC_SDA(1);
    i2c_delay(2);
    VL_IIC_SCL(1);
    i2c_delay(2);
    VL_IIC_SCL(0);
}

static void VL_IIC_Send_Byte(uint8_t txd)
{
    uint8_t t;
    VL_SDA_OUT();
    VL_IIC_SCL(0);
    for (t = 0; t < 8; t++)
    {
        VL_IIC_SDA((txd & 0x80) ? 1u : 0u);
        txd <<= 1;
        i2c_delay(2);
        VL_IIC_SCL(1);
        i2c_delay(2);
        VL_IIC_SCL(0);
        i2c_delay(2);
    }
}

/* ack=1 -> send ACK after read, ack=0 -> send NACK */
static uint8_t VL_IIC_Read_Byte(uint8_t ack)
{
    uint8_t i, receive = 0;
    VL_SDA_IN();
    for (i = 0; i < 8; i++)
    {
        VL_IIC_SCL(0);
        i2c_delay(4);
        VL_IIC_SCL(1);
        receive <<= 1;
        if (VL_READ_SDA()) receive++;
        i2c_delay(4);
    }
    if (ack) VL_IIC_Ack();
    else     VL_IIC_NAck();
    return receive;
}

/* ---- multi-byte register access (matches reference VL_IIC_*_nByte) ------- */

static uint8_t VL_IIC_Write_nByte(uint8_t slave_addr, uint8_t reg, uint16_t len, uint8_t *buf)
{
    VL_IIC_Start();
    VL_IIC_Send_Byte(slave_addr);          /* write */
    if (VL_IIC_Wait_Ack()) { VL_IIC_Stop(); return 1; }
    VL_IIC_Send_Byte(reg);
    VL_IIC_Wait_Ack();
    while (len--)
    {
        VL_IIC_Send_Byte(*buf++);
        VL_IIC_Wait_Ack();
    }
    VL_IIC_Stop();
    return 0;
}

static uint8_t VL_IIC_Read_nByte(uint8_t slave_addr, uint8_t reg, uint16_t len, uint8_t *buf)
{
    VL_IIC_Start();
    VL_IIC_Send_Byte(slave_addr);          /* write reg pointer */
    if (VL_IIC_Wait_Ack()) { VL_IIC_Stop(); return 1; }
    VL_IIC_Send_Byte(reg);
    VL_IIC_Wait_Ack();

    VL_IIC_Start();
    VL_IIC_Send_Byte(slave_addr | 0x01);   /* read */
    VL_IIC_Wait_Ack();
    while (len)
    {
        *buf = VL_IIC_Read_Byte((len == 1) ? 0u : 1u);  /* NACK last byte */
        buf++;
        len--;
    }
    VL_IIC_Stop();
    return 0;
}

/* ---- public API used by vl53l0x_platform.c ------------------------------- */

uint8_t VL53L0X_write_multi(uint8_t address, uint8_t index, uint8_t *pdata, uint16_t count)
{
    return VL_IIC_Write_nByte(address, index, count, pdata) ? STATUS_FAIL : STATUS_OK;
}

uint8_t VL53L0X_read_multi(uint8_t address, uint8_t index, uint8_t *pdata, uint16_t count)
{
    return VL_IIC_Read_nByte(address, index, count, pdata) ? STATUS_FAIL : STATUS_OK;
}

uint8_t VL53L0X_write_byte(uint8_t address, uint8_t index, uint8_t data)
{
    return VL53L0X_write_multi(address, index, &data, 1);
}

uint8_t VL53L0X_write_word(uint8_t address, uint8_t index, uint16_t data)
{
    uint8_t buf[2];
    buf[0] = (uint8_t)(data >> 8);
    buf[1] = (uint8_t)(data & 0xFF);
    return VL53L0X_write_multi(address, index, buf, 2);
}

uint8_t VL53L0X_write_dword(uint8_t address, uint8_t index, uint32_t data)
{
    uint8_t buf[4];
    buf[0] = (uint8_t)(data >> 24);
    buf[1] = (uint8_t)((data >> 16) & 0xFF);
    buf[2] = (uint8_t)((data >> 8) & 0xFF);
    buf[3] = (uint8_t)(data & 0xFF);
    return VL53L0X_write_multi(address, index, buf, 4);
}

uint8_t VL53L0X_read_byte(uint8_t address, uint8_t index, uint8_t *pdata)
{
    return VL53L0X_read_multi(address, index, pdata, 1);
}

uint8_t VL53L0X_read_word(uint8_t address, uint8_t index, uint16_t *pdata)
{
    uint8_t buf[2];
    uint8_t st = VL53L0X_read_multi(address, index, buf, 2);
    *pdata = ((uint16_t)buf[0] << 8) + buf[1];
    return st;
}

uint8_t VL53L0X_read_dword(uint8_t address, uint8_t index, uint32_t *pdata)
{
    uint8_t buf[4];
    uint8_t st = VL53L0X_read_multi(address, index, buf, 4);
    *pdata = ((uint32_t)buf[0] << 24) + ((uint32_t)buf[1] << 16)
           + ((uint32_t)buf[2] << 8) + buf[3];
    return st;
}
