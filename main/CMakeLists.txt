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
                            INCLUDE_DIRS "."
                            )
component_compile_options(-DMG_ENABLE_LINES=1)
component_compile_options(-DMG_ENABLE_PACKED_FS=1)
component_compile_options(-DUART_API_IMPLEMENTED=1)
littlefs_create_partition_image(storage ../web FLASH_IN_PROJECT)






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


