#ifndef stream_observer_include_h
#define stream_observer_include_h

#include <list>
#include <vector>
#include <util/stream_type.h>
#include <mutex>
#include <thread>

namespace ipc_mini{namespace util{

    class stream_obj
    {
        public:
            stream_obj(std::string name,int32_t chn,int32_t stream_id)
                :m_name(name),m_chn(chn),m_stream_id(stream_id)
            {
            }

            virtual ~stream_obj()
            {
            }

            std::string name()
            {
                return m_name;
            }

            int32_t chn()
            {
                return m_chn;
            }

            int32_t stream_id()
            {
                return m_stream_id;
            }

        protected:
            std::string m_name;
            int32_t m_chn;
            int32_t m_stream_id;
    };
    typedef std::shared_ptr<stream_obj> stream_obj_ptr;

    class stream_observer
    {
        public:
            virtual void on_stream_come(stream_obj_ptr sob,util::stream_head* head, const char* buf, int32_t len) = 0;
            virtual void on_stream_error(stream_obj_ptr sob,int32_t errno) = 0;
    };

    typedef std::shared_ptr<stream_observer> stream_observer_ptr;

    class stream_post
    {
        public:
            stream_post()
            {

            }

            ~stream_post()
            {

            }

            void register_stream_observer(stream_observer_ptr observer)
            {
                std::unique_lock<std::mutex> lock(m_stream_observers_mu);

                std::list<stream_observer_ptr>::iterator it;
                for (it = m_stream_observers.begin(); it != m_stream_observers.end(); it++)
                {
                    if (*it == observer)
                    {                
                        return;
                    }
                }

                m_stream_observers.push_back(observer);
            }

            void unregister_stream_observer(stream_observer_ptr observer)
            {
                std::unique_lock<std::mutex> lock(m_stream_observers_mu);
                m_stream_observers.remove(observer);
            }

            void clear_observers()
            {
                std::unique_lock<std::mutex> lock(m_stream_observers_mu);
                m_stream_observers.clear();
            }

            int32_t get_observer_size()
            {
                std::unique_lock<std::mutex> lock(m_stream_observers_mu);
                return m_stream_observers.size();
            }

            void  get_observers(std::vector<stream_observer_ptr>& observers)
            {
                observers.clear();

                std::unique_lock<std::mutex> lock(m_stream_observers_mu);
                std::list<stream_observer_ptr>::iterator it;
                for (it = m_stream_observers.begin(); it != m_stream_observers.end(); it++)
                {
                    observers.push_back(*it);
                }
            }

        protected:
            void post_stream_to_observer(stream_obj_ptr sobj,stream_head* head, const char* buf, int32_t len)
            {
                std::unique_lock<std::mutex> lock(m_stream_observers_mu);
                std::list<stream_observer_ptr>::iterator it;
                for (it = m_stream_observers.begin(); it != m_stream_observers.end(); it++)
                {
                    (*it)->on_stream_come(sobj,head, buf, len);                    
                }
            }    

        protected:
            std::list<stream_observer_ptr> m_stream_observers;
            std::mutex  m_stream_observers_mu;
    };

}}//namespace

#endif
