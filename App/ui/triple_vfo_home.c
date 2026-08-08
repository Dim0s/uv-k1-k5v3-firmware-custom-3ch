#include "triple_vfo_home.h"

#include <string.h>

#include "app/dtmf.h"
#include "bitmaps.h"
#include "dcs.h"
#include "driver/bk4819.h"
#include "driver/st7565.h"
#include "external/printf/printf.h"
#include "font.h"
#include "frequencies.h"
#include "functions.h"
#include "helper/battery.h"
#include "misc.h"
#include "radio.h"
#include "settings.h"
#include "ui/battery.h"
#include "ui/helper.h"
#include "triple_vfo_smeter_xbm.h"
#include "triple_vfo_u8g2.h"

#define TV_METER_Y         3u   /* S 表刻度/方块（状态栏内） */
#define TV_METER_TICK_SRC  6u   /* XBM 中竖线起始行（跳过上方 1/3/5/7/9 数字） */
#define TV_METER_TICK_H    3u
#define TV_METER_WIDTH     DUALVFO_SMETER_XBM_WIDTH
#define TV_DBM_X           1u   /* 左侧固定 dBm 槽 */
#define TV_LABEL_GAP       2u   /* 文字槽与刻度间距 */
#define TV_RXTX_GAP        6u   /* Sn 槽后到 RX/TX 的间距 */
#define TV_LOCK_GAP        1u
/* 主缓冲 56px：分隔线占 fb y=0，信道自 y=2 起（16px + 3px 间隔 → 2/21/40） */
#define TV_SEP_Y           0u
#define TV_CH_BASE_Y       2u
#define TV_CH_H            16u
#define TV_CH_GAP          3u
#define TV_CH_STRIDE       (TV_CH_H + TV_CH_GAP)
#define TV_CH_TOP(v)       ((uint8_t)(TV_CH_BASE_Y + (v) * TV_CH_STRIDE))
#define TV_PARAM_Y0(v)     ((uint8_t)(TV_CH_TOP(v) + 1u))
#define TV_PARAM_Y1(v)     ((uint8_t)(TV_CH_TOP(v) + 8u))
#define TV_GAP_PX          2u
#define TV_FB_H            ((uint8_t)(FRAME_LINES * 8u))

/* S 表画在状态栏（8px）；超出部分丢弃，避免盖住下方信道行 */
static void draw_pixel_status(uint8_t x, uint8_t y, bool black)
{
    if (x >= LCD_WIDTH || y >= 8u)
        return;
    UI_DrawPixelBuffer(&gStatusLine, x, y, black);
}

/* 只画刻度竖线（XBM 第 6..8 行），不上数字 */
static void draw_smeter_ticks(uint8_t x0, uint8_t y0)
{
    const unsigned stride = (DUALVFO_SMETER_XBM_WIDTH + 7u) / 8u;

    for (uint8_t i = 0; i < TV_METER_TICK_H; i++) {
        const uint8_t src_py = (uint8_t)(TV_METER_TICK_SRC + i);
        for (uint8_t px = 0; px < DUALVFO_SMETER_XBM_WIDTH; px++) {
            const unsigned bi = (unsigned)src_py * stride + (unsigned)(px / 8u);
            const uint8_t  b  = dualvfo_smeter_xbm_bits[bi];
            if (b & (uint8_t)(1u << (px % 8u)))
                draw_pixel_status((uint8_t)(x0 + px), (uint8_t)(y0 + i), true);
        }
    }
}

/* 3×3 方块叠在刻度竖线带上，步进 4，最多 9 块 */
static void draw_smeter_boxes(uint8_t s_level, uint8_t x, uint8_t y)
{
    uint8_t currentX = x;
    const uint8_t boxes = (s_level > 9u) ? 9u : s_level;

    for (uint8_t i = 0; i < boxes; i++) {
        for (uint8_t yy = 0; yy < 3u; yy++) {
            for (uint8_t xx = 0; xx < 3u; xx++)
                draw_pixel_status((uint8_t)(currentX + xx), (uint8_t)(y + yy), true);
        }
        currentX = (uint8_t)(currentX + 4u);
    }
}

