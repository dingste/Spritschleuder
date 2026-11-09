#include "display_manager.h"
#include "config.h"
#include "recipes.h"
#include "wifi_manager.h"
#include "qrcode.h"
#include "driver/gpio.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include <algorithm>
#include <cstring>

static const char *TAG = "display";
static constexpr uint16_t BLACK = 0x0000, WHITE = 0xffff, ORANGE = 0xfd20;
static constexpr uint16_t DARK = 0x2104, PANEL = 0x1082, MUTED = 0x8410;

// 5x7 glyphs, column-wise, bit 0 at the top. The UI deliberately uses capitals.
static const uint8_t *glyph(char c) {
    static const uint8_t blank[5] = {};
    static const uint8_t digits[][5] = {
        {0x3e,0x51,0x49,0x45,0x3e},{0x00,0x42,0x7f,0x40,0x00},{0x42,0x61,0x51,0x49,0x46},
        {0x21,0x41,0x45,0x4b,0x31},{0x18,0x14,0x12,0x7f,0x10},{0x27,0x45,0x45,0x45,0x39},
        {0x3c,0x4a,0x49,0x49,0x30},{0x01,0x71,0x09,0x05,0x03},{0x36,0x49,0x49,0x49,0x36},
        {0x06,0x49,0x49,0x29,0x1e}
    };
    static const uint8_t letters[][5] = {
        {0x7e,0x11,0x11,0x11,0x7e},{0x7f,0x49,0x49,0x49,0x36},{0x3e,0x41,0x41,0x41,0x22},
        {0x7f,0x41,0x41,0x22,0x1c},{0x7f,0x49,0x49,0x49,0x41},{0x7f,0x09,0x09,0x09,0x01},
        {0x3e,0x41,0x49,0x49,0x7a},{0x7f,0x08,0x08,0x08,0x7f},{0x00,0x41,0x7f,0x41,0x00},
        {0x20,0x40,0x41,0x3f,0x01},{0x7f,0x08,0x14,0x22,0x41},{0x7f,0x40,0x40,0x40,0x40},
        {0x7f,0x02,0x0c,0x02,0x7f},{0x7f,0x04,0x08,0x10,0x7f},{0x3e,0x41,0x41,0x41,0x3e},
        {0x7f,0x09,0x09,0x09,0x06},{0x3e,0x41,0x51,0x21,0x5e},{0x7f,0x09,0x19,0x29,0x46},
        {0x46,0x49,0x49,0x49,0x31},{0x01,0x01,0x7f,0x01,0x01},{0x3f,0x40,0x40,0x40,0x3f},
        {0x1f,0x20,0x40,0x20,0x1f},{0x3f,0x40,0x38,0x40,0x3f},{0x63,0x14,0x08,0x14,0x63},
        {0x07,0x08,0x70,0x08,0x07},{0x61,0x51,0x49,0x45,0x43}
    };
    static const uint8_t colon[5] = {0,0x36,0x36,0,0}, dash[5] = {0x08,0x08,0x08,0x08,0x08};
    if (c >= '0' && c <= '9') return digits[c - '0'];
    if (c >= 'A' && c <= 'Z') return letters[c - 'A'];
    if (c == ':') return colon;
    if (c == '-') return dash;
    return blank;
}

DisplayManager &DisplayManager::getInstance() { static DisplayManager instance; return instance; }

void DisplayManager::sendCommand(uint8_t command, const uint8_t *data, size_t length) {
    spi_transaction_t t = {}; t.length = 8; t.tx_buffer = &command;
    gpio_set_level(static_cast<gpio_num_t>(LCD_DC_PIN), 0); ESP_ERROR_CHECK(spi_device_polling_transmit(spi, &t));
    if (length) { t = {}; t.length = length * 8; t.tx_buffer = data; gpio_set_level(static_cast<gpio_num_t>(LCD_DC_PIN), 1); ESP_ERROR_CHECK(spi_device_polling_transmit(spi, &t)); }
}

