add_library(minhook STATIC
    src/buffer.c
    src/hook.c
    src/trampoline.c
    src/hde/hde64.c
)
target_include_directories(minhook PUBLIC include)
target_include_directories(minhook PRIVATE src)
target_compile_definitions(minhook PRIVATE
    STRICT
    WIN32_LEAN_AND_MEAN
)
if(MSVC)
    target_compile_options(minhook PRIVATE /W3)
endif()
