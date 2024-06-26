#include "a4988_driver.h"

#include "driver/rmt_tx.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// -- Configuration ------------------------------------------------------------
#define RMT_RESOLUTION (1 * 1000 * 1000)
#define PULSE_LENGTH_US ((uint16_t)2)
#define PI ((double)3.1415926)

const char* TAG = "A4988 Driver";

static const uint8_t STEP_MODE_TO_LOGIC[] = {
    0b000, 0b001, 0b010, 0b011, 0b111,
    0b1111 // Error value - corresponds to STEP_MODE_MAX. Shouldn't be returned.
};
static const rmt_transmit_config_t tx_config = {
    .loop_count = -1, .flags.eot_level = 0, .flags.queue_nonblocking = false
};

// -- Global State -------------------------------------------------------------
static a4988_driver_config_t driver_config;
static rmt_channel_handle_t tx_chan;
static rmt_encoder_handle_t copy_encoder_handle;
static step_mode_t step_mode = STEP_MODE_FULL;

// -- Public Functions ---------------------------------------------------------
void a4988_driver_init(const a4988_driver_config_t* config)
{
    driver_config = *config;
    uint64_t gpio_bit_mask = (1ULL << config->dir_gpio)
        | (1ULL << config->ms1_gpio) | (1ULL << config->ms2_gpio)
        | (1ULL << config->ms3_gpio) | (1ULL << config->sleep_gpio)
        | (1ULL << config->enable_gpio) | (1ULL << config->reset_gpio);

    gpio_config_t gpio_cfg = { .intr_type = GPIO_INTR_DISABLE,
                               .mode = GPIO_MODE_OUTPUT,
                               .pull_up_en = 0,
                               .pull_down_en = 0,
                               .pin_bit_mask = gpio_bit_mask };

    ESP_ERROR_CHECK(gpio_config(&gpio_cfg));

    ESP_ERROR_CHECK(gpio_set_level(config->dir_gpio, A4988_DEFAULT_DIRECTION));
    ESP_ERROR_CHECK(gpio_set_level(config->sleep_gpio, 0));
    ESP_ERROR_CHECK(gpio_set_level(config->reset_gpio, 1));
    ESP_ERROR_CHECK(gpio_set_level(config->enable_gpio, 1));
    ESP_ERROR_CHECK(gpio_set_level(config->ms1_gpio, 0));
    ESP_ERROR_CHECK(gpio_set_level(config->ms2_gpio, 0));
    ESP_ERROR_CHECK(gpio_set_level(config->ms3_gpio, 0));

    rmt_tx_channel_config_t tx_chan_config = {
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .gpio_num = config->step_gpio,
        .mem_block_symbols = 64,
        .resolution_hz = RMT_RESOLUTION, // 1MHz, 1us per tick
        .trans_queue_depth = 4,
        .flags.invert_out = false,
        .flags.with_dma = false,
    };
    ESP_ERROR_CHECK(rmt_new_tx_channel(&tx_chan_config, &tx_chan));
    const rmt_copy_encoder_config_t encoder_config = {};
    ESP_ERROR_CHECK(
        rmt_new_copy_encoder(&encoder_config, &copy_encoder_handle));
}
void a4988_rotate_continuous(double omega, bool direction)
{
    const uint16_t t1 = PULSE_LENGTH_US * (RMT_RESOLUTION / (1 * 1000 * 1000));
    static rmt_symbol_word_t block[3] = {
        { .level0 = 1, .level1 = 1, .duration0 = t1 / 2, .duration1 = t1 / 2 },
        {
            .level0 = 0,
            .level1 = 0,
        },
        {
            .level0 = 0,
            .level1 = 0,
        }
    };
    const uint8_t step_mode_mul = STEP_MODE_TO_MUL(step_mode);
    const uint32_t t2 =
        ((uint32_t)(((double)RMT_RESOLUTION * 2.0 * PI)
                    / (omega * 400.0 * step_mode_mul))
         - (uint32_t)t1);

    block[1].duration0 = (uint16_t)(t2 / 4);
    block[1].duration1 = (uint16_t)(t2 / 4);
    block[2].duration0 = (uint16_t)(t2 / 4);
    block[2].duration1 = (uint16_t)(t2 / 4);

    ESP_ERROR_CHECK(gpio_set_level(driver_config.dir_gpio, direction));
    ESP_ERROR_CHECK(gpio_set_level(driver_config.sleep_gpio, 1));
    ESP_ERROR_CHECK(gpio_set_level(driver_config.enable_gpio, 0));

    // Safety thing for the 
    vTaskDelay(1 / portTICK_PERIOD_MS);

    ESP_ERROR_CHECK(rmt_enable(tx_chan));
    ESP_ERROR_CHECK(rmt_transmit(
        tx_chan,
        copy_encoder_handle,
        &block,
        3 * sizeof(rmt_symbol_word_t),
        &tx_config));
}

void a4988_set_step_mode(step_mode_t mode)
{
    step_mode = mode;
    const uint8_t logic_levels = STEP_MODE_TO_LOGIC[mode];
    ESP_ERROR_CHECK(
        gpio_set_level(driver_config.ms1_gpio, (uint8_t)logic_levels & 0b001));
    ESP_ERROR_CHECK(gpio_set_level(
        driver_config.ms2_gpio, ((uint8_t)logic_levels & 0b010) >> 1));
    ESP_ERROR_CHECK(gpio_set_level(
        driver_config.ms3_gpio, ((uint8_t)logic_levels & 0b100) >> 2));
}

step_mode_t a4988_get_step_mode()
{
    return step_mode;
}

void a4988_stop()
{
    ESP_ERROR_CHECK(rmt_disable(tx_chan));
    ESP_ERROR_CHECK(gpio_set_level(driver_config.enable_gpio, 1));
    ESP_ERROR_CHECK(gpio_set_level(driver_config.sleep_gpio, 0));
}
