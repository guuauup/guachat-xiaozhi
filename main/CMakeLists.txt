# Define source files
set(SOURCES "audio/audio_codec.cc"
            "audio/audio_debugger.cc"
            "audio/audio_service.cc"
            "audio/demuxer/ogg_demuxer.cc"
            "notify/notify_player.cc"
            "audio/codecs/no_audio_codec.cc"
            "display/display.cc"
            "display/text_glyph.cc"
            "display/lcd_display.cc"
            "display/lvgl_display/lvgl_display.cc"
            "display/lvgl_display/emoji_collection.cc"
            "display/lvgl_display/lvgl_theme.cc"
            "display/lvgl_display/lvgl_font.cc"
            "display/lvgl_display/dynamic_glyph_cache.cc"
            "display/lvgl_display/lvgl_image.cc"
            "display/lvgl_display/gif/lvgl_gif.cc"
            "display/lvgl_display/gif/gifdec.c"
            "display/lvgl_display/jpg/image_to_jpeg.cpp"
            "display/lvgl_display/jpg/jpeg_to_image.c"
            "protocols/protocol.cc"
            "protocols/text_glyph_payload.cc"
            "protocols/mqtt_protocol.cc"
            "protocols/websocket_protocol.cc"
            "mcp_server.cc"
            "system_info.cc"
            "application.cc"
            "ota.cc"
            "settings.cc"
            "device_state_machine.cc"
            "assets.cc"
            "main.cc"
            )

set(INCLUDE_DIRS "." "display" "display/lvgl_display" "display/lvgl_display/jpg" "audio" "audio/demuxer" "protocols")

# Common hardware layer shared by board implementations.
list(APPEND SOURCES
    "boards/common/board.cc"
    "boards/common/wifi_board.cc"
    "boards/common/backlight.cc"
    "boards/common/button.cc"
)
list(APPEND INCLUDE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}/boards/common)


idf_build_get_property(build_components BUILD_COMPONENTS)
# Function to find component dynamically by pattern
function(find_component_by_pattern PATTERN COMPONENT_VAR PATH_VAR)
    foreach(COMPONENT ${build_components})
        if(COMPONENT MATCHES "${PATTERN}")
            set(${COMPONENT_VAR} ${COMPONENT} PARENT_SCOPE)
            idf_component_get_property(COMPONENT_PATH ${COMPONENT} COMPONENT_DIR)
            set(${PATH_VAR} "${COMPONENT_PATH}" PARENT_SCOPE)
            break()
        endif()
    endforeach()
endfunction()

# Set default BUILTIN_TEXT_FONT and BUILTIN_ICON_FONT
set(BUILTIN_TEXT_FONT font_noto_sans_basic_14_1)
set(BUILTIN_ICON_FONT font_material_symbols_14_1)

# This repository ships a single board: GUACHAT 2G4
# (ESP32-S3, 320x480 ST7796, bare I2S + MAX98357).
if(CONFIG_BOARD_TYPE_GUAUP_GUACHAT)
    set(BOARD_DIR "guaup/guachat")
    set(BUILTIN_TEXT_FONT font_noto_sans_basic_20_4)
    set(BUILTIN_ICON_FONT font_material_symbols_20_4)
    # The idle face is the only thing on screen while idle, so it gets animated
    # GIF expressions instead of the static 32 px PNG set the default used to
    # pin (which was upscaled and looked soft). otto-gif resolves to the
    # txp666/otto-emoji-gif-component gifs/ directory: 21 x 240x240 GIFs,
    # ~1 MB in an 8 MB assets partition.
    set(DEFAULT_EMOJI_COLLECTION otto-gif)
endif()
if(NOT BOARD_DIR AND NOT CMAKE_BUILD_EARLY_EXPANSION)
    message(FATAL_ERROR "The selected board does not define BOARD_DIR")
endif()
set(BOARD_CONFIG_FILE "${CMAKE_CURRENT_SOURCE_DIR}/boards/${BOARD_DIR}/config.json")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${BOARD_CONFIG_FILE}")