void DisplayManager::init() {
    if (initialized) return;
    gpio_config_t gpio = {}; gpio.pin_bit_mask = (1ULL << LCD_DC_PIN) | (1ULL << LCD_RST_PIN); gpio.mode = GPIO_MODE_OUTPUT;
    ESP_ERROR_CHECK(gpio_config(&gpio));
    spi_bus_config_t bus = {}; bus.mosi_io_num = LCD_MOSI_PIN; bus.miso_io_num = -1; bus.sclk_io_num = LCD_SCK_PIN;
    bus.quadwp_io_num = -1; bus.quadhd_io_num = -1; bus.max_transfer_sz = DISPLAY_WIDTH * DISPLAY_HEIGHT * 2 + 8;
    ESP_ERROR_CHECK(spi_bus_initialize(LCD_SPI_HOST, &bus, SPI_DMA_CH_AUTO));
    spi_device_interface_config_t device = {}; device.clock_speed_hz = LCD_PIXEL_CLOCK_HZ; device.mode = 0;
    device.spics_io_num = LCD_CS_PIN; device.queue_size = 1;
    ESP_ERROR_CHECK(spi_bus_add_device(LCD_SPI_HOST, &device, &spi));
    gpio_set_level(static_cast<gpio_num_t>(LCD_RST_PIN), 0); vTaskDelay(pdMS_TO_TICKS(20));
    gpio_set_level(static_cast<gpio_num_t>(LCD_RST_PIN), 1); vTaskDelay(pdMS_TO_TICKS(120));
    sendCommand(0x01); vTaskDelay(pdMS_TO_TICKS(150)); // SWRESET
    sendCommand(0x11); vTaskDelay(pdMS_TO_TICKS(120)); // SLPOUT
    const uint8_t colmod = 0x05; sendCommand(0x3a, &colmod, 1); // RGB565
    // The ST7735 panel is wired in BGR order; MADCTL.BGR fixes red/blue swaps.
    const uint8_t madctl = 0x08; sendCommand(0x36, &madctl, 1);
    sendCommand(0x20); // INVOFF: dieses Board verwendet die nicht-invertierte Farbpalette
    sendCommand(0x13); sendCommand(0x29); vTaskDelay(pdMS_TO_TICKS(20));
    framebuffer = static_cast<uint16_t *>(heap_caps_malloc(DISPLAY_WIDTH * DISPLAY_HEIGHT * 2, MALLOC_CAP_DMA));
    if (!framebuffer) { ESP_LOGE(TAG, "Kein Speicher fuer Framebuffer"); return; }
    initialized = true; xTaskCreate(displayTask, "display", 4096, this, 3, &task_handle);
    ESP_LOGI(TAG, "ST7735 initialisiert (%dx%d)", DISPLAY_WIDTH, DISPLAY_HEIGHT);
}

void DisplayManager::fill(uint16_t color) { color = __builtin_bswap16(color); std::fill(framebuffer, framebuffer + DISPLAY_WIDTH * DISPLAY_HEIGHT, color); }
void DisplayManager::rectangle(int x, int y, int w, int h, uint16_t color) {
    color = __builtin_bswap16(color);
    int x0 = std::max(0, x), y0 = std::max(0, y), x1 = std::min(DISPLAY_WIDTH, x + w), y1 = std::min(DISPLAY_HEIGHT, y + h);
    for (int row = y0; row < y1; ++row) std::fill(framebuffer + row * DISPLAY_WIDTH + x0, framebuffer + row * DISPLAY_WIDTH + x1, color);
}
void DisplayManager::character(int x, int y, char value, uint16_t color, int scale) {
    const uint8_t *g = glyph(value >= 'a' && value <= 'z' ? value - 32 : value);
    for (int col = 0; col < 5; ++col) for (int row = 0; row < 7; ++row)
        if (g[col] & (1 << row)) rectangle(x + col * scale, y + row * scale, scale, scale, color);
}
void DisplayManager::text(int x, int y, const char *value, uint16_t color, int scale) {
    while (*value) { character(x, y, *value++, color, scale); x += 6 * scale; }
}
void DisplayManager::flush() {
    uint8_t area[] = {0, static_cast<uint8_t>(LCD_X_OFFSET), 0, static_cast<uint8_t>(LCD_X_OFFSET + DISPLAY_WIDTH - 1)};
    sendCommand(0x2a, area, sizeof(area)); area[1] = LCD_Y_OFFSET; area[3] = LCD_Y_OFFSET + DISPLAY_HEIGHT - 1;
    sendCommand(0x2b, area, sizeof(area)); sendCommand(0x2c);
    spi_transaction_t t = {}; t.length = DISPLAY_WIDTH * DISPLAY_HEIGHT * 16; t.tx_buffer = framebuffer;
    gpio_set_level(static_cast<gpio_num_t>(LCD_DC_PIN), 1); ESP_ERROR_CHECK(spi_device_polling_transmit(spi, &t));
}

