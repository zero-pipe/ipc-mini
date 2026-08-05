/*
  Copyright (c), 2001-2025, Shenshu Tech. Co., Ltd.
 */

#include "sensor_common.h"
#include "hy006_3814_0011_cfg.h"
#include "hy006_3814_0011_cmos.h"

static void hy006_3814_0011_default_reg_init(cis_info *cis)
{
    td_u32 i;
    td_s32 ret = TD_SUCCESS;
    ot_isp_sns_state *past_sensor = TD_NULL;

    past_sensor = cis->sns_state;
    for (i = 0; i < past_sensor->regs_info[0].reg_num; i++) {
        ret += cis_write_reg(&cis->i2c,
            past_sensor->regs_info[0].i2c_data[i].reg_addr,
            past_sensor->regs_info[0].i2c_data[i].data);
    }

    if (ret != TD_SUCCESS) {
        isp_err_trace("write register failed!\n");
    }
    return;
}

static td_s32 hy006_3814_0011_reg_init(cis_info *cis, cis_reg_cfg *cfg, td_u32 len)
{
    td_u32 i;

    sns_check_return(cis_write_reg(&cis->i2c, 0x0103, 0x01));
    cis_delay_ms(1); /* 1ms */

    for (i = 0; i < len; i++) {
        sns_check_return(cis_write_reg(&cis->i2c, cfg->addr, cfg->data));
        cfg++;
    }

    hy006_3814_0011_default_reg_init(cis);

    sns_check_return(cis_write_reg(&cis->i2c, 0x0100, 0x01));

    return TD_SUCCESS;
}

td_s32 hy006_3814_0011_linear_4m30_10bit_init(cis_info *cis)
{
    td_s32 ret;
    td_u32 len;
    cis_reg_cfg *cfg = TD_NULL;
    ot_isp_sns_state *sns_state = TD_NULL;

    sns_check_pointer_return(cis);
    sns_state = cis->sns_state;
    sns_check_pointer_return(sns_state);

    if (sns_state->img_mode == HY006_3814_0011_4M_30FPS_10BIT_LINEAR_MODE) {
        cfg = hy006_3814_0011_linear_4m30_10bit;
        len = (td_u32)(sizeof(hy006_3814_0011_linear_4m30_10bit) / sizeof(hy006_3814_0011_linear_4m30_10bit[0]));
    } else if (sns_state->img_mode == HY006_3814_0011_2M_30FPS_10BIT_LINEAR_MODE) {
        cfg = hy006_3814_0011_linear_2m30_10bit;
        len = (td_u32)(sizeof(hy006_3814_0011_linear_2m30_10bit) / sizeof(hy006_3814_0011_linear_2m30_10bit[0]));
    } else {
        return TD_FAILURE;
    }
    ret = hy006_3814_0011_reg_init(cis, cfg, len);
    if (ret != TD_SUCCESS) {
        isp_err_trace("hy006_3814_0011_reg_init failed!\n");
        return ret;
    }

    printf("===================================================================================\n");
    if (sns_state->img_mode == HY006_3814_0011_4M_30FPS_10BIT_LINEAR_MODE) {
        printf("vi_pipe:%d,== HY006_3814_0011_MIPI_27Minput_2lane_10bit_630Mbps_2560x1440_30fps Init OK! ==\n",
            cis->pipe);
    } else {
        printf("vi_pipe:%d,== HY006_3814_0011_MIPI_27Minput_2lane_10bit_472.5Mbps_1920x1080_30fps Init OK! ==\n",
            cis->pipe);
    }
    printf("===================================================================================\n");

    return TD_SUCCESS;
}

td_s32 hy006_3814_0011_get_standby_cfg(ot_isp_sns_regs_info *standby_cfg)
{
    td_u32 i;
    standby_cfg->reg_num = (td_u32)(sizeof(hy006_3814_0011_standby_cfg) / sizeof(hy006_3814_0011_standby_cfg[0]));

    for (i = 0; i < standby_cfg->reg_num; i++) {
        standby_cfg->i2c_data[i].dev_addr = HY006_3814_0011_I2C_ADDR;
        standby_cfg->i2c_data[i].addr_byte_num = HY006_3814_0011_ADDR_BYTE;
        standby_cfg->i2c_data[i].data_byte_num = HY006_3814_0011_DATA_BYTE;
        standby_cfg->i2c_data[i].reg_addr = hy006_3814_0011_standby_cfg[i].addr;
        standby_cfg->i2c_data[i].data = hy006_3814_0011_standby_cfg[i].data;
    }

    return TD_SUCCESS;
}

