# Hi3516CV610 implementation vendored with this project.
set(ZIPCG_DEVICE_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/devices/hisi_3516cv610")

set(ZIPCG_DEVICE_CPP_SOURCES
    "${ZIPCG_DEVICE_ROOT}/dev_sys.cpp"
    "${ZIPCG_DEVICE_ROOT}/dev_vi.cpp"
    "${ZIPCG_DEVICE_ROOT}/dev_vi_isp.cpp"
    "${ZIPCG_DEVICE_ROOT}/dev_vi_sc4336p_liner.cpp"
    "${ZIPCG_DEVICE_ROOT}/dev_vi_hy006_3814_0011_liner.cpp"
    "${ZIPCG_DEVICE_ROOT}/dev_vi_sc431hai_liner.cpp"
    "${ZIPCG_DEVICE_ROOT}/dev_vi_gc8613_liner.cpp"
    "${ZIPCG_DEVICE_ROOT}/dev_vi_gc4023_liner.cpp"
    "${ZIPCG_DEVICE_ROOT}/dev_venc.cpp"
    "${ZIPCG_DEVICE_ROOT}/dev_aenc.cpp"
    "${ZIPCG_DEVICE_ROOT}/dev_aplay.cpp"
    "${ZIPCG_DEVICE_ROOT}/dev_chn.cpp"
    "${ZIPCG_DEVICE_ROOT}/dev_osd.cpp"
    "${ZIPCG_DEVICE_ROOT}/dev_svp.cpp"
    "${ZIPCG_DEVICE_ROOT}/dev_svp_yolov8.cpp"
    "${ZIPCG_DEVICE_ROOT}/dev_std.cpp"
    "${ZIPCG_DEVICE_ROOT}/font_renderer.cpp"
)

set(ZIPCG_SENSOR_C_SOURCES
    "${ZIPCG_DEVICE_ROOT}/sensor/common/sensor_common.c"
    "${ZIPCG_DEVICE_ROOT}/sensor/smart_sc4336p/sc4336p_cmos.c"
    "${ZIPCG_DEVICE_ROOT}/sensor/smart_sc4336p/sc4336p_sensor_ctrl.c"
    "${ZIPCG_DEVICE_ROOT}/sensor/galaxycore_gc4023/gc4023_cmos.c"
    "${ZIPCG_DEVICE_ROOT}/sensor/galaxycore_gc4023/gc4023_sensor_ctrl.c"
    "${ZIPCG_DEVICE_ROOT}/sensor/smart_sc431hai/sc431hai_cmos.c"
    "${ZIPCG_DEVICE_ROOT}/sensor/smart_sc431hai/sc431hai_sensor_ctrl.c"
    "${ZIPCG_DEVICE_ROOT}/sensor/hy006_3814_0011/hy006_3814_0011_cmos.c"
    "${ZIPCG_DEVICE_ROOT}/sensor/hy006_3814_0011/hy006_3814_0011_sensor_ctrl.c"
    "${ZIPCG_DEVICE_ROOT}/sensor/galaxycore_gc8613/1080p20/gc8613_cmos.c"
    "${ZIPCG_DEVICE_ROOT}/sensor/galaxycore_gc8613/1080p20/gc8613_sensor_ctrl.c"
    "${ZIPCG_DEVICE_ROOT}/sensor/galaxycore_gc8613/4k20/gc8613_cmos.c"
    "${ZIPCG_DEVICE_ROOT}/sensor/galaxycore_gc8613/4k20/gc8613_sensor_ctrl.c"
)
