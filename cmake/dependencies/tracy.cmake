set(APOLLO_TRACY_AVAILABLE OFF)

if(APOLLO_ENABLE_TRACY)
    find_package(Tracy CONFIG QUIET)

    if(NOT TARGET Tracy::TracyClient AND NOT TARGET TracyClient)
        message(STATUS "Tracy package not found in the system. Falling back to FetchContent.")
        include(FetchContent)
        FetchContent_Declare(
                tracy
                GIT_REPOSITORY https://github.com/wolfpld/tracy.git
                GIT_TAG v0.11.1
                GIT_SHALLOW TRUE)
        FetchContent_MakeAvailable(tracy)
    endif()

    if(TARGET Tracy::TracyClient)
        list(APPEND SUNSHINE_EXTERNAL_LIBRARIES Tracy::TracyClient)
        set(APOLLO_TRACY_AVAILABLE ON)
    elseif(TARGET TracyClient)
        list(APPEND SUNSHINE_EXTERNAL_LIBRARIES TracyClient)
        set(APOLLO_TRACY_AVAILABLE ON)
    else()
        message(STATUS "Tracy requested, but no Tracy client target was found. Profiling instrumentation will remain disabled.")
    endif()
endif()
