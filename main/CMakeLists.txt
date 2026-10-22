# Desk Buddy firmware — component registration.
#
# Sources are listed one by one rather than globbed with SRC_DIRS. That is a
# deliberate choice: it means a stray file left behind in the project folder
# never sneaks into the build, and the list below doubles as a table of contents
# for the whole firmware.

idf_component_register(
    SRCS
        "main.c"

        # Configuration that survives a reboot
        "core/buddy_settings.c"
        "core/buddy_api_key.c"
        "core/buddy_calculator.c"
        "core/buddy_timer.c"
        "core/buddy_presence.c"
        "core/buddy_selftest.c"

        # Talking to the hardware
        "hardware/hw_display.c"
        "hardware/hw_speaker.c"
        "hardware/hw_microphone.c"
        "hardware/hw_sensors.c"
        "hardware/hw_buttons.c"

        # Wi-Fi and the clock
        "net/net_wifi.c"
        "net/net_time.c"

        # Sound that is not speech
        "audio/audio_prompts.c"
        "audio/audio_music.c"
        "audio/audio_resampler.c"
        "audio/audio_voice.c"
        "audio/audio_voice_hash.c"

        # Drawing and animating the face
        "face/face_pose.c"
        "face/face_emotions.c"
        "face/face_view.c"
        "face/face_animator.c"

        # Screens
        "ui/ui_screen_face.c"
        "ui/ui_sensor_readout.c"
        "ui/ui_speech_bubble.c"
        "ui/ui_timer_badge.c"
        "ui/ui_settings.c"
        "ui/ui_calculator.c"

        # Listening, understanding, answering
        "voice/voice_commands.c"
        "voice/voice_mood_tag.c"
        "voice/voice_math.c"
        "voice/voice_number_words.c"
        "voice/voice_speech_parts.c"
        "voice/voice_speech_gate.c"
        "voice/voice_router.c"
        "voice/voice_recorder.c"
        "voice/voice_wakeword.c"
        "voice/voice_local_brain.c"
        "voice/voice_cloud_brain.c"
        "voice/voice_assistant.c"

    INCLUDE_DIRS
        "."
        "core"
        "hardware"
        "net"
        "audio"
        "face"
        "ui"
        "voice"
)

# No REQUIRES list: ESP-IDF gives the "main" component an implicit dependency on
# every other component in the build, so listing them here would add nothing but
# a second place to keep up to date.

# LVGL's headers are included as "lvgl.h" rather than "lvgl/lvgl.h".
target_compile_definitions(${COMPONENT_LIB} PRIVATE LV_LVGL_H_INCLUDE_SIMPLE)

# The speech component's headers trip a couple of pedantic warnings that are not
# worth failing a build over.
target_compile_options(${COMPONENT_LIB} PRIVATE "-Wno-format" "-Wno-deprecated-declarations")

# Bake everything in the project's spiffs/ folder into the "storage" partition:
# the chimes, and any .mp3 files you drop into spiffs/music/.
spiffs_create_partition_image(storage ../spiffs FLASH_IN_PROJECT)
