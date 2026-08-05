#ifndef ZERO_IPC_HISI_3516CV610_DEV_LOG_H
#define ZERO_IPC_HISI_3516CV610_DEV_LOG_H

#include <ztk/util/log.h>

#define DEV_WRITE_LOG_ERROR(info, ...) ztk_error("[hisi-device] " info, ##__VA_ARGS__)
#define DEV_WRITE_LOG_DEBUG(info, ...) ztk_debug("[hisi-device] " info, ##__VA_ARGS__)
#define DEV_WRITE_LOG_INFO(info, ...)  ztk_info("[hisi-device] " info, ##__VA_ARGS__)
#define DEV_WRITE_LOG_WARN(info, ...)  ztk_warn("[hisi-device] " info, ##__VA_ARGS__)
#define DEV_WRITE_LOG_FATAL(info, ...) ztk_error("[hisi-device] FATAL: " info, ##__VA_ARGS__)

#endif
