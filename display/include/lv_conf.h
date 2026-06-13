#if 1   /* enable file content */
#ifndef LV_CONF_H
#define LV_CONF_H

#include <stdint.h>

/* Color depth: 1/8/16/32 */
#define LV_COLOR_DEPTH 16

/* DPI */
#define LV_DPI_DEF 200

/* Memory */
#define LV_MEM_CUSTOM 0
#define LV_MEM_SIZE   (128U * 1024U)   /* 128 kB */

/* HAL settings */
#define LV_DISP_DEF_REFR_PERIOD  16    /* 60 Hz */
#define LV_INDEV_DEF_READ_PERIOD 30

/* Fonts — include Montserrat for numbers */
#define LV_FONT_MONTSERRAT_12 1
#define LV_FONT_MONTSERRAT_16 1
#define LV_FONT_MONTSERRAT_24 1
#define LV_FONT_MONTSERRAT_36 1
#define LV_FONT_MONTSERRAT_48 1
#define LV_FONT_DEFAULT       &lv_font_montserrat_24

/* Enable needed widgets */
#define LV_USE_ARC    1
#define LV_USE_LABEL  1
#define LV_USE_METER  1
#define LV_USE_IMG    1
#define LV_USE_LINE   1
#define LV_USE_BTN    1
#define LV_USE_CONT   0

/* Animations */
#define LV_USE_ANIMATION 1

/* Log */
#define LV_USE_LOG      1
#define LV_LOG_LEVEL    LV_LOG_LEVEL_WARN
#define LV_LOG_PRINTF   1

/* Misc */
#define LV_USE_THEME_DEFAULT 1
#define LV_THEME_DEFAULT_DARK 1

#endif  /* LV_CONF_H */
#endif  /* enable file content */
