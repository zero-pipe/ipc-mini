set(ZIPCG_MEDIA_KIT "${CMAKE_CURRENT_SOURCE_DIR}/3rdpart/zero-media-kit")

add_subdirectory("${ZIPCG_MEDIA_KIT}/libflv")
add_subdirectory("${ZIPCG_MEDIA_KIT}/libmov")
add_subdirectory("${ZIPCG_MEDIA_KIT}/libmpeg")

if (ZIPCG_ENABLE_RTP)
    add_subdirectory("${ZIPCG_MEDIA_KIT}/libmkv")
    add_subdirectory("${ZIPCG_MEDIA_KIT}/librtp")
    add_subdirectory("${ZIPCG_MEDIA_KIT}/librtsp")
    target_sources(librtsp PRIVATE
        "${ZIPCG_MEDIA_KIT}/librtsp/source/rtsp-header-transport.c")
endif()

if (ZIPCG_ENABLE_RTMP)
    add_subdirectory("${ZIPCG_MEDIA_KIT}/librtmp")
    target_sources(librtmp PRIVATE
        "${ZIPCG_MEDIA_KIT}/librtmp/source/rtmp-client.c")
endif()

foreach(_target libflv libmov libmpeg librtp)
    if (TARGET ${_target})
        if (MSVC)
            target_compile_options(${_target} PRIVATE /wd4018 /wd4267 /wd4828 /wd4996)
        else()
            target_compile_options(${_target} PRIVATE -w)
        endif()
    endif()
endforeach()
