#include "wifi_board.h"
// #include "audio_codecs/box_audio_codec.h"
// #include "audio_codecs/es8388_audio_codec.h"
#include "JPet_audio_codec.h"
#include "application.h"
// #include "display/lcd_display.h"
// #include "display/no_display.h"
#include "button.h"
// #include "iot/thing_manager.h"

#include "esp_lcd_panel_ops.h"
#include "esp_lcd_mipi_dsi.h"
#include "esp_ldo_regulator.h"

// #include "esp_lcd_jd9365.h"
#include "config.h"

#include <wifi_station.h>
#include <esp_log.h>
#include <driver/i2c_master.h>
#include <esp_lvgl_port.h>
#include "esp_lcd_touch_gt911.h"

#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"
#include "driver/sdmmc_host.h"
#include "sd_pwr_ctrl_by_on_chip_ldo.h"
#include "driver/jpeg_decode.h"

#include "esp_timer.h"
#include "driver/gptimer.h"
#include "esp_lcd_st7701.h"
#include "JPet_lcd_display.h"
#include "mcp_server.h"
#include "led/gpio_led.h"

// char JPet_subtitles[256]={0};
// bool JPet_subtitlesChanged = false;

#define TAG "JPet"
#define TEST_LCD_BIT_PER_PIXEL (24)
#define TEST_MIPI_DPI_PX_FORMAT (LCD_COLOR_PIXEL_FORMAT_RGB888)
// #define TEST_MIPI_DPI_PX_FORMAT (LCD_COLOR_PIXEL_FORMAT_RGB666)
// #define TEST_MIPI_DPI_PX_FORMAT (LCD_COLOR_PIXEL_FORMAT_RGB565)

static esp_ldo_channel_handle_t ldo_mipi_phy = NULL;
static esp_lcd_dsi_bus_handle_t mipi_dsi_bus = NULL;
static esp_lcd_panel_io_handle_t mipi_dbi_io = NULL;
static esp_lcd_panel_handle_t disp_panel = NULL;


static const st7701_lcd_init_cmd_t lcdst7701_init_cmds[] = {
    {0xFF, (uint8_t []){0x77, 0x01, 0x00, 0x00, 0x13}, 5, 0},
    {0xEF, (uint8_t []){0x08}, 1, 0},
    {0x3A, (uint8_t []){0x77}, 1, 0},    // 888
    // {0x3A, (uint8_t []){0x66}, 1, 0},    // 666
    // {0x3A, (uint8_t []){0x55}, 1, 0},    // 565
    {0xFF, (uint8_t []){0x77, 0x01, 0x00, 0x00, 0x10}, 5, 0},
    {0xC0, (uint8_t []){0x3B, 0x00}, 2, 0},
    {0xC1, (uint8_t []){0x09, 0x05}, 2, 0},
    {0xC2, (uint8_t []){0x07, 0x02}, 2, 0},
    {0xC6, (uint8_t []){0x21}, 1, 0},
    {0xCC, (uint8_t []){0x30}, 1, 0},
    {0xB0, (uint8_t []){0x40, 0x87, 0x92, 0x0C, 0x90, 0x07, 0x05, 0x09, 0x08, 0x21, 0x06, 0x55, 0x12, 0x25, 0xA8, 0x4F}, 16, 0},
    {0xB1, (uint8_t []){0xC0, 0x53, 0xD9, 0x0F, 0x12, 0x05, 0x07, 0x08, 0x07, 0x23, 0x08, 0x17, 0x15, 0xA3, 0xA6, 0xD6}, 16, 0},
    {0xFF, (uint8_t []){0x77, 0x01, 0x00, 0x00, 0x11}, 5, 0},
    {0xB0, (uint8_t []){0x6D}, 1, 0},
    {0xB1, (uint8_t []){0x28}, 1, 0},
    {0xB2, (uint8_t []){0x87}, 1, 0},
    {0xB3, (uint8_t []){0x80}, 1, 0},
    {0xB5, (uint8_t []){0x45}, 1, 0},
    {0xB7, (uint8_t []){0x87}, 1, 0},
    {0xB8, (uint8_t []){0x33}, 1, 0},
    {0xB9, (uint8_t []){0x10}, 1, 0},
    {0xBB, (uint8_t []){0x03}, 1, 0},
    {0xC0, (uint8_t []){0x03}, 1, 0},
    {0xC1, (uint8_t []){0x78}, 1, 0},
    {0xC2, (uint8_t []){0x78}, 1, 0},
    {0xD0, (uint8_t []){0x88}, 1, 0},
    {0xFF, (uint8_t []){0x77, 0x01, 0x00, 0x00, 0x11}, 5, 0},
    {0xE0, (uint8_t []){0x00, 0x18, 0x00, 0x00, 0x00, 0x20}, 6, 0},
    {0xE1, (uint8_t []){0x05, 0xA0, 0x00, 0xA0, 0x04, 0x0A, 0x00, 0xA0, 0x00, 0x44, 0x44}, 11, 0},
    {0xE2, (uint8_t []){0x11, 0x11, 0x44, 0x44, 0xEA, 0xA0, 0x00, 0x00, 0xE9, 0xA0, 0x00, 0x00}, 12, 0},
    {0xE3, (uint8_t []){0x00, 0x00, 0x11, 0x11}, 4, 0},
    {0xE4, (uint8_t []){0x44, 0x44}, 2, 0},
    {0xE5, (uint8_t []){0x06, 0xE5, 0xD8, 0xA0, 0x08, 0xE7, 0xD8, 0xA0, 0x0A, 0xE9, 0xD8, 0xA0, 0x0C, 0xEB, 0xD8, 0xA0}, 16, 0},
    {0xE6, (uint8_t []){0x00, 0x00, 0x11, 0x11}, 4, 0},
    {0xE7, (uint8_t []){0x44, 0x44}, 2, 0},
    {0xE8, (uint8_t []){0x05, 0xE4, 0xD8, 0xA0, 0x07, 0xE6, 0xD8, 0xA0, 0x09, 0xE8, 0xD8, 0xA0, 0x0B, 0xEA, 0xD8, 0xA0}, 16, 0},
    {0xEB, (uint8_t []){0x02, 0x00, 0xE4, 0xE4, 0x88, 0x00, 0x10}, 7, 0},
    {0xEC, (uint8_t []){0x3D, 0x02, 0x00}, 3, 0},
    {0xED, (uint8_t []){0x20, 0x76, 0x54, 0x98, 0xBA, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xAB, 0x89, 0x45, 0x67, 0x02}, 16, 0},
    {0xEF, (uint8_t []){0x08, 0x08, 0x08, 0x45, 0x1F, 0x54}, 6, 0},
    {0xFF, (uint8_t []){0x77, 0x01, 0x00, 0x00, 0x13}, 5, 0},
    {0xE8, (uint8_t []){0x00, 0x0E}, 2, 0},
    {0x11, (uint8_t []){0x00}, 0, 120},
    {0xE8, (uint8_t []){0x00, 0x0C}, 2, 20},
    {0xE8, (uint8_t []){0x00, 0x00}, 2, 0},
    {0xE6, (uint8_t []){0x16, 0x7C}, 2, 0},
    {0xFF, (uint8_t []){0x77, 0x01, 0x00, 0x00, 0x00}, 5, 0},
    // {0x11, (uint8_t []){0x00}, 0, 120},
    {0x29, (uint8_t []){0x00}, 0, 120},
    {0x36, (uint8_t []){0x00}, 1, 0},

    // {0xFF, (uint8_t []){0x77, 0x01, 0x00, 0x00, 0x12}, 5, 0}, /* This part of the parameters can be used for screen self-test */
    // {0xD1, (uint8_t []){0x81}, 1, 0},
    // {0xD2, (uint8_t []){0x08}, 1, 0},
};


class JPet : public WifiBoard {
private:
    i2c_master_bus_handle_t i2c_bus_;
    Button boot_button_;
    Button volume_up_button_;
    Button volume_down_button_;
    NoDisplay *display_;
    bool led_on_ = false;


    void InitializeCodecI2c() {
        // Initialize I2C peripheral
        i2c_master_bus_config_t i2c_bus_cfg = {
            .i2c_port = I2C_NUM_1,
            .sda_io_num = AUDIO_CODEC_I2C_SDA_PIN,
            .scl_io_num = AUDIO_CODEC_I2C_SCL_PIN,
            .clk_source = I2C_CLK_SRC_DEFAULT,
            .glitch_ignore_cnt = 7,
            .intr_priority = 0,
            .trans_queue_depth = 0,
            .flags = {
                .enable_internal_pullup = 1,
            },
        };
        ESP_ERROR_CHECK(i2c_new_master_bus(&i2c_bus_cfg, &i2c_bus_));
    }

