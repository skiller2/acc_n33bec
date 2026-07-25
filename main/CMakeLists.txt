idf_component_register(SRCS "main.c" "rtc.c" "config.c" "beep.c" "connect.c" "eth_connect.c" "card_store.c"
                            "fs.c"
                            "http.c"
                            "log_store.c"
                           
                            "wiegand.c"
                            "ws.c"
                            "wifi.c"
                            "eth_connect.c"
                            "connect.c"
                            "time_sync.c"
                        INCLUDE_DIRS ".")
component_compile_options(-DMG_ENABLE_LINES=1)
component_compile_options(-DMG_ENABLE_PACKED_FS=1)
component_compile_options(-DUART_API_IMPLEMENTED=1)
littlefs_create_partition_image(storage ../web FLASH_IN_PROJECT)

set(EXCLUDE_COMPONENTS wifi_provisioning)

# Directorio web
set(WEB_DIR ${CMAKE_CURRENT_LIST_DIR}/../web)

# Bundle generado
set(WEB_BUNDLE ${CMAKE_BINARY_DIR}/web_bundle.bin)

set(OTA_FW ${CMAKE_BINARY_DIR}/acc_n33bec.bin)

# Bundle generado
set(FULL_FW "${CMAKE_BINARY_DIR}/acc-n33bec-fw-${GIT_TAG}.bin")

# Buscar todos los archivos web
file(GLOB_RECURSE WEB_FILES
    "${WEB_DIR}/*"
)

idf_build_get_property(python PYTHON)

add_custom_command(
    OUTPUT ${WEB_BUNDLE}
    COMMAND ${python}
            ${CMAKE_CURRENT_LIST_DIR}/../tools/make_web_bundle.py
            ${WEB_BUNDLE}
            ${WEB_FILES}
    DEPENDS
            ${WEB_FILES}
            ${python} ${CMAKE_CURRENT_LIST_DIR}/../tools/make_web_bundle.py
    COMMENT "Generating web bundle"
)

add_custom_command(
    OUTPUT ${FULL_FW}
    COMMAND ${python}
            ${CMAKE_CURRENT_LIST_DIR}/../tools/make_update_bundle.py
            ${FULL_FW}
            ${OTA_FW}
            ${WEB_BUNDLE}
    DEPENDS
            ${WEB_BUNDLE}
            ${OTA_FW}
            ${python} ${CMAKE_CURRENT_LIST_DIR}/../tools/make_update_bundle.py
    COMMENT "Generating firmare integrated"
)

add_custom_target(generate_web_bundle ALL
    DEPENDS ${WEB_BUNDLE}
)

add_custom_target(generate_full_firmware ALL
    DEPENDS ${FULL_FW}
)



#add_custom_target(release ALL
#    COMMAND git release create ${GIT_TAG}
#            ${CMAKE_BINARY_DIR}/acc_n33bec.bin
#            --clobber
#    DEPENDS ${CMAKE_BINARY_DIR}/acc_n33bec.bin
#)

#add_custom_target(release2 ALL
#    COMMAND git release upload ${GIT_TAG}
#            ${CMAKE_BINARY_DIR}/acc_n33bec.bin
#            --clobber
#    DEPENDS ${CMAKE_BINARY_DIR}/acc_n33bec.bin
#)