static uint8_t convert_rssi_to_s_level(int16_t rssi_dBm)
{
    static const int16_t thresholds[] = {
        -121, -115, -109, -103, -97, -91, -85, -79, -73, -67
    };
    for (uint8_t level = 0; level < ARRAY_SIZE(thresholds); level++) {
        if (rssi_dBm <= thresholds[level])
            return level;
    }
    return 10u;
}

static void draw_pixel_fb(uint8_t x, uint8_t y, bool black)
{
    if (x >= LCD_WIDTH || y >= TV_FB_H)
        return;
    UI_DrawPixelBuffer(gFrameBuffer, x, y, black);
}

static void invert_channel_row(uint8_t vfo)
{
    const uint8_t y0 = TV_CH_TOP(vfo);
    const uint8_t y1 = (uint8_t)(y0 + TV_CH_H - 1u);

    /* 反色信道上方加 1px 黑线 */
    if (y0 > 0u) {
        const uint8_t y_bar = (uint8_t)(y0 - 1u);
        for (uint8_t x = 0; x < LCD_WIDTH; x++)
            draw_pixel_fb(x, y_bar, true);
    }

    for (uint8_t y = y0; y <= y1 && y < TV_FB_H; y++) {
        for (uint8_t x = 0; x < LCD_WIDTH; x++) {
            const uint8_t pattern = (uint8_t)(1u << (y % 8u));
            gFrameBuffer[y / 8u][x] ^= pattern;
        }
    }
}

/* gFontSmall 右对齐串的左缘 X（含字符前 1px 空位） */
static uint8_t right_small_px_left(const char *text)
{
    const size_t len = strlen(text);
    const uint8_t char_w = (uint8_t)ARRAY_SIZE(gFontSmall[0]);
    const uint8_t char_spacing = (uint8_t)(char_w + 1u);
    const uint8_t width = (uint8_t)(len * char_spacing);
    return (width >= LCD_WIDTH) ? 0u : (uint8_t)(LCD_WIDTH - width);
}

/* gFontSmall 按像素 Y 右对齐绘制；返回名称左缘 X（含字符前 1px 空位） */
static uint8_t draw_right_small_px(const char *text, uint8_t y_top)
{
    const size_t len = strlen(text);
    const uint8_t char_w = (uint8_t)ARRAY_SIZE(gFontSmall[0]);
    const uint8_t char_spacing = (uint8_t)(char_w + 1u);
    uint8_t x = right_small_px_left(text);
    const uint8_t left = x;

    for (size_t i = 0; i < len; i++) {
        const char c = text[i];
        if (c > ' ' && c < 127) {
            const unsigned int index = (unsigned int)(c - ' ' - 1);
            const uint8_t *glyph = gFontSmall[index];
            const uint8_t gx = (uint8_t)(x + 1u); /* 与 UI_PrintStringBuffer 左空 1px 一致 */
            for (uint8_t col = 0; col < char_w; col++) {
                uint8_t bits = glyph[col];
                for (uint8_t row = 0; row < 8u; row++) {
                    if (bits & (uint8_t)(1u << row))
                        draw_pixel_fb((uint8_t)(gx + col), (uint8_t)(y_top + row), true);
                }
            }
        }
        x = (uint8_t)(x + char_spacing);
    }
    return left;
}

/* 第二行左侧：DTMF: + 数字；超宽时固定前缀，从左侧丢掉数字（滚动） */
static void draw_dtmf_live_param(uint8_t y, uint8_t x_right, const char *digits)
{
    static const char prefix[] = "DTMF:";
    const uint8_t x_left = 1u;
    const uint8_t gap = 2u;
    char buf[28];
    size_t dig_off = 0;
    size_t dig_len;
    uint8_t avail;

    if (digits == NULL)
        digits = "";
    dig_len = strlen(digits);

    if (x_right <= (uint8_t)(x_left + gap))
        return;
    avail = (uint8_t)(x_right - gap - x_left);

    while (dig_off < dig_len) {
        snprintf(buf, sizeof(buf), "%s%s", prefix, digits + dig_off);
        if (TripleVfoU8g2_GetSmallTextWidth(buf) <= avail)
            break;
        dig_off++;
    }
    snprintf(buf, sizeof(buf), "%s%s", prefix, digits + dig_off);
    TripleVfoU8g2_DrawSmallText(buf, x_left, y, true);
}

