cmake_minimum_required(VERSION 3.10)
project(NodeAudioEngine VERSION 0.0.1 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_subdirectory(libs/SDL)
add_subdirectory(libs/GLAD)

set(JUCE_BUILD_HELPER_TOOLS ON CACHE INTERNAL "")
set(JUCE_ENABLE_MODULE_SOURCE_GROUPS ON CACHE INTERNAL "")
add_subdirectory(libs/JUCE)

juce_add_console_app(${PROJECT_NAME}
    PRODUCT_NAME "NodeAudioEngine"
)

target_sources(${PROJECT_NAME} PRIVATE
    src/main.cpp
)

target_compile_definitions(${PROJECT_NAME} PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
)

target_link_libraries(${PROJECT_NAME} PRIVATE
    SDL3::SDL3
    GLAD
    juce::juce_audio_devices    # Talk to sound cards (ASIO, CoreAudio)
    juce::juce_audio_basics     # Sine waves, buffers, ADSR logic
    juce::juce_events           # Required for JUCE's internal threading
)

target_include_directories(${PROJECT_NAME} PRIVATE
    libs/SDL/include
    libs/GLAD/include
)