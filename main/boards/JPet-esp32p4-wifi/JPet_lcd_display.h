#ifndef __JPET_LCD_DISPLAY_H__
#define __JPET_LCD_DISPLAY_H__

#include "display.h"
#include "esp_lcd_types.h"
#include "esp_lcd_mipi_dsi.h"

bool JPet_lcd_ready_callback(esp_lcd_panel_handle_t panel, esp_lcd_dpi_panel_event_data_t *edata, void *user_ctx);
void JPet_lcd_flush();

class JPetLcdDisplay : public NoDisplay {
public:
    JPetLcdDisplay(esp_lcd_panel_handle_t panel);

    virtual void SetEmotion(const char* emotion) override;
    virtual void SetChatMessage(const char* role, const char* content);

};




#endif // __CUSTOM_LCD_DISPLAY_H__