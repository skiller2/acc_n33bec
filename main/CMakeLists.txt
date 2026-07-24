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

add_custom_target(generate_web_bundle ALL
    DEPENDS ${WEB_BUNDLE}
)
