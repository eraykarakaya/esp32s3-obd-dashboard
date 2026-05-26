#if 1
#ifndef LV_CONF_H
#define LV_CONF_H
#include <stdint.h>

#define LV_COLOR_DEPTH          16
#define LV_COLOR_16_SWAP         0
#define LV_MEM_CUSTOM            0
#define LV_MEM_SIZE    (48U * 1024U)
#define LV_MEM_ADR               0
#define LV_DISP_DEF_REFR_PERIOD 33
#define LV_INDEV_DEF_READ_PERIOD 50
#define LV_DPI_DEF             130
#define LV_DRAW_COMPLEX          1
#define LV_SHADOW_CACHE_SIZE     0
#define LV_CIRCLE_CACHE_SIZE     0

#define LV_USE_GPU_STM32_DMA2D   0
#define LV_USE_GPU_NXP_PXP       0
#define LV_USE_GPU_NXP_VG_LITE   0
#define LV_USE_GPU_SDL           0

#define LV_USE_LOG               0
#define LV_USE_ASSERT_NULL       0
#define LV_USE_ASSERT_MALLOC     0
#define LV_USE_ASSERT_STYLE      0
#define LV_USE_ASSERT_MEM_INTEGRITY 0
#define LV_USE_ASSERT_OBJ        0

// Fontlar — sadece kullanılanlar açık
#define LV_FONT_MONTSERRAT_8     0
#define LV_FONT_MONTSERRAT_10    0
#define LV_FONT_MONTSERRAT_12    1
#define LV_FONT_MONTSERRAT_14    1
#define LV_FONT_MONTSERRAT_16    0
#define LV_FONT_MONTSERRAT_18    0
#define LV_FONT_MONTSERRAT_20    0
#define LV_FONT_MONTSERRAT_22    0
#define LV_FONT_MONTSERRAT_24    0
#define LV_FONT_MONTSERRAT_26    0
#define LV_FONT_MONTSERRAT_28    0
#define LV_FONT_MONTSERRAT_30    0
#define LV_FONT_MONTSERRAT_32    1
#define LV_FONT_MONTSERRAT_34    0
#define LV_FONT_MONTSERRAT_36    0
#define LV_FONT_MONTSERRAT_38    0
#define LV_FONT_MONTSERRAT_40    0
#define LV_FONT_MONTSERRAT_42    0
#define LV_FONT_MONTSERRAT_44    0
#define LV_FONT_MONTSERRAT_46    0
#define LV_FONT_MONTSERRAT_48    0

#define LV_FONT_DEFAULT &lv_font_montserrat_14

// Widget'lar
#define LV_USE_ARC       1
#define LV_USE_BAR       1
#define LV_USE_BTN       1
#define LV_USE_LABEL     1
#define LV_USE_LINE      1
#define LV_USE_METER     1
#define LV_USE_ANIM      1
#define LV_USE_TABVIEW   0

#define LV_USE_THEME_DEFAULT  1
#define LV_THEME_DEFAULT_DARK 1
#define LV_SPRINTF_CUSTOM     0
#define LV_USE_USER_DATA      1

#endif
#endif