static const char *power_letter(uint8_t power)
{
    static const char *const names[] = {"U", "L1", "L2", "L3", "L4", "L5", "M", "H"};
    if (power >= ARRAY_SIZE(names))
        return "?";
    return names[power];
}

static void format_tx_tone(char *out, size_t out_sz, const VFO_Info_t *vfo)
{
    const FREQ_Config_t *pConfig = vfo->pTX;
    out[0] = '\0';
    switch (pConfig->CodeType) {
    case CODE_TYPE_CONTINUOUS_TONE:
        snprintf(out, out_sz, "%u.%u", CTCSS_Options[pConfig->Code] / 10u, CTCSS_Options[pConfig->Code] % 10u);
        break;
    case CODE_TYPE_DIGITAL:
        snprintf(out, out_sz, "%03oN", DCS_Options[pConfig->Code]);
        break;
    case CODE_TYPE_REVERSE_DIGITAL:
        snprintf(out, out_sz, "%03oI", DCS_Options[pConfig->Code]);
        break;
    default:
        break;
    }
}

static uint8_t draw_param_item(const char *text, uint8_t x, uint8_t y, bool black)
{
    if (text == NULL || text[0] == '\0')
        return x;
    TripleVfoU8g2_DrawSmallText(text, x, y, black);
    return (uint8_t)(x + TripleVfoU8g2_GetSmallTextWidth(text) + TV_GAP_PX);
}

