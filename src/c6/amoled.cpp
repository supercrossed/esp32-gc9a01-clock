#include "amoled.h"
#include "driver/spi_master.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_sh8601.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

namespace amoled {

// Pin map from the board's schematic table (LCD column).
static const int PIN_CS = 10, PIN_CLK = 11, PIN_RST = 3;
static const int PIN_D0 = 4, PIN_D1 = 5, PIN_D2 = 6, PIN_D3 = 7;
static const spi_host_device_t HOST = SPI2_HOST;
// The panel's RAM is wider than the glass; pixel 0 is at column 6.
static const int X_OFFSET = 6;
// Largest single push we will ever make (screen_amoled.cpp sizes its bands
// to stay under this).
static const int MAX_PUSH_ROWS = 64;

static esp_lcd_panel_io_handle_t io    = nullptr;
static esp_lcd_panel_handle_t    panel = nullptr;
static SemaphoreHandle_t         done  = nullptr;
static volatile bool             pending = false;

// Waveshare's init sequence for this panel.
static const sh8601_lcd_init_cmd_t INIT_CMDS[] = {
    {0x11, (uint8_t[]){0x00}, 0, 80},
    {0xC4, (uint8_t[]){0x80}, 1, 0},
    {0x53, (uint8_t[]){0x20}, 1, 1},
    {0x63, (uint8_t[]){0xFF}, 1, 1},
    {0x51, (uint8_t[]){0x00}, 1, 1},
    {0x29, (uint8_t[]){0x00}, 0, 10},
    {0x51, (uint8_t[]){0xFF}, 1, 0},
};

static bool IRAM_ATTR onTransDone(esp_lcd_panel_io_handle_t, esp_lcd_panel_io_event_data_t *, void *)
{
    BaseType_t hp = pdFALSE;
    xSemaphoreGiveFromISR(done, &hp);
    return hp == pdTRUE;
}

bool begin()
{
    done = xSemaphoreCreateBinary();

    spi_bus_config_t bus = {};
    bus.sclk_io_num     = PIN_CLK;
    bus.data0_io_num    = PIN_D0;
    bus.data1_io_num    = PIN_D1;
    bus.data2_io_num    = PIN_D2;
    bus.data3_io_num    = PIN_D3;
    bus.max_transfer_sz = WIDTH * MAX_PUSH_ROWS * 2;
    if (spi_bus_initialize(HOST, &bus, SPI_DMA_CH_AUTO) != ESP_OK) return false;

    esp_lcd_panel_io_spi_config_t ioc = {};
    ioc.cs_gpio_num         = PIN_CS;
    ioc.dc_gpio_num         = -1;
    ioc.spi_mode            = 0;
    ioc.pclk_hz             = 40 * 1000 * 1000;
    ioc.trans_queue_depth   = 4;
    ioc.on_color_trans_done = onTransDone;
    ioc.user_ctx            = nullptr;
    ioc.lcd_cmd_bits        = 32;
    ioc.lcd_param_bits      = 8;
    ioc.flags.quad_mode     = 1;
    if (esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)HOST, &ioc, &io) != ESP_OK) return false;

    sh8601_vendor_config_t vendor = {};
    vendor.init_cmds                = INIT_CMDS;
    vendor.init_cmds_size           = sizeof(INIT_CMDS) / sizeof(INIT_CMDS[0]);
    vendor.flags.use_qspi_interface = 1;

    esp_lcd_panel_dev_config_t pc = {};
    pc.reset_gpio_num  = PIN_RST;
    pc.rgb_ele_order   = LCD_RGB_ELEMENT_ORDER_RGB;
    pc.bits_per_pixel  = 16;
    pc.vendor_config   = &vendor;
    if (esp_lcd_new_panel_sh8601(io, &pc, &panel) != ESP_OK) return false;

    if (esp_lcd_panel_reset(panel) != ESP_OK)  return false;
    if (esp_lcd_panel_init(panel) != ESP_OK)   return false;
    esp_lcd_panel_disp_on_off(panel, true);
    return true;
}

void push(int x, int y, int w, int h, const uint16_t *pixels)
{
    if (!panel) return;
    pending = true;
    esp_lcd_panel_draw_bitmap(panel, x + X_OFFSET, y, x + X_OFFSET + w, y + h, pixels);
}

void waitIdle()
{
    if (!pending) return;
    // A timeout here used to be shrugged off: pending was cleared and the
    // give the ISR eventually posted stayed in the semaphore. The next wait
    // then returned instantly on that stale give while its own transfer was
    // still in flight, the renderer overwrote the buffer being clocked out,
    // and every wait after that was one transfer behind - permanently, from
    // a single timeout.
    //
    // So on a timeout, give the transfer time to land and then clear
    // anything it left behind, rather than racing the DMA with a stale
    // handshake. A binary semaphore cannot count, which is what made a
    // single missed pairing stick permanently.
    if (xSemaphoreTake(done, pdMS_TO_TICKS(500)) != pdTRUE) {
        vTaskDelay(pdMS_TO_TICKS(20));
        while (xSemaphoreTake(done, 0) == pdTRUE) { }
    }
    pending = false;
}

void setBrightness(uint8_t v)
{
    if (!io) return;
    // Same framing as the vendor's set_amoled_backlight(): opcode 0x02,
    // command 0x51 (display brightness), one parameter.
    uint32_t cmd = (0x02u << 24) | (0x51u << 8);
    esp_lcd_panel_io_tx_param(io, cmd, &v, 1);
}

} // namespace amoled