    static esp_err_t bsp_enable_dsi_phy_power(void) {
#if MIPI_DSI_PHY_PWR_LDO_CHAN > 0
        // Turn on the power for MIPI DSI PHY, so it can go from "No Power" state to "Shutdown" state
        static esp_ldo_channel_handle_t phy_pwr_chan = NULL;
        esp_ldo_channel_config_t ldo_cfg = {
            .chan_id = MIPI_DSI_PHY_PWR_LDO_CHAN,
            .voltage_mv = MIPI_DSI_PHY_PWR_LDO_VOLTAGE_MV,
        };
        esp_ldo_acquire_channel(&ldo_cfg, &phy_pwr_chan);
        ESP_LOGI(TAG, "MIPI DSI PHY Powered on");
#endif // BSP_MIPI_DSI_PHY_PWR_LDO_CHAN > 0

        return ESP_OK;
    }

    void InitializeLCDST7701(){
        ESP_LOGI(TAG, "MIPI DSI PHY Powered on");
        esp_ldo_channel_config_t ldo_mipi_phy_config = {
            .chan_id = MIPI_DSI_PHY_PWR_LDO_CHAN,
            .voltage_mv = MIPI_DSI_PHY_PWR_LDO_VOLTAGE_MV,
        };
        ESP_ERROR_CHECK(esp_ldo_acquire_channel(&ldo_mipi_phy_config, &ldo_mipi_phy));

        ESP_LOGI(TAG, "Turn on LCD backlight REFERENCE");
        gpio_config_t bk_gpio_config = {
            .pin_bit_mask = 1ULL << DISPLAY_BLACKLIGHT_REFERENCE_PIN,     // PWM BL
            .mode = GPIO_MODE_OUTPUT,
        };
        ESP_ERROR_CHECK(gpio_config(&bk_gpio_config));
        ESP_ERROR_CHECK(gpio_set_level(DISPLAY_BLACKLIGHT_REFERENCE_PIN, 1));  // BL ON

        
        ESP_LOGI(TAG, "Initialize MIPI DSI bus");
        esp_lcd_dsi_bus_config_t bus_config = ST7701_PANEL_BUS_DSI_2CH_CONFIG();
        ESP_ERROR_CHECK(esp_lcd_new_dsi_bus(&bus_config, &mipi_dsi_bus));

        ESP_LOGI(TAG, "Install panel IO");
        esp_lcd_dbi_io_config_t dbi_config = ST7701_PANEL_IO_DBI_CONFIG();
        ESP_ERROR_CHECK(esp_lcd_new_panel_io_dbi(mipi_dsi_bus, &dbi_config, &mipi_dbi_io));

        ESP_LOGI(TAG, "Install LCD driver of st7701");

        esp_lcd_dpi_panel_config_t dpi_config= {
            .virtual_channel = 0,
            .dpi_clk_src = MIPI_DSI_DPI_CLK_SRC_DEFAULT,
            .dpi_clock_freq_mhz = 15,
            .pixel_format = TEST_MIPI_DPI_PX_FORMAT,
            .num_fbs = 1,
            .video_timing = {
                .h_size = 480,
                .v_size = 480,
                .hsync_pulse_width = 10,
                .hsync_back_porch = 10,
                .hsync_front_porch = 20,
                .vsync_pulse_width = 10,
                .vsync_back_porch = 10,
                .vsync_front_porch = 10,
            },
            .flags = {
                .use_dma2d = true,
            }
        };

        st7701_vendor_config_t vendor_config = {
            .init_cmds = lcdst7701_init_cmds,
            .init_cmds_size = sizeof(lcdst7701_init_cmds) / sizeof(lcdst7701_init_cmds[0]),
            .mipi_config = {
                .dsi_bus = mipi_dsi_bus,
                .dpi_config = &dpi_config,
            },
            .flags = {
                .use_mipi_interface = 1,
            },
        };
        const esp_lcd_panel_dev_config_t panel_config = {
            .reset_gpio_num = PIN_NUM_LCD_RST,
            .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
            .bits_per_pixel = TEST_LCD_BIT_PER_PIXEL,
            .vendor_config = &vendor_config,
        };

        ESP_ERROR_CHECK(esp_lcd_new_panel_st7701(mipi_dbi_io, &panel_config, &disp_panel));
        ESP_ERROR_CHECK(esp_lcd_panel_reset(disp_panel));
        ESP_ERROR_CHECK(esp_lcd_panel_init(disp_panel));
        ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(disp_panel, true));