static void draw_channel_row(uint8_t vfo)
{
    const VFO_Info_t *info = &gEeprom.VfoInfo[vfo];
    char String[22];
    char tone[12];
    char freq_str[16];
    const bool transmitting = (gCurrentFunction == FUNCTION_TRANSMIT && gEeprom.TX_VFO == vfo);
    const bool black = true;
    const bool show_dtmf = gSetting_live_DTMF_decoder &&
                           gDTMF_RX_live[0] != 0 &&
                           vfo == gHighlightVfo;

    /* 上行：信道号（反色徽章）+ 亚音；下行：调制 / 功率 / SQL（或 DTMF） */
    uint8_t x = 2u;
    const uint8_t y0 = TV_PARAM_Y0(vfo);
    const uint8_t y1 = TV_PARAM_Y1(vfo);

    snprintf(String, sizeof(String), "CH%u", (unsigned)(vfo + 1u));
    {
        const uint8_t ch_w = TripleVfoU8g2_GetSmallTextWidth(String);
        const uint8_t text_x = x;
        /* 黑底：左/右各多 1px 列，顶部多 1px 行 */
        const uint8_t box_x0 = (uint8_t)(text_x - 1u);
        const uint8_t box_x1 = (uint8_t)(text_x + ch_w); /* 右缘含右侧 1px 列 */
        const uint8_t box_y0 = (uint8_t)(y0 - 1u);
        const uint8_t box_y1 = (uint8_t)(y0 + 5u);
        for (uint8_t yy = box_y0; yy <= box_y1; yy++) {
            for (uint8_t xx = box_x0; xx <= box_x1; xx++)
                draw_pixel_fb(xx, yy, true);
        }
        TripleVfoU8g2_DrawSmallText(String, text_x, y0, false);
        x = (uint8_t)(box_x1 + 1u + TV_GAP_PX);
    }
    format_tx_tone(tone, sizeof(tone), info);
    if (tone[0] != '\0')
        draw_param_item(tone, x, y0, black);

    /* frequency 文案先算好：左侧 DTMF 右界要贴到频率前 */
    {
        uint32_t frequency = transmitting ? info->pTX->Frequency : info->pRX->Frequency;
        if (info->TX_OFFSET_FREQUENCY_DIRECTION == TX_OFFSET_FREQUENCY_DIRECTION_ADD)
            snprintf(freq_str, sizeof(freq_str), "+%03u.%05u", frequency / 100000u, frequency % 100000u);
        else if (info->TX_OFFSET_FREQUENCY_DIRECTION == TX_OFFSET_FREQUENCY_DIRECTION_SUB)
            snprintf(freq_str, sizeof(freq_str), "-%03u.%05u", frequency / 100000u, frequency % 100000u);
        else
            snprintf(freq_str, sizeof(freq_str), "%03u.%05u", frequency / 100000u, frequency % 100000u);
    }

    if (show_dtmf) {
        draw_dtmf_live_param(y1, right_small_px_left(freq_str), gDTMF_RX_live);
    } else {
        x = 1;
        x = draw_param_item(gModulationStr[info->Modulation], x, y1, black);
        x = draw_param_item(power_letter(info->OUTPUT_POWER), x, y1, black);
        snprintf(String, sizeof(String), "%u", (unsigned)gEeprom.SQUELCH_LEVEL);
        draw_param_item(String, x, y1, black);
    }

    /* name；禁止发射时在信道名位置显示原因 */
    if (VfoState[vfo] != VFO_STATE_NORMAL &&
        VfoState[vfo] < _VFO_STATE_LAST_ELEMENT &&
        VfoStateStr[VfoState[vfo]] != NULL &&
        VfoStateStr[VfoState[vfo]][0] != '\0') {
        snprintf(String, sizeof(String), "%s", VfoStateStr[VfoState[vfo]]);
    } else {
        SETTINGS_FetchChannelName(String, gEeprom.ScreenChannel[vfo]);
        if (String[0] == 0) {
            if (IS_MR_CHANNEL(gEeprom.ScreenChannel[vfo]))
                snprintf(String, sizeof(String), "CH-%04u", gEeprom.ScreenChannel[vfo] + 1u);
            else
                snprintf(String, sizeof(String), "VFO-%u", (unsigned)(vfo + 1u));
        }
    }
    String[10] = 0;
    {
        const uint8_t name_y = TV_CH_TOP(vfo);
        const uint8_t name_left = draw_right_small_px(String, name_y);

        /* 信道名前方：u8g2 小字显示当前记忆信道号，顶边与信道名对齐 */
        if (IS_MR_CHANNEL(gEeprom.ScreenChannel[vfo])) {
            char num[6];
            snprintf(num, sizeof(num), "%u", (unsigned)(gEeprom.ScreenChannel[vfo] + 1u));
            const uint8_t num_w = TripleVfoU8g2_GetSmallTextWidth(num);
            if (num_w + 2u < name_left)
                TripleVfoU8g2_DrawSmallText(num, (uint8_t)(name_left - 2u - num_w), name_y, true);
        }
    }

    draw_right_small_px(freq_str, (uint8_t)(TV_CH_TOP(vfo) + 8u));
}