void DisplayManager::render() {
    State current; std::string name; int ml; int64_t started, until; uint32_t duration;
    { std::lock_guard<std::mutex> lock(state_mutex); current = state; name = detail; ml = quantity_ml; started = mixing_started_us; duration = mixing_duration_ms; until = state_until_us; }
    fill(BLACK);
    uint16_t accent = ORANGE;
    rectangle(0, 0, DISPLAY_WIDTH, 19, accent);
    rectangle(0, 19, DISPLAY_WIDTH, 2, DARK);
    if (current == State::Boot) {
        text(49, 6, "START", WHITE);
        rectangle(7, 38, 114, 47, PANEL); text(13, 50, "SPRITSCHLEUDER", WHITE); text(34, 70, "BOOTET", MUTED);
    }
    else if (current == State::Ready) {
        text(43, 6, "BEREIT", BLACK);
        rectangle(7, 35, 114, 58, PANEL); text(31, 46, "COCKTAIL", WHITE); text(34, 64, "WAEHLEN", ORANGE);
        rectangle(35, 103, 58, 2, ORANGE);
    }
    else if (current == State::Error) { text(31, 5, "FEHLER", WHITE); text(4, 43, name.c_str(), WHITE); }
    else if (current == State::Aborted) {
        text(34, 6, "ABBRUCH", BLACK); rectangle(7, 39, 114, 45, PANEL); text(16, 55, "ABGEBROCHEN", ORANGE);
        if (esp_timer_get_time() >= until) { std::lock_guard<std::mutex> lock(state_mutex); if (state == State::Aborted) state = State::Ready; }
    }
    else if (current == State::Info) {
        text(49, 6, "INFO", BLACK);
        std::string ip = WifiManager::getInstance().ipAddress(), url = "http://" + ip;
        struct QrContext { DisplayManager *display; } context{this};
        esp_qrcode_config_t cfg = ESP_QRCODE_CONFIG_DEFAULT();
        cfg.max_qrcode_version = 3; cfg.qrcode_ecc_level = ESP_QRCODE_ECC_LOW; cfg.user_data = &context;
        cfg.display_func_with_cb = [](esp_qrcode_handle_t qr, void *data) {
            auto *ctx = static_cast<QrContext *>(data); int size = esp_qrcode_get_size(qr); int scale = std::max(1, 84 / (size + 8));
            int side = (size + 8) * scale, ox = (DISPLAY_WIDTH - side) / 2 + 4 * scale, oy = 25 + 4 * scale;
            ctx->display->rectangle((DISPLAY_WIDTH - side) / 2, 25, side, side, WHITE);
            for (int y = 0; y < size; ++y) for (int x = 0; x < size; ++x) if (esp_qrcode_get_module(qr, x, y)) ctx->display->rectangle(ox + x * scale, oy + y * scale, scale, scale, BLACK);
        };
        esp_qrcode_generate(&cfg, url.c_str()); text(19, 115, ip.c_str(), WHITE);
    }
    else {
        uint32_t elapsed = static_cast<uint32_t>(std::max<int64_t>(0, (esp_timer_get_time() - started) / 1000));
        int percent = duration ? static_cast<int>(std::min<uint32_t>(100, elapsed * 100 / duration)) : 100;
        text(43, 6, "MIXING", BLACK); text(4, 30, name.c_str(), WHITE, name.size() <= 10 ? 2 : 1);
        char amount[16]; snprintf(amount, sizeof(amount), "%d ML", ml); text(4, 57, amount, ORANGE);
        rectangle(4, 81, 120, 22, DARK); rectangle(7, 84, percent * 114 / 100, 16, ORANGE);
        char progress[8]; snprintf(progress, sizeof(progress), "%d", percent); text(48, 108, progress, WHITE); text(69, 108, "PCT", WHITE);
        if (elapsed >= duration) { std::lock_guard<std::mutex> lock(state_mutex); state = State::Ready; }
    }
    flush();
}

void DisplayManager::displayTask(void *arg) {
    auto *self = static_cast<DisplayManager *>(arg); while (true) { self->render(); vTaskDelay(pdMS_TO_TICKS(250)); }
}
void DisplayManager::showBootScreen() { std::lock_guard<std::mutex> lock(state_mutex); state = State::Boot; detail.clear(); }
void DisplayManager::showReady() { std::lock_guard<std::mutex> lock(state_mutex); state = State::Ready; detail.clear(); }
void DisplayManager::showAborted() { std::lock_guard<std::mutex> lock(state_mutex); state = State::Aborted; detail.clear(); state_until_us = esp_timer_get_time() + 3000000; }
void DisplayManager::showMainMenu() { showReady(); }
void DisplayManager::showError(const std::string &error) { std::lock_guard<std::mutex> lock(state_mutex); state = State::Error; detail = error; }
void DisplayManager::showMixing(const Recipe &recipe, int ml, uint16_t milliseconds_per_ml) {
    uint32_t duration = 0, total_ml = 0;
    for (uint8_t i = 0; i < recipe.num_ingredients; ++i) total_ml += recipe.ingredients[i].amount_ml;
    for (uint8_t i = 0; i < recipe.num_ingredients; ++i) {
        uint32_t ingredient_ml = recipe.ingredients[i].amount_ml * ml / total_ml;
        duration = std::max(duration, ingredient_ml * milliseconds_per_ml);
    }
    std::lock_guard<std::mutex> lock(state_mutex); state = State::Mixing; detail = recipe.name; quantity_ml = ml;
    mixing_started_us = esp_timer_get_time(); mixing_duration_ms = duration;
    ESP_LOGI(TAG, "Mische %s (%d ml, %lu ms)", recipe.name.c_str(), ml, static_cast<unsigned long>(mixing_duration_ms));
}
void DisplayManager::showInfo() { std::lock_guard<std::mutex> lock(state_mutex); state = State::Info; }
void DisplayManager::toggleInfo() { std::lock_guard<std::mutex> lock(state_mutex); state = state == State::Info ? State::Ready : State::Info; }
