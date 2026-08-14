# Standalone cross-build for example programs (does NOT touch board/Makefile).
#
# On the HiSilicon build host:
#   cd /home/youyan/work/ipc/example
#   ./build.sh
#
# Or manually:
#   source /etc/profile
#   cmake -S . -B build -DCMAKE_TOOLCHAIN_FILE=cmake/hisilicon-musl.cmake
#   cmake --build build -j$(nproc)

cmake_minimum_required(VERSION 3.16)
project(ipc_mini_examples LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 99)

# Parent of example/ is the ipc project root (remote: /home/youyan/work/ipc).
get_filename_component(IPC_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/.." ABSOLUTE)
set(DEVICE_ROOT "${IPC_ROOT}/devices/hisi_3516cv610")

set(HISI_SDK_SOURCE "$ENV{HOME}/Hi/hi3516cv610_1020_musl/Hi3516CV610_SDK_V1.0.2.0/smp/a7_linux/source"
    CACHE PATH "HiSilicon SDK source tree (contains out/include and out/lib)")
set(HISI_REL_INC "${HISI_SDK_SOURCE}/out/include" CACHE PATH "HiSilicon MPI include dir")
set(HISI_REL_LIB "${HISI_SDK_SOURCE}/out/lib" CACHE PATH "HiSilicon MPI library dir")
set(SENSOR0_TYPE "SC4336P_MIPI_4M_30FPS_10BIT" CACHE STRING "Sensor0 type macro")
set(SENSOR1_TYPE "SC4336P_MIPI_4M_30FPS_10BIT" CACHE STRING "Sensor1 type macro")

if (NOT EXISTS "${HISI_REL_INC}/ot_common.h" AND NOT EXISTS "${HISI_REL_INC}/ss_mpi_sys.h")
    # Headers may live flat under out/include; just sanity-check the dir.
    if (NOT IS_DIRECTORY "${HISI_REL_INC}")
        message(FATAL_ERROR "HISI_REL_INC not found: ${HISI_REL_INC}")
    endif()
endif()
if (NOT EXISTS "${HISI_REL_LIB}/libss_mpi.a")
    message(FATAL_ERROR "HISI_REL_LIB/libss_mpi.a not found: ${HISI_REL_LIB}")
endif()

set(HISI_COMMON_DEFINES
    NPU
    SENSOR0_TYPE=${SENSOR0_TYPE}
    SENSOR1_TYPE=${SENSOR1_TYPE}
    ${SENSOR0_TYPE}_SELECT=y
    ${SENSOR1_TYPE}_SELECT=y
    ISP_V2
    hi3516cv610
    OT_XXXX
    USER_BIT_32
    KERNEL_BIT_32
    OT_RELEASE
)

set(HISI_COMMON_INCLUDES
    "${HISI_REL_INC}"
    "${HISI_REL_INC}/exp_inc"
    "${HISI_SDK_SOURCE}/osal/include"
    "${HISI_SDK_SOURCE}/../platform/securec/include"
    "${DEVICE_ROOT}"
    "${DEVICE_ROOT}/support"
    "${DEVICE_ROOT}/sensor/common"
)

# Same MPI group as board/Makefile (without KVS).
set(HISI_MPI_LIBS
    "${HISI_REL_LIB}/libss_mpi.a"
    "${HISI_REL_LIB}/libss_mpi_sysbind.a"
    "${HISI_REL_LIB}/libss_mpi_sysmem.a"
    "${HISI_REL_LIB}/libss_mpi_ae.a"
    "${HISI_REL_LIB}/libss_mpi_isp.a"
    "${HISI_REL_LIB}/libot_mpi_isp.a"
    "${HISI_REL_LIB}/libss_mpi_awb.a"
    "${HISI_REL_LIB}/libdehaze.a"
    "${HISI_REL_LIB}/libextend_stats.a"
    "${HISI_REL_LIB}/libdrc.a"
    "${HISI_REL_LIB}/libldci.a"
    "${HISI_REL_LIB}/libbnr.a"
    "${HISI_REL_LIB}/libcalcflicker.a"
    "${HISI_REL_LIB}/libacs.a"
    "${HISI_REL_LIB}/libir_auto.a"
    "${HISI_REL_LIB}/libss_mpi_aibnr.a"
    "${HISI_REL_LIB}/libsvp_acl.a"
    "${HISI_REL_LIB}/libprotobuf-c.a"
    "${HISI_REL_LIB}/libss_mpi_aidetect.a"
    "${HISI_REL_LIB}/libaiservice_comm.a"
    "${HISI_REL_LIB}/libss_mpi_cipher.a"
    "${HISI_REL_LIB}/libot_osal.a"
    "${HISI_REL_LIB}/libss_mpi_audio.a"
    "${HISI_REL_LIB}/libss_mpi_audio_adp.a"
    "${HISI_REL_LIB}/libvoice_engine.a"
    "${HISI_REL_LIB}/libaac_enc.a"
    "${HISI_REL_LIB}/libaac_dec.a"
    "${HISI_REL_LIB}/libaac_comm.a"
    "${HISI_REL_LIB}/libdnvqe.a"
    "${HISI_REL_LIB}/libaac_sbr_enc.a"
    "${HISI_REL_LIB}/libaac_sbr_dec.a"
    "${HISI_REL_LIB}/libupvqe.a"
    "${HISI_REL_LIB}/libsecurec.a"
    "${HISI_REL_LIB}/libsns_sc4336p.a"
)

# Optional sensor libs if present (match board/Makefile.param).
foreach(_sns IN ITEMS gc4023 sc431hai sc450ai sc500ai os04d10)
    if (EXISTS "${HISI_REL_LIB}/libsns_${_sns}.a")
        list(APPEND HISI_MPI_LIBS "${HISI_REL_LIB}/libsns_${_sns}.a")
    endif()
endforeach()

function(example_apply_hisi_flags target_name)
    target_include_directories(${target_name} PRIVATE ${HISI_COMMON_INCLUDES})
    target_compile_definitions(${target_name} PRIVATE ${HISI_COMMON_DEFINES})
    target_compile_options(${target_name} PRIVATE
        -Wall
        -Wno-unused-parameter
        -Wno-missing-field-initializers
        -fsigned-char
        -ffunction-sections
        -fdata-sections
        -fstack-protector-strong
        -Os
        -mthumb
        -mcpu=cortex-a7
        -mfloat-abi=softfp
        -mfpu=neon-vfpv4
        -include stddef.h
        $<$<COMPILE_LANGUAGE:CXX>:-Wno-reorder>
        $<$<COMPILE_LANGUAGE:CXX>:-Wno-psabi>
    )
    target_link_options(${target_name} PRIVATE
        -static
        -Wl,--gc-sections
        -Wl,-Map,$<TARGET_FILE_DIR:${target_name}>/${target_name}.map
    )
endfunction()

function(example_link_mpi target_name)
    target_link_libraries(${target_name} PRIVATE
        -Wl,--start-group
        ${HISI_MPI_LIBS}
        -Wl,--end-group
        pthread
        dl
        rt
        m
    )
endfunction()

# -----------------------------------------------------------------------------
# pipeline_direct_mpp: raw HiSilicon MPI calls + project SC4336P sensor driver
# -----------------------------------------------------------------------------
add_executable(pipeline_direct_mpp
    pipeline_direct_mpp.cpp
    "${DEVICE_ROOT}/sensor/common/sensor_common.c"
    "${DEVICE_ROOT}/sensor/smart_sc4336p/sc4336p_cmos.c"
    "${DEVICE_ROOT}/sensor/smart_sc4336p/sc4336p_sensor_ctrl.c"
)
example_apply_hisi_flags(pipeline_direct_mpp)
example_link_mpi(pipeline_direct_mpp)

# -----------------------------------------------------------------------------
# pipeline_with_chn: uses project hisilicon::dev::chn stack
# -----------------------------------------------------------------------------
set(ZTK_ROOT "${IPC_ROOT}/3rdpart/zero-tool-kit")
set(ZTK_SRC "${ZTK_ROOT}/src")

set(EXAMPLE_DEVICE_CXX
    "${DEVICE_ROOT}/dev_sys.cpp"
    "${DEVICE_ROOT}/dev_vi.cpp"
    "${DEVICE_ROOT}/dev_vi_isp.cpp"
    "${DEVICE_ROOT}/dev_vi_sc4336p_liner.cpp"
    "${DEVICE_ROOT}/dev_vi_hy006_3814_0011_liner.cpp"
    "${DEVICE_ROOT}/dev_vi_sc431hai_liner.cpp"
    "${DEVICE_ROOT}/dev_vi_gc8613_liner.cpp"
    "${DEVICE_ROOT}/dev_vi_gc4023_liner.cpp"
    "${DEVICE_ROOT}/dev_venc.cpp"
    "${DEVICE_ROOT}/dev_aenc.cpp"
    "${DEVICE_ROOT}/dev_aplay.cpp"
    "${DEVICE_ROOT}/dev_chn.cpp"
    "${DEVICE_ROOT}/dev_osd.cpp"
    "${DEVICE_ROOT}/dev_std.cpp"
    "${DEVICE_ROOT}/font_renderer.cpp"
)

set(EXAMPLE_DEVICE_C
    "${DEVICE_ROOT}/sensor/common/sensor_common.c"
    "${DEVICE_ROOT}/sensor/smart_sc4336p/sc4336p_cmos.c"
    "${DEVICE_ROOT}/sensor/smart_sc4336p/sc4336p_sensor_ctrl.c"
    "${DEVICE_ROOT}/sensor/galaxycore_gc4023/gc4023_cmos.c"
    "${DEVICE_ROOT}/sensor/galaxycore_gc4023/gc4023_sensor_ctrl.c"
    "${DEVICE_ROOT}/sensor/smart_sc431hai/sc431hai_cmos.c"
    "${DEVICE_ROOT}/sensor/smart_sc431hai/sc431hai_sensor_ctrl.c"
    "${DEVICE_ROOT}/sensor/hy006_3814_0011/hy006_3814_0011_cmos.c"
    "${DEVICE_ROOT}/sensor/hy006_3814_0011/hy006_3814_0011_sensor_ctrl.c"
    "${DEVICE_ROOT}/sensor/galaxycore_gc8613/1080p20/gc8613_cmos.c"
    "${DEVICE_ROOT}/sensor/galaxycore_gc8613/1080p20/gc8613_sensor_ctrl.c"
    "${DEVICE_ROOT}/sensor/galaxycore_gc8613/4k20/gc8613_cmos.c"
    "${DEVICE_ROOT}/sensor/galaxycore_gc8613/4k20/gc8613_sensor_ctrl.c"
)

# Minimal ztk needed by DEV_WRITE_LOG_* macros.
set(EXAMPLE_ZTK_C
    "${ZTK_SRC}/core/ztk_errno.c"
    "${ZTK_SRC}/core/ztk.c"
    "${ZTK_SRC}/platform/platform.c"
    "${ZTK_SRC}/platform/linux/platform_linux.c"
    "${ZTK_SRC}/platform/linux/sync_pthread.c"
    "${ZTK_SRC}/platform/linux/sem_pthread.c"
    "${ZTK_SRC}/platform/linux/thread_pthread.c"
    "${ZTK_SRC}/platform/linux/thread_priority_linux.c"
    "${ZTK_SRC}/util/log.c"
)

add_executable(pipeline_with_chn
    pipeline_with_chn.cpp
    ${EXAMPLE_DEVICE_CXX}
    ${EXAMPLE_DEVICE_C}
    ${EXAMPLE_ZTK_C}
)
example_apply_hisi_flags(pipeline_with_chn)
target_include_directories(pipeline_with_chn PRIVATE
    "${IPC_ROOT}/include"
    "${IPC_ROOT}/board"
    "${ZTK_ROOT}/include"
    "${IPC_ROOT}/3rdpart/freetype-2.7.1/include/freetype2"
)
target_link_libraries(pipeline_with_chn PRIVATE
    -Wl,--start-group
    "${IPC_ROOT}/3rdpart/freetype-2.7.1/lib/libfreetype.a"
    ${HISI_MPI_LIBS}
    -Wl,--end-group
    pthread
    dl
    rt
    m
)

message(STATUS "IPC_ROOT=${IPC_ROOT}")
message(STATUS "HISI_REL_INC=${HISI_REL_INC}")
message(STATUS "HISI_REL_LIB=${HISI_REL_LIB}")
message(STATUS "Targets: pipeline_direct_mpp, pipeline_with_chn")