# BOARD_TYPE and BOARD_MANUFACTURER are reported by the firmware.
# Prefer config.json and fall back to values derived from the full board directory.
set(BOARD_TYPE "")
set(BOARD_MANUFACTURER "")
if(EXISTS "${BOARD_CONFIG_FILE}")
    file(READ "${BOARD_CONFIG_FILE}" BOARD_CONFIG_JSON)
    string(REGEX MATCH "\"type\"[ \t\r\n]*:[ \t\r\n]*\"([^\"]+)\"" BOARD_TYPE_MATCH "${BOARD_CONFIG_JSON}")
    if(BOARD_TYPE_MATCH)
        set(BOARD_TYPE "${CMAKE_MATCH_1}")
    endif()
    string(REGEX MATCH "\"manufacturer\"[ \t\r\n]*:[ \t\r\n]*\"([^\"]+)\"" BOARD_MANUFACTURER_MATCH "${BOARD_CONFIG_JSON}")
    if(BOARD_MANUFACTURER_MATCH)
        set(BOARD_MANUFACTURER "${CMAKE_MATCH_1}")
    endif()
endif()
if(NOT BOARD_TYPE)
    string(REPLACE "/" "-" BOARD_TYPE "${BOARD_DIR}")
endif()
if(NOT BOARD_MANUFACTURER AND BOARD_DIR MATCHES "^([^/]+)/")
    set(BOARD_MANUFACTURER "${CMAKE_MATCH_1}")
endif()
if(NOT CMAKE_BUILD_EARLY_EXPANSION AND NOT BOARD_TYPE MATCHES "^[a-z0-9.-]+$")
    message(FATAL_ERROR
        "Invalid BOARD_TYPE '${BOARD_TYPE}': only lowercase letters, digits, '.' and '-' are allowed"
    )
endif()