        // display_ = new NoDisplay();
        display_ = new JPetLcdDisplay(disp_panel);
    }
    void InitializeSDMMC(){
        esp_err_t ret;

        // Options for mounting the filesystem.
        // If format_if_mount_failed is set to true, SD card will be partitioned and
        // formatted in case when mounting fails.
        esp_vfs_fat_sdmmc_mount_config_t mount_config = {
            .format_if_mount_failed = false,
            .max_files = 5,
            .allocation_unit_size = 16 * 1024
        };
        sdmmc_card_t *card;
        const char mount_point[] = "/sdcard";
        ESP_LOGI(TAG, "Initializing SD card");
        // Use settings defined above to initialize SD card and mount FAT filesystem.
        // Note: esp_vfs_fat_sdmmc/sdspi_mount is all-in-one convenience functions.
        // Please check its source code and implement error recovery when developing
        // production applications.

        ESP_LOGI(TAG, "Using SDMMC peripheral");

    // By default, SD card frequency is initialized to SDMMC_FREQ_DEFAULT (20MHz)
    // For setting a specific frequency, use host.max_freq_khz (range 400kHz - 40MHz for SDMMC)
    // Example: for fixed frequency of 10MHz, use host.max_freq_khz = 10000;
        sdmmc_host_t host = SDMMC_HOST_DEFAULT();
        host.slot = SDMMC_HOST_SLOT_0; // Use slot 0 for SDMMC
        host.max_freq_khz = 40000;

        sd_pwr_ctrl_ldo_config_t ldo_config = {
            .ldo_chan_id = 4, // Use LDO channel 4 for SDMMC
        };
        sd_pwr_ctrl_handle_t pwr_ctrl_handle = NULL;

        ret = sd_pwr_ctrl_new_on_chip_ldo(&ldo_config, &pwr_ctrl_handle);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to create a new on-chip LDO power control driver");
            return;
        }
        host.pwr_ctrl_handle = pwr_ctrl_handle;

        // This initializes the slot without card detect (CD) and write protect (WP) signals.
        // Modify slot_config.gpio_cd and slot_config.gpio_wp if your board has these signals.
        sdmmc_slot_config_t slot_config = SDMMC_SLOT_CONFIG_DEFAULT();

        // Set bus width to use:
        slot_config.width = 4;
        // On chips where the GPIOs used for SD card can be configured, set them in
        // the slot_config structure:
        slot_config.clk = GPIO_NUM_43;
        slot_config.cmd = GPIO_NUM_44;
        slot_config.d0 = GPIO_NUM_39;
        slot_config.d1 = GPIO_NUM_40;
        slot_config.d2 = GPIO_NUM_41;
        slot_config.d3 = GPIO_NUM_42;

        // Enable internal pullups on enabled pins. The internal pullups
        // are insufficient however, please make sure 10k external pullups are
        // connected on the bus. This is for debug / example purpose only.
        // slot_config.flags |= SDMMC_SLOT_FLAG_INTERNAL_PULLUP;

        ESP_LOGI(TAG, "Mounting filesystem");
        ret = esp_vfs_fat_sdmmc_mount(mount_point, &host, &slot_config, &mount_config, &card);

        if (ret != ESP_OK) {
            if (ret == ESP_FAIL) {
                ESP_LOGE(TAG, "Failed to mount filesystem. "
                        "If you want the card to be formatted, set the EXAMPLE_FORMAT_IF_MOUNT_FAILED menuconfig option.");
            } else {
                ESP_LOGE(TAG, "Failed to initialize the card (%s). "
                        "Make sure SD card lines have pull-up resistors in place.", esp_err_to_name(ret));
            }
            return;
        }
        ESP_LOGI(TAG, "Filesystem mounted");

        // Card has been initialized, print its properties
        sdmmc_card_print_info(stdout, card);
    }
    void InitializeButtons() {
        boot_button_.OnClick([this]() {
            auto& app = Application::GetInstance();
            if (app.GetDeviceState() == kDeviceStateStarting && !WifiStation::GetInstance().IsConnected()) {
                // ESP_LOGI(TAG, "debug2");
                ResetWifiConfiguration();
            }
            // ESP_LOGI(TAG, "debug1");
            app.ToggleChatState(); });

        volume_up_button_.OnClick([this]() {
            auto codec = GetAudioCodec();
            auto volume = codec->output_volume() + 10;
            if (volume > 100) {
                volume = 100;
            }
            codec->SetOutputVolume(volume);
        });

        volume_up_button_.OnLongPress([this]() {
            GetAudioCodec()->SetOutputVolume(100);
        });

        volume_down_button_.OnClick([this]() {
            auto codec = GetAudioCodec();
            auto volume = codec->output_volume() - 10;
            if (volume < 0) {
                volume = 0;
            }
            codec->SetOutputVolume(volume);
        });

        volume_down_button_.OnLongPress([this]() {
            GetAudioCodec()->SetOutputVolume(0);
        });
    }

    // 物联网初始化，添加对 AI 可见设备
    // void InitializeTools()
    // {
    //     auto& mcp_server = McpServer::GetInstance();
        
    //     // 基础动作控制
    //     mcp_server.AddTool("self.dog.basic_control", "机器人的基础动作。机器人可以做以下基础动作：\n"
    //         "forward: 向前移动\nbackward: 向后移动\nturn_left: 向左转\nturn_right: 向右转\nstop: 立即停止当前动作", 
    //         PropertyList({
    //             Property("action", kPropertyTypeString),
    //         }), [this](const PropertyList& properties) -> ReturnValue {
    //             const std::string& action = properties["action"].value<std::string>();
    //             if (action == "forward") {
    //                 ESP_LOGI(TAG, "Dog action: forward");
    //             } else if (action == "backward") {
    //                 ESP_LOGI(TAG, "Dog action: backward");
    //             } else if (action == "turn_left") {
    //                 ESP_LOGI(TAG, "Dog action: turn_left");
    //             } else if (action == "turn_right") {
    //                 ESP_LOGI(TAG, "Dog action: turn_right");
    //             } else if (action == "stop") {
    //                 ESP_LOGI(TAG, "Dog action: stop");
    //             } else {
    //                 ESP_LOGE(TAG, "Unknown dog action: %s", action.c_str());
    //                 return false;
    //             } 
    //             return true;
    //         });
    //     // SD 卡播放音乐
    //     mcp_server.AddTool("self.music.play", "播放音乐，主要是SD卡里面的音乐，说出音乐名会在SD卡里面搜索并播放", PropertyList({
    //         Property("file", kPropertyTypeString),
    //     }), [this](const PropertyList& properties) -> ReturnValue {
    //         const std::string& file = properties["file"].value<std::string>();
    //         ESP_LOGI(TAG, "Playing music file: %s", file.c_str());
    //         JPetSDMusicPlayer* player = GetSDMusicPlayer();
    //         if (player) {
    //             player->SendMusicCmd(file);
    //             return true;
    //         }
    //         return false;
    //     });

    //     // 灯光控制
    //     mcp_server.AddTool("self.light.get_power", "获取灯是否打开", PropertyList(), [this](const PropertyList& properties) -> ReturnValue {
    //         return led_on_;
    //     });

    //     mcp_server.AddTool("self.light.turn_on", "打开灯", PropertyList(), [this](const PropertyList& properties) -> ReturnValue {
    //         led_on_ = true;
    //         ESP_LOGI(TAG, "Light turned on");
    //         return true;
    //     });

    //     mcp_server.AddTool("self.light.turn_off", "关闭灯", PropertyList(), [this](const PropertyList& properties) -> ReturnValue {
    //         led_on_ = false;
    //         ESP_LOGI(TAG, "Light turned off");
    //         return true;
    //     });
    // }