static void draw_meter_row(void)
{
    char String[12];
    const bool tx = (gCurrentFunction == FUNCTION_TRANSMIT);
    /* 固定槽位：dBm(+000) | 刻度 | Sn | RX/TX | 锁 | …电池（缺省不左移） */
    const uint8_t dbm_x    = TV_DBM_X;
    const uint8_t dbm_slot = TripleVfoU8g2_GetSmallTextWidth("+000");
    const uint8_t scale_x  = (uint8_t)(dbm_x + dbm_slot + TV_LABEL_GAP);
    const uint8_t s_x      = (uint8_t)(scale_x + TV_METER_WIDTH + TV_LABEL_GAP);
    const uint8_t s_slot   = TripleVfoU8g2_GetSmallTextWidth("S9");
    const uint8_t rxtx_x   = (uint8_t)(s_x + s_slot + TV_RXTX_GAP);
    const uint8_t lock_x   = (uint8_t)(rxtx_x + TripleVfoU8g2_GetSmallTextWidth("TX") + TV_LOCK_GAP);

    UI_StatusClear();

    draw_smeter_ticks(scale_x, TV_METER_Y);

    if (tx) {
        uint8_t level = (uint8_t)((gFlashLightBlinkCounter >> 1) % 9u) + 1u;
        draw_smeter_boxes(level, scale_x, TV_METER_Y);
        TripleVfoU8g2_DrawSmallTextStatus("TX", rxtx_x, 1u, true);
    } else if (FUNCTION_IsRx()) {
        int16_t rssi_dBm = BK4819_GetRSSI_dBm();
        {
            const unsigned int b = gEeprom.VfoInfo[gEeprom.RX_VFO].Band;
            if (b < 7u)
                rssi_dBm += dBmCorrTable[b];
        }
        uint8_t s_level = convert_rssi_to_s_level(rssi_dBm);
        if (s_level < 1u)
            s_level = 1u;

        {
            int v = (int)rssi_dBm;
            const char sign = (v < 0) ? '-' : '+';
            if (v < 0)
                v = -v;
            if (v > 999)
                v = 999;
            snprintf(String, sizeof(String), "%c%03d", sign, v);
            TripleVfoU8g2_DrawSmallTextStatus(String, dbm_x, 1u, true);
        }

        draw_smeter_boxes((s_level >= 10u) ? 9u : s_level, scale_x, TV_METER_Y);

        snprintf(String, sizeof(String), "S%u", (s_level >= 10u) ? 9u : (unsigned)s_level);
        TripleVfoU8g2_DrawSmallTextStatus(String, s_x, 1u, true);

        TripleVfoU8g2_DrawSmallTextStatus("RX", rxtx_x, 1u, true);
    }

    if (gEeprom.KEY_LOCK && lock_x + sizeof(gFontKeyLock) <= LCD_WIDTH)
        memcpy(gStatusLine + lock_x, gFontKeyLock, sizeof(gFontKeyLock));

    /* battery icon at right of status line */
    {
        uint8_t bitmap[sizeof(BITMAP_BatteryLevel1)];
        UI_DrawBattery(bitmap, gBatteryDisplayLevel, gLowBatteryBlink);
        const uint8_t bat_x = (uint8_t)(LCD_WIDTH - sizeof(bitmap));
        memcpy(gStatusLine + bat_x, bitmap, sizeof(bitmap));

        if (gSetting_battery_text == 1) {
            snprintf(String, sizeof(String), "%u.%02u",
                     gBatteryVoltageAverage / 100u, gBatteryVoltageAverage % 100u);
            const uint8_t w = TripleVfoU8g2_GetSmallTextWidth(String);
            if (w + 1u < bat_x)
                TripleVfoU8g2_DrawSmallTextStatus(String, (uint8_t)(bat_x - w - 1u), 1u, true);
        } else if (gSetting_battery_text == 2) {
            snprintf(String, sizeof(String), "%u%%", BATTERY_VoltsToPercent(gBatteryVoltageAverage));
            const uint8_t w = TripleVfoU8g2_GetSmallTextWidth(String);
            if (w + 1u < bat_x)
                TripleVfoU8g2_DrawSmallTextStatus(String, (uint8_t)(bat_x - w - 1u), 1u, true);
        }
    }

    /* 顶栏底部分隔横线：画在主缓冲首行（相对原状态栏底再下移 1px） */
    for (uint8_t x = 0; x < LCD_WIDTH; x++)
        draw_pixel_fb(x, TV_SEP_Y, true);
}

void UI_DisplayTripleVfoHome(void)
{
    UI_DisplayClear();
    UI_StatusClear();

    draw_meter_row();

    for (uint8_t v = 0; v < NUM_VFOS; v++)
        draw_channel_row(v);

    if (gHighlightVfo < NUM_VFOS)
        invert_channel_row(gHighlightVfo);

    ST7565_BlitStatusLine();
    ST7565_BlitFullScreen();
}
