set(_flash_root "${CMAKE_CURRENT_LIST_DIR}/..")
draxul_add_test_target(draxul-test-flashcards flashcards 1
    "${_flash_root}/tests/flashcards_review_tests.cpp")
target_link_libraries(draxul-test-flashcards PRIVATE draxul-flashcards-review)
add_dependencies(draxul-test-flashcards draxul)
add_test(NAME draxul-flashcards-generator
    COMMAND ${Python3_EXECUTABLE} "${_flash_root}/tests/generator_tests.py")
set_tests_properties(draxul-flashcards-generator PROPERTIES LABELS "flashcards;integration;python" TIMEOUT 60)
add_test(NAME draxul-render-flashcards
    COMMAND ${Python3_EXECUTABLE} "${_flash_root}/tests/render_smoke.py"
        --exe "$<TARGET_FILE:draxul>" --out "${CMAKE_BINARY_DIR}/flashcards-render")
set_tests_properties(draxul-render-flashcards PROPERTIES
    LABELS "flashcards;render" RESOURCE_LOCK draxul_gpu TIMEOUT 120)
add_test(NAME draxul-render-flashcards-cues
    COMMAND ${Python3_EXECUTABLE} "${_flash_root}/tests/native_cue_smoke.py"
        --exe "$<TARGET_FILE:draxul>" --out "${CMAKE_BINARY_DIR}/flashcards-native-cues"
        --source "${DRAXUL_FLASHCARDS_VOCABULARY_SOURCE}")
set_tests_properties(draxul-render-flashcards-cues PROPERTIES
    LABELS "flashcards;render" RESOURCE_LOCK draxul_gpu TIMEOUT 180)