public:
    JPet() :
        boot_button_(BOOT_BUTTON_GPIO),
        volume_up_button_(VOLUME_UP_BUTTON_GPIO),
        volume_down_button_(VOLUME_DOWN_BUTTON_GPIO) {
        InitializeCodecI2c();
        InitializeSDMMC();
        InitializeLCDST7701();
        InitializeButtons();
        GetBacklight()->RestoreBrightness();
        // InitializeTools();
        // while(1){
        //     vTaskDelay(pdMS_TO_TICKS(1000));
        // }
    }

    virtual AudioCodec* GetAudioCodec() override {
        static JPetAudioCodec audio_codec(
            i2c_bus_, 
            AUDIO_INPUT_SAMPLE_RATE, 
            AUDIO_OUTPUT_SAMPLE_RATE,
            AUDIO_I2S_GPIO_MCLK, 
            AUDIO_I2S_GPIO_BCLK, 
            AUDIO_I2S_GPIO_WS, 
            AUDIO_I2S_GPIO_DOUT, 
            AUDIO_I2S_GPIO_DIN,
            AUDIO_CODEC_PA_PIN, 
            AUDIO_CODEC_ES8388_ADDR, 
            AUDIO_CODEC_ES7210_ADDR, 
            AUDIO_INPUT_REFERENCE);
        return &audio_codec;
    }

    virtual Display *GetDisplay() override {
        return display_;
    }

    virtual Backlight* GetBacklight() override {
        static PwmBacklight backlight(DISPLAY_BLACKLIGHT_REFERENCE_PIN, DISPLAY_BACKLIGHT_OUTPUT_INVERT);
        return &backlight;
    }

    virtual Led* GetLed() override {
        static GpioLed led(USER_LED_PIN, 1, LEDC_TIMER_1, LEDC_CHANNEL_1);
        return &led;
    }
};

DECLARE_BOARD(JPet);
