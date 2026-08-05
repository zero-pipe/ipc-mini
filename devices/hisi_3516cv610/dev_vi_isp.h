#ifndef dev_vi_isp_include_h
#define dev_vi_isp_include_h

#include "dev_vi.h"
#include "dev_sys.h"

//to support rgb raw sensor
namespace hisilicon{namespace dev{

    class vi_isp
        :public vi
    {
        public:
            vi_isp(int32_t w,
                    int32_t h,
                    int32_t src_fr,
                    int32_t vi_dev,
                    int32_t mipi_dev,
                    int32_t sns_clk_src,
                    int32_t wdr_mode,
                    ot_isp_sns_obj* sns_obj,
                    int32_t i2c_dev,
                    int32_t flag);

            virtual ~vi_isp();

            bool start() override;

            void stop() override;

            int32_t isp_w();

            int32_t isp_h();

            int32_t wdr_mode();

            bool get_isp_exposure_info(isp_exposure_t* val);

            ot_vi_pipe_attr& vi_pipe_attr();
            ot_vi_chn_attr vi_chn_attr();
            ot_isp_pub_attr& isp_pub_attr();
            ot_vpss_grp_attr vpss_grp_attr();
            ot_vpss_chn_attr& vpss_chn_attr();
            ot_isp_sns_obj* sns_obj();
            ot_frame_interrupt_attr& frame_interrupt_attr();

            static bool init_hs_mode(lane_divide_mode_t mode);

        protected:
            bool start_mipi();
            bool stop_mipi();
            bool reset_sns();
            void on_isp_proc();

            bool start_isp();
            void stop_isp();
            std::thread m_isp_thread;

        protected:
            int32_t m_mipi_dev;
            int32_t m_sns_clk_src;
            int32_t m_wdr_mode;
            ot_isp_sns_obj* m_sns_obj;
            int32_t m_i2c_dev;

            combo_dev_attr_t m_mipi_attr;
            ot_vi_dev_attr m_vi_dev_attr;
            ot_vi_wdr_fusion_grp_attr m_fusion_grp_attr;
            ot_vi_pipe_attr m_vi_pipe_attr;
            ot_vi_chn_attr m_vi_chn_attr;
            ot_isp_pub_attr m_isp_pub_attr;
            ot_vpss_grp_attr m_vpss_grp_attr;
            ot_vpss_chn_attr m_vpss_chn0_attr;
            ot_3dnr_attr m_nr_attr;

            ot_frame_interrupt_attr m_frame_interrupt_attr;
            bool m_ldc_enable;
            bool m_debreath_effect_enable;
            bool m_wrap_enable;
    };

}}//namespace

#endif
