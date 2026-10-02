cmake_minimum_required(VERSION 3.25)
if(NOT TARGET Draxul::PluginSDK)
    message(FATAL_ERROR "Flashcards currently builds as a mounted Draxul product")
endif()
find_package(Python3 REQUIRED COMPONENTS Interpreter)
if(NOT TARGET nlohmann_json::nlohmann_json)
    include(FetchContent)
    FetchContent_Declare(nlohmann_json
        GIT_REPOSITORY https://github.com/nlohmann/json.git GIT_TAG v3.11.3 GIT_SHALLOW TRUE)
    set(JSON_BuildTests OFF CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(nlohmann_json)
endif()

set(DRAXUL_FLASHCARDS_VOCABULARY_SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/vocabulary.json" CACHE FILEPATH
    "Machine-local vocabulary source; fixture by default. Personal builds must not be published.")
if(DEFINED ENV{DRAXUL_FLASHCARDS_VOCABULARY_SOURCE})
    set(DRAXUL_FLASHCARDS_VOCABULARY_SOURCE "$ENV{DRAXUL_FLASHCARDS_VOCABULARY_SOURCE}"
        CACHE FILEPATH "Machine-local vocabulary source; personal builds must not be published." FORCE)
endif()
set(DRAXUL_FLASHCARDS_ARTWORK_MAP "${CMAKE_CURRENT_SOURCE_DIR}/assets/artwork.json"
    CACHE FILEPATH "Vetted artwork metadata and ID/kana mapping")

set(_deck_header "${CMAKE_CURRENT_BINARY_DIR}/generated/embedded_deck.h")
# Always check the source (including conflict siblings) at build time, rather
# than relying on configure or timestamps. BYPRODUCTS + restat avoid rebuilding
# the module when the whitelisted content is unchanged.
add_custom_target(draxul-flashcards-deck-check
    COMMAND ${Python3_EXECUTABLE} "${CMAKE_CURRENT_SOURCE_DIR}/tools/embed_deck.py"
        --source "${DRAXUL_FLASHCARDS_VOCABULARY_SOURCE}"
        --artwork "${DRAXUL_FLASHCARDS_ARTWORK_MAP}" --output "${_deck_header}"
    BYPRODUCTS "${_deck_header}"
    DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/tools/embed_deck.py"
    COMMENT "Validating and embedding translation-free flashcards" VERBATIM)

add_library(draxul-flashcards-review STATIC src/review.cpp src/review.h src/review_lock.cpp src/review_lock.h)
set_target_properties(draxul-flashcards-review PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_include_directories(draxul-flashcards-review PUBLIC src)
target_link_libraries(draxul-flashcards-review PRIVATE nlohmann_json::nlohmann_json)

add_library(draxul-flashcards-plugin MODULE src/flashcards_plugin.cpp "${_deck_header}")
add_dependencies(draxul-flashcards-plugin draxul-flashcards-deck-check)
target_include_directories(draxul-flashcards-plugin PRIVATE
    "${CMAKE_CURRENT_BINARY_DIR}/generated" "${stb_SOURCE_DIR}")
target_link_libraries(draxul-flashcards-plugin PRIVATE Draxul::PluginSDK
    Draxul::PluginSupport::Adapter Draxul::PluginSupport::NanoVGPass
    Draxul::PluginSupport::Types draxul-flashcards-review)
if(APPLE)
    target_link_libraries(draxul-flashcards-plugin PRIVATE SDL3::Headers)
    # Share the host's SDL audio implementation, as the other audio product does.
    target_link_options(draxul-flashcards-plugin PRIVATE "LINKER:-undefined,dynamic_lookup")
else()
    target_link_libraries(draxul-flashcards-plugin PRIVATE SDL3::SDL3)
endif()
set_target_properties(draxul-flashcards-plugin PROPERTIES PREFIX "" DEBUG_POSTFIX "" OUTPUT_NAME "draxul-flashcards")

set(_shaders)
if(NOT APPLE)
    get_target_property(_shader_dir draxul-nanovg-backend DRAXUL_NANOVG_SHADER_DIR)
    file(GLOB _sources CONFIGURE_DEPENDS "${_shader_dir}/*.vert" "${_shader_dir}/*.frag")
    foreach(_source IN LISTS _sources)
        get_filename_component(_name "${_source}" NAME)
        set(_output "${CMAKE_CURRENT_BINARY_DIR}/shaders/${_name}.spv")
        add_custom_command(OUTPUT "${_output}"
            COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/shaders"
            COMMAND ${Vulkan_GLSLC_EXECUTABLE} "${_source}" -o "${_output}"
            DEPENDS "${_source}" VERBATIM)
        list(APPEND _shaders "${_output}=>shaders/${_name}.spv")
        list(APPEND _shader_outputs "${_output}")
    endforeach()
    add_custom_target(draxul-flashcards-shaders DEPENDS ${_shader_outputs})
    add_dependencies(draxul-flashcards-plugin draxul-flashcards-shaders)
endif()

draxul_register_bundled_plugin(ID dev.draxul.flashcards TARGET draxul-flashcards-plugin
    MANIFEST "${CMAKE_CURRENT_SOURCE_DIR}/plugin.toml" FEATURE DRAXUL_ENABLE_FLASHCARDS
    DEPENDENCY_MODE STRICT PRODUCT_TARGETS draxul-flashcards-review draxul-flashcards-plugin
    COPY_DIRECTORIES "${CMAKE_CURRENT_SOURCE_DIR}/assets=>assets"
    COPY_FILES ${_shaders} DEPENDS ${_shader_outputs}
    TEST_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/tests/flashcards_review_tests.cpp"
    TEST_CMAKE "${CMAKE_CURRENT_SOURCE_DIR}/cmake/Tests.cmake")
draxul_configure_internal_targets_in_directory("${CMAKE_CURRENT_SOURCE_DIR}")
