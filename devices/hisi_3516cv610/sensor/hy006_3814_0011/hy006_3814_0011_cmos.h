/*
  Copyright (c), 2001-2025, Shenshu Tech. Co., Ltd.
 */

#ifndef HY006_3814_0011_CMOS_H
#define HY006_3814_0011_CMOS_H

#include "ot_common_isp.h"
#include "ot_sns_ctrl.h"
#include "securec.h"

#ifdef __cplusplus
#if __cplusplus
extern "C" {
#endif
#endif

/****************************************************************************
 * basic info config                                                        *
 ****************************************************************************/
/* sensor ID & Resolution */
#define HY006_3814_0011_ID             4336
#define HY006_3814_0011_WIDTH          2560
#define HY006_3814_0011_HEIGHT         1440

/* sensor Register Address */
#define HY006_3814_0011_EXPO_H_ADDR          0x3E00
#define HY006_3814_0011_EXPO_M_ADDR          0x3E01
#define HY006_3814_0011_EXPO_L_ADDR          0x3E02
#define HY006_3814_0011_AGAIN_L_ADDR         0x3E09
#define HY006_3814_0011_DGAIN_H_ADDR         0x3E06
#define HY006_3814_0011_DGAIN_L_ADDR         0x3E07
#define HY006_3814_0011_VMAX_H_ADDR          0x320E
#define HY006_3814_0011_VMAX_L_ADDR          0x320F

/****************************************************************************
 * i2c bus config                                                           *
 ****************************************************************************/
/* sensor I2C bus config */
#define HY006_3814_0011_I2C_ADDR    0x60
#define HY006_3814_0011_ADDR_BYTE   2
#define HY006_3814_0011_DATA_BYTE   1

/* common I2C bus config */
#define I2C_DEV_FILE_NUM    16
#define I2C_BUF_NUM         8

/****************************************************************************
 * sensor lines configs                                                     *
 ****************************************************************************/
/* increase line */
#define HY006_3814_0011_INCREASE_LINES 0  /* make real fps less than stand fps because NVR require */
/* linear mode */
#define HY006_3814_0011_FULL_LINES_MAX_LINEAR 0x4650 /* calc by std & min fps, 0x4650 = 18000 = Min:2.5fps */
#define HY006_3814_0011_FULL_LINES_MAX_2M_LINEAR 0x34bc /* calc by std & min fps, 0x34bc = 13500 = Min:2.5fps */
#define HY006_3814_0011_VMAX_VAL_LINEAR 1500         /* linear init sequence */
#define HY006_3814_0011_VMAX_VAL_2M_LINEAR 1125
#define HY006_3814_0011_VMAX_LINEAR     (HY006_3814_0011_VMAX_VAL_LINEAR + HY006_3814_0011_INCREASE_LINES)
#define HY006_3814_0011_VMAX_2M_LINEAR  (HY006_3814_0011_VMAX_VAL_2M_LINEAR + HY006_3814_0011_INCREASE_LINES)
#define HY006_3814_0011_FPS_MAX_LINEAR 30
#define HY006_3814_0011_FPS_MIN_LINEAR 2.5
#define HY006_3814_0011_WIDTH_LINEAR 2560
#define HY006_3814_0011_HEIGHT_LINEAR 1440
#define HY006_3814_0011_WIDTH_2M_LINEAR 1920
#define HY006_3814_0011_HEIGHT_2M_LINEAR 1080
#define HY006_3814_0011_MODE_LINEAR 0

/****************************************************************************
 * sensor ae configs                                                        *
 ****************************************************************************/
#define HY006_3814_0011_AGAIN_MIN    1024
#define HY006_3814_0011_AGAIN_MAX    32768

#define HY006_3814_0011_DGAIN_MIN    1024
#define HY006_3814_0011_DGAIN_MAX    16128

#define ISP_DGAIN_SHIFT           8
#define ISP_DGAIN_TARGET_MIN      1
#define ISP_DGAIN_TARGET_MAX      32
#define ISP_DGAIN_TARGET_WDR_MIN  1
#define ISP_DGAIN_TARGET_WDR_MAX  4
#define INT_TIME_ACCURACY      1
#define AGAIN_ACCURACY         1
#define DGAIN_ACCURACY         1

#define FL_OFFSET_LINEAR       10
/****************************************************************************
 * sensor awb calibrate configs                                             *
 ****************************************************************************/
/* awb static param for Fuji Lens New IR_Cut */
#define CALIBRATE_STATIC_TEMP       5000
#define CALIBRATE_STATIC_WB_R_GAIN  409
#define CALIBRATE_STATIC_WB_GR_GAIN 256
#define CALIBRATE_STATIC_WB_GB_GAIN 256
#define CALIBRATE_STATIC_WB_B_GAIN  452

/* Calibration results for Auto WB Planck */
#define CALIBRATE_AWB_P1 (36)
#define CALIBRATE_AWB_P2 220
#define CALIBRATE_AWB_Q1 0
#define CALIBRATE_AWB_A1 218409
#define CALIBRATE_AWB_B1 128
#define CALIBRATE_AWB_C1 (-167686)

/* Rgain and Bgain of the golden sample */
#define GOLDEN_RGAIN                                  0
#define GOLDEN_BGAIN                                  0

/****************************************************************************
 * sensor other configs                                                     *
 ****************************************************************************/
#define STANDARD_FPS              30
#define FLICKER_FREQ            (50 * 256)  /* light flicker freq: 50Hz, accuracy: 256 */
#define INIT_EXP_DEFAULT_LINEAR   148859
#define MAX_INT_TIME_TARGET       65535
#define AE_COMENSATION_DEFAULT    0x40
/* Black level */
#define BLACK_LEVEL_DEFAULT            0x410
/* DNG */
#define DNG_RAW_FORMAT_BIT_LINEAR         10 /* raw 10 bit */
#define DNG_RAW_FORMAT_WHITE_LEVEL_LINEAR 1023 /* 2^10 - 1 */

/****************************************************************************
 * assist function macros                                                   *
 ****************************************************************************/
#define high_8bits(x)   (((x) & 0xff00) >> 8)
#define low_8bits(x)    ((x) & 0x00ff)
#define lower_4bits(x)  (((x) & 0x000F) << 4)
#define higher_8bits(x) (((x) & 0x0FF0) >> 4)
#define higher_4bits(x) (((x) & 0xF000) >> 12)
#ifndef clip3
#define clip3(x, min, max) ((x) < (min) ? (min) : ((x) > (max) ? (max) : (x)))
#endif
#ifndef max
#define max(a, b) (((a) < (b)) ?  (b) : (a))
#endif
#ifndef min
#define min(a, b) (((a) > (b)) ?  (b) : (a))
#endif

/****************************************************************************
 * sensor data type defines                                                 *
 ****************************************************************************/
/* define your sensor modes */
typedef enum {
    HY006_3814_0011_2M_30FPS_10BIT_LINEAR_MODE = 0,
    HY006_3814_0011_4M_30FPS_10BIT_LINEAR_MODE = 1,
    HY006_3814_0011_MODE_MAX
} hy006_3814_0011_res_mode;

typedef enum {
    EXPO_L_IDX = 0,
    EXPO_M_IDX,
    EXPO_H_IDX,
    AGAIN_L_IDX,
    DGAIN_L_IDX,
    DGAIN_H_IDX,
    VMAX_L_IDX,
    VMAX_H_IDX,
    REG_MAX_IDX
} hy006_3814_0011_linear_reg_index;

ot_isp_sns_obj *hy006_3814_0011_get_obj(td_void);

#ifdef __cplusplus
#if __cplusplus
}
#endif
#endif /* End of #ifdef __cplusplus */
#endif /* HY006_3814_0011_CMOS_H */
