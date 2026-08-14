#include "thermocouple.h"

#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_err.h"

// ---- MAX6675 thermocouple (SPI slave on its own bus). GPIO18 (LCD clock)
// isn't exposed on the T-Display headers, so use a second SPI host on free
// pins: SCK=17, CS=26, SO=27. Receive-only, so no MOSI needed. ----
#define TC_SCK_GPIO GPIO_NUM_17
#define TC_CS_GPIO  GPIO_NUM_26
#define TC_SO_GPIO  GPIO_NUM_27

static spi_device_handle_t s_spi;

void tc_init(void)
{
    // Dedicated receive-only SPI bus for the thermocouple
    const spi_bus_config_t buscfg = {
        .sclk_io_num = TC_SCK_GPIO,
        .mosi_io_num = GPIO_NUM_NC, // receive-only: no MOSI wired
        .miso_io_num = TC_SO_GPIO,
        .quadwp_io_num = GPIO_NUM_NC,
        .quadhd_io_num = GPIO_NUM_NC,
        .max_transfer_sz = 32,
    };
    ESP_ERROR_CHECK(spi_bus_initialize(SPI3_HOST, &buscfg, SPI_DMA_CH_AUTO));

    const spi_device_interface_config_t devcfg = {
        .mode = 0, // MAX6675 clocks data out on SCK falling edge, sampled on rising
        .clock_speed_hz = 1000000, // datasheet max is 4.3 MHz
        .spics_io_num = TC_CS_GPIO,
        .queue_size = 1,
        .flags = SPI_DEVICE_HALFDUPLEX,
    };
    ESP_ERROR_CHECK(spi_bus_add_device(SPI3_HOST, &devcfg, &s_spi));
}

int tc_read_tenths(void)
{
    uint8_t rx[2] = { 0, 0 };
    spi_transaction_t t = {
        .length = 0, // half-duplex, receive only
        .rxlength = 16,
        .rx_buffer = rx,
    };
    if (spi_device_transmit(s_spi, &t) != ESP_OK) {
        return -1;
    }
    uint16_t raw = ((uint16_t)rx[0] << 8) | rx[1];
    if (raw & 0x04) {
        return -1; // thermocouple input open
    }
    int16_t bits = (int16_t)((raw >> 3) & 0x0FFF);
    if (bits & 0x0800) {
        bits |= (int16_t)0xF000; // sign-extend 12-bit two's complement
    }
    return bits * 5 / 2; // 0.25 C per LSB -> tenths
}