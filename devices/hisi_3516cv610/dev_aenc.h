#ifndef dev_aenc_include_h
#define dev_aenc_include_h

#include "dev_std.h"
#include <stream_observer.h>

namespace hisilicon{namespace dev{


    class aenc;
    typedef std::shared_ptr<aenc> aenc_ptr;

    class aenc
    {
        public:
            aenc(ot_audio_dev ai_dev,ot_ai_chn ai_chn,ot_aenc_chn aenc_chn,bool is_mic);

        public:
            virtual ~aenc();

            bool start();
            void stop();

            uint32_t sample_rate();
            uint8_t bit_width();
            uint8_t chn_cnt();

            int32_t aenc_fd();
            bool get_stream(ot_audio_stream *stream, td_s32 msec);
            void release_stream(ot_audio_stream *stream);

            static bool get_instance(const char* name,bool is_mic,aenc_ptr& aenc);

        protected:
            bool m_is_start;
            ot_payload_type m_payload_type;
            ot_aio_attr m_aio_attr;
            ot_audio_dev m_ai_dev;
            ot_ai_chn m_ai_chn;
            ot_aenc_chn_attr m_aenc_attr;
            ot_aenc_chn m_aenc_chn;
            ot_acodec_fs m_i2s_fs_sel;
            bool m_is_mic;
    };
    
    class aenc_g711u
        :public aenc
    {
        public:
            aenc_g711u(bool is_mic);

        public:
            ~aenc_g711u();

        private:
            ot_aenc_attr_g711 m_aenc_g711;
    };

    class aenc_aac
        :public aenc
    {
        public:
            aenc_aac(bool is_mic);

        public:
            ~aenc_aac();

        private:
            ot_aenc_attr_aac m_aenc_aac;
    };

}}//namespace

#endif


