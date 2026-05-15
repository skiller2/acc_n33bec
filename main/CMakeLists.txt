idf_component_register(SRCS "card_store.c"
                            "fs.c"
                            "http.c"
                            "log_store.c"
                            "main.c"
                            "processor.c"
                            "wiegand.c"
                            "ws.c"
                            "wifi.c"
                        INCLUDE_DIRS ".")
component_compile_options(-DMG_ENABLE_LINES=1)
component_compile_options(-DMG_ENABLE_PACKED_FS=1)
component_compile_options(-DUART_API_IMPLEMENTED=1)
set(EXCLUDE_COMPONENTS wifi_provisioning)