# Note: plain GLOB, not CONFIGURE_DEPENDS - this file is also included by
# idf_component_get_requirements() in CMake script mode, which rejects
# CONFIGURE_DEPENDS. Adding a .cc to a board directory therefore needs an
# explicit "idf.py reconfigure" before the next build.
file(GLOB BOARD_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/boards/${BOARD_DIR}/*.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/boards/${BOARD_DIR}/*.c
)
list(APPEND SOURCES ${BOARD_SOURCES})

# Select one audio engine for each chip family
if(CONFIG_IDF_TARGET_ESP32S3 OR CONFIG_IDF_TARGET_ESP32P4 OR CONFIG_IDF_TARGET_ESP32S31)
    list(APPEND SOURCES "audio/engines/afe_audio_engine.cc")
    list(APPEND SOURCES "audio/wake_words/custom_wake_word.cc")
    list(APPEND SOURCES "audio/wake_words/wake_word_audio_cache.cc")
else()
    list(APPEND SOURCES "audio/engines/lite_audio_engine.cc")
    list(APPEND SOURCES "audio/wake_words/esp_wake_word.cc")
endif()

# Auto Select Additional Sources
if (CONFIG_USE_ESP_BLUFI_WIFI_PROVISIONING)
    list(APPEND SOURCES "boards/common/blufi.cpp")
endif ()
# Select language directory according to Kconfig
if(CONFIG_LANGUAGE_ZH_CN)
    set(LANG_DIR "zh-CN")
elseif(CONFIG_LANGUAGE_EN_US)
    set(LANG_DIR "en-US")
elseif(NOT CMAKE_BUILD_EARLY_EXPANSION)
    # Early expansion runs before sdkconfig is available, so only enforce this
    # once the real configuration is in place.
    message(FATAL_ERROR "No supported language selected")
endif()

# Define generation path
set(LANG_JSON "${CMAKE_CURRENT_SOURCE_DIR}/assets/locales/${LANG_DIR}/language.json")
set(LANG_HEADER "${CMAKE_CURRENT_SOURCE_DIR}/assets/lang_config.h")

# Collect current language audio files
file(GLOB LANG_SOUNDS ${CMAKE_CURRENT_SOURCE_DIR}/assets/locales/${LANG_DIR}/*.ogg)

# If not en-US, collect en-US audio files as fallback for missing files
if(NOT LANG_DIR STREQUAL "en-US")
    file(GLOB EN_US_SOUNDS ${CMAKE_CURRENT_SOURCE_DIR}/assets/locales/en-US/*.ogg)

    # Extract filenames (without path) from current language
    set(EXISTING_NAMES "")
    foreach(SOUND_FILE ${LANG_SOUNDS})
        get_filename_component(FILENAME ${SOUND_FILE} NAME)
        list(APPEND EXISTING_NAMES ${FILENAME})
    endforeach()

    # Only add en-US audio files that are missing in current language
    foreach(EN_SOUND ${EN_US_SOUNDS})
        get_filename_component(FILENAME ${EN_SOUND} NAME)
        if(NOT ${FILENAME} IN_LIST EXISTING_NAMES)
            list(APPEND LANG_SOUNDS ${EN_SOUND})
            message(STATUS "Using en-US fallback for missing audio: ${FILENAME}")
        endif()
    endforeach()
endif()

file(GLOB COMMON_SOUNDS ${CMAKE_CURRENT_SOURCE_DIR}/assets/common/*.ogg)

idf_component_register(SRCS ${SOURCES}
                    EMBED_FILES ${LANG_SOUNDS} ${COMMON_SOUNDS}
                    INCLUDE_DIRS ${INCLUDE_DIRS}
                    WHOLE_ARCHIVE
                    PRIV_REQUIRES
                        esp_pm
                        esp_psram
                        esp_netif
                        esp_eth
                        esp_driver_gpio
                        esp_driver_uart
                        esp_driver_spi
                        esp_driver_i2c
                        esp_driver_i2s
                        esp_driver_ledc
                        esp_driver_jpeg
                        esp_driver_ppa
                        esp_driver_tsens
                        esp_app_format
                        app_update
                        spi_flash
                        mbedtls
                        console
                        efuse
                        bt
                        fatfs
                        xiaozhi-fonts
                    )

# Keep project-owned C++ on GNU C++23. ESP-IDF 6 may default to a newer
# dialect; pin the baseline so main/ does not depend on it.
if(NOT CMAKE_BUILD_EARLY_EXPANSION)
    target_compile_options(${COMPONENT_LIB} PRIVATE $<$<COMPILE_LANGUAGE:CXX>:-std=gnu++23>)
endif()

# Use target_compile_definitions to define the reported board identity
# If BOARD_NAME is empty, use BOARD_TYPE
if(NOT BOARD_NAME)
    set(BOARD_NAME ${BOARD_TYPE})
endif()
if(NOT CMAKE_BUILD_EARLY_EXPANSION AND NOT BOARD_NAME MATCHES "^[a-z0-9.-]+$")
    message(FATAL_ERROR
        "Invalid BOARD_NAME '${BOARD_NAME}': only lowercase letters, digits, '.' and '-' are allowed"
    )
endif()
string(REGEX MATCH "^font_noto_sans_basic_([0-9]+)_([0-9]+)$" _font_match "${BUILTIN_TEXT_FONT}")
if(NOT _font_match)
    message(FATAL_ERROR "Unsupported built-in text font name: ${BUILTIN_TEXT_FONT}")
endif()
set(TEXT_FONT_SIZE "${CMAKE_MATCH_1}")
set(TEXT_FONT_BPP "${CMAKE_MATCH_2}")
if(NOT CMAKE_BUILD_EARLY_EXPANSION)
    target_compile_definitions(${COMPONENT_LIB}
                        PRIVATE BOARD_TYPE=\"${BOARD_TYPE}\" BOARD_NAME=\"${BOARD_NAME}\"
                        PRIVATE BOARD_MANUFACTURER=\"${BOARD_MANUFACTURER}\"
                        PRIVATE BUILTIN_TEXT_FONT=${BUILTIN_TEXT_FONT} BUILTIN_ICON_FONT=${BUILTIN_ICON_FONT}
                        PRIVATE TEXT_FONT_SIZE=${TEXT_FONT_SIZE} TEXT_FONT_BPP=${TEXT_FONT_BPP}
                        )
    # ESP32-S3 LVGL builtin pool: PSRAM boards allocate it from PSRAM.
    # Boards that set CONFIG_SPIRAM=n must not keep the 1MB chip-default
    # pool as a .dram0.bss array (build.py also switches those variants
    # back to CLIB malloc).
    if(CONFIG_IDF_TARGET_ESP32S3)
        idf_component_get_property(lvgl_lib lvgl__lvgl COMPONENT_LIB)
        if(lvgl_lib)
            target_include_directories(${lvgl_lib} PRIVATE "${CMAKE_CURRENT_LIST_DIR}/display")
            if(CONFIG_SPIRAM)
                target_compile_definitions(${lvgl_lib} PRIVATE
                    "LV_MEM_POOL_INCLUDE=\"lvgl_psram_pool.h\"")
            elseif(CONFIG_LV_USE_BUILTIN_MALLOC)
                target_compile_definitions(${lvgl_lib} PRIVATE
                    "LV_MEM_POOL_INCLUDE=\"lvgl_no_psram_pool.h\"")
            endif()
        endif()
    endif()
endif()

# Add generation rules
add_custom_command(
    OUTPUT ${LANG_HEADER}
    COMMAND python ${PROJECT_DIR}/scripts/gen_lang.py
            --language "${LANG_DIR}"
            --output "${LANG_HEADER}"
    DEPENDS
        ${LANG_JSON}
        ${PROJECT_DIR}/scripts/gen_lang.py
    COMMENT "Generating ${LANG_DIR} language config"
)

# Force build generation dependencies
add_custom_target(lang_header ALL
    DEPENDS ${LANG_HEADER}
)

# Find ESP-SR component dynamically
find_component_by_pattern("espressif__esp-sr" ESP_SR_COMPONENT ESP_SR_COMPONENT_PATH)
if(ESP_SR_COMPONENT_PATH)
    set(ESP_SR_MODEL_PATH "${ESP_SR_COMPONENT_PATH}/model")
endif()

# Find xiaozhi-fonts component dynamically
find_component_by_pattern("xiaozhi-fonts" NOTO_FONTS_COMPONENT NOTO_FONTS_COMPONENT_PATH)
if(NOTO_FONTS_COMPONENT_PATH)
    set(NOTO_FONTS_PATH "${NOTO_FONTS_COMPONENT_PATH}")
endif()


# Function to build default assets based on configuration
function(build_default_assets_bin)
    # Set output path for generated assets.bin
    set(GENERATED_ASSETS_BIN "${CMAKE_BINARY_DIR}/generated_assets.bin")
    set(DEFAULT_ASSETS_DEPENDENCIES
        "${SDKCONFIG}"
        "${PROJECT_DIR}/scripts/build_default_assets.py"
    )

    # Prepare arguments for build script
    set(BUILD_ARGS
        "--sdkconfig" "${SDKCONFIG}"
        "--output" "${GENERATED_ASSETS_BIN}"
    )

    # Add builtin text font if defined
    if(BUILTIN_TEXT_FONT)
        list(APPEND BUILD_ARGS "--builtin_text_font" "${BUILTIN_TEXT_FONT}")
        string(REPLACE "_basic_" "_common_" COMMON_TEXT_FONT "${BUILTIN_TEXT_FONT}")
        set(COMMON_TEXT_FONT_FILE "${NOTO_FONTS_PATH}/cbin/${COMMON_TEXT_FONT}.bin")
        if(EXISTS "${COMMON_TEXT_FONT_FILE}")
            list(APPEND DEFAULT_ASSETS_DEPENDENCIES "${COMMON_TEXT_FONT_FILE}")
        endif()
        if(EXISTS "${NOTO_FONTS_PATH}/manifest.json")
            list(APPEND DEFAULT_ASSETS_DEPENDENCIES "${NOTO_FONTS_PATH}/manifest.json")
        endif()
    endif()

    # Add default emoji collection if defined
    if(DEFAULT_EMOJI_COLLECTION)
        list(APPEND BUILD_ARGS "--emoji_collection" "${DEFAULT_EMOJI_COLLECTION}")
        file(GLOB DEFAULT_EMOJI_FILES CONFIGURE_DEPENDS
            "${NOTO_FONTS_PATH}/png/${DEFAULT_EMOJI_COLLECTION}/*"
        )
        list(APPEND DEFAULT_ASSETS_DEPENDENCIES ${DEFAULT_EMOJI_FILES})
    endif()

    # Add default assets extra files if defined
    if(DEFAULT_ASSETS_EXTRA_FILES)
        list(APPEND BUILD_ARGS "--extra_files" "${DEFAULT_ASSETS_EXTRA_FILES}")
    endif()

    list(APPEND BUILD_ARGS "--esp_sr_model_path" "${ESP_SR_MODEL_PATH}")
    list(APPEND BUILD_ARGS "--noto_fonts_path" "${NOTO_FONTS_PATH}")

    # Enforce assets.bin fits the flash partition (e.g. ESP-HI 4MB map uses 1MB assets).
    partition_table_get_partition_info(assets_partition_size "--partition-name assets" "size")
    if(assets_partition_size)
        list(APPEND BUILD_ARGS "--max_size" "${assets_partition_size}")
    endif()

    # Create custom command to build assets
    add_custom_command(
        OUTPUT ${GENERATED_ASSETS_BIN}
        COMMAND python ${PROJECT_DIR}/scripts/build_default_assets.py ${BUILD_ARGS}
        DEPENDS ${DEFAULT_ASSETS_DEPENDENCIES}
        COMMENT "Building default assets.bin based on configuration"
        VERBATIM
    )

    # Create target for generated assets
    add_custom_target(generated_default_assets ALL
        DEPENDS ${GENERATED_ASSETS_BIN}
    )

    # Set the generated file path in parent scope
    set(GENERATED_ASSETS_LOCAL_FILE ${GENERATED_ASSETS_BIN} PARENT_SCOPE)

    message(STATUS "Default assets build configured: ${GENERATED_ASSETS_BIN}")
endfunction()


# Function to get local assets file path (handles both URL and local file)
function(get_assets_local_file assets_source assets_local_file_var)
    # Check if it's a URL (starts with http:// or https://)
    if(assets_source MATCHES "^https?://")
        # It's a URL, download it
        get_filename_component(ASSETS_FILENAME "${assets_source}" NAME)
        set(ASSETS_LOCAL_FILE "${CMAKE_BINARY_DIR}/${ASSETS_FILENAME}")
        set(ASSETS_TEMP_FILE "${CMAKE_BINARY_DIR}/${ASSETS_FILENAME}.tmp")

        # Check if local file exists
        if(EXISTS ${ASSETS_LOCAL_FILE})
            message(STATUS "Assets file ${ASSETS_FILENAME} already exists, skipping download")
        else()
            message(STATUS "Downloading ${ASSETS_FILENAME}")

            # Clean up any existing temp file
            if(EXISTS ${ASSETS_TEMP_FILE})
                file(REMOVE ${ASSETS_TEMP_FILE})
            endif()

            # Download to temporary file first
            file(DOWNLOAD ${assets_source} ${ASSETS_TEMP_FILE}
                 STATUS DOWNLOAD_STATUS)
            list(GET DOWNLOAD_STATUS 0 STATUS_CODE)
            if(NOT STATUS_CODE EQUAL 0)
                # Clean up temp file on failure
                if(EXISTS ${ASSETS_TEMP_FILE})
                    file(REMOVE ${ASSETS_TEMP_FILE})
                endif()
                message(FATAL_ERROR "Failed to download ${ASSETS_FILENAME} from ${assets_source}")
            endif()

            # Move temp file to final location (atomic operation)
            file(RENAME ${ASSETS_TEMP_FILE} ${ASSETS_LOCAL_FILE})
            message(STATUS "Successfully downloaded ${ASSETS_FILENAME}")
        endif()
    else()
        # It's a local file path
        if(IS_ABSOLUTE "${assets_source}")
            set(ASSETS_LOCAL_FILE "${assets_source}")
        else()
            set(ASSETS_LOCAL_FILE "${CMAKE_CURRENT_SOURCE_DIR}/${assets_source}")
        endif()

        # Check if local file exists
        if(NOT EXISTS ${ASSETS_LOCAL_FILE})
            message(FATAL_ERROR "Assets file not found: ${ASSETS_LOCAL_FILE}")
        endif()

        message(STATUS "Using assets file: ${ASSETS_LOCAL_FILE}")
    endif()

    set(${assets_local_file_var} ${ASSETS_LOCAL_FILE} PARENT_SCOPE)
endfunction()


partition_table_get_partition_info(size "--partition-name assets" "size")
partition_table_get_partition_info(offset "--partition-name assets" "offset")
if ("${size}" AND "${offset}")
    # Flash assets based on configuration
    if(CONFIG_FLASH_DEFAULT_ASSETS)
        # Build default assets based on configuration
        build_default_assets_bin()
        esptool_py_flash_to_partition(flash "assets" "${GENERATED_ASSETS_LOCAL_FILE}")
        message(STATUS "Generated default assets flash configured: ${GENERATED_ASSETS_LOCAL_FILE} -> assets partition")
    elseif(CONFIG_FLASH_CUSTOM_ASSETS)
        # Flash custom assets
        get_assets_local_file("${CONFIG_CUSTOM_ASSETS_FILE}" ASSETS_LOCAL_FILE)
        esptool_py_flash_to_partition(flash "assets" "${ASSETS_LOCAL_FILE}")
        message(STATUS "Custom assets flash configured: ${ASSETS_LOCAL_FILE} -> assets partition")
    elseif(CONFIG_FLASH_NONE_ASSETS)
        message(STATUS "Assets flashing disabled (FLASH_NONE_ASSETS)")
    endif()
else()
    message(STATUS "Assets partition not found, using v1 partition table")
endif()


