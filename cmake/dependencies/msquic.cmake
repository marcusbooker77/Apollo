set(APOLLO_MSQUIC_AVAILABLE OFF)

if(APOLLO_ENABLE_MSQUIC)
    if(WIN32)
        find_path(MSQUIC_INCLUDE_DIR msquic.h)
        find_library(MSQUIC_LIBRARY NAMES msquic)

        if(MSQUIC_INCLUDE_DIR AND MSQUIC_LIBRARY)
            add_library(msquic::msquic UNKNOWN IMPORTED)
            set_target_properties(msquic::msquic PROPERTIES
                    IMPORTED_LOCATION "${MSQUIC_LIBRARY}"
                    INTERFACE_INCLUDE_DIRECTORIES "${MSQUIC_INCLUDE_DIR}")
            list(APPEND SUNSHINE_EXTERNAL_LIBRARIES msquic::msquic)
            set(APOLLO_MSQUIC_AVAILABLE ON)
        else()
            message(STATUS "MsQuic requested, but msquic.h or the msquic library was not found. Experimental QUIC support will remain disabled.")
        endif()
    else()
        message(STATUS "MsQuic support is only available for Windows builds.")
    endif()
endif()
