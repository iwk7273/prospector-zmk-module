# Prospector Meteorite shield — dedicated single-layout build.
#
# Reuses scanner_stub.c (BLE ADV ingestion + ring buffer), brightness/backlight
# helpers, and display_settings (NVS), then replaces the entire screen layer
# with bootstrap.c + meteorite_layout.c + meteorite_data.c.

if(CONFIG_SHIELD_PROSPECTOR_METEORITE)
    # ----- Always required (display-independent) -----
    #   meteorite_data : shared snapshot store consumed by both the layout
    #                    and the host rate ingress
    #   host_rate_rx   : USB CDC line-protocol receiver (SYS_INIT auto-registers,
    #                    no call site needed); has no display dependency
    target_sources(app PRIVATE
        src/backlight_init.c
        src/meteorite_data.c
        src/host_rate_rx.c
    )

    # ----- Custom screen layer (Meteorite layout) -----
    target_sources_ifdef(CONFIG_ZMK_DISPLAY_STATUS_SCREEN_CUSTOM app PRIVATE
        src/bootstrap.c
        src/meteorite_layout.c
        src/display_settings.c
        src/brightness_control.c
    )

    # BLE scanner data plumbing (ring buffer + keyboard table + pending data)
    target_sources_ifdef(CONFIG_PROSPECTOR_MODE_SCANNER app PRIVATE
        src/scanner_stub.c
    )

    target_include_directories(app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
endif()
