set(APOLLO_OPENTELEMETRY_AVAILABLE OFF)

if(APOLLO_ENABLE_OPENTELEMETRY)
    find_package(opentelemetry-cpp CONFIG QUIET)

    set(APOLLO_OPENTELEMETRY_LIBRARIES "")
    foreach(target_name
            opentelemetry_common
            opentelemetry_trace
            opentelemetry_resources
            opentelemetry_sdk
            opentelemetry_http_client_curl
            opentelemetry_exporter_otlp_http)
        if(TARGET ${target_name})
            list(APPEND APOLLO_OPENTELEMETRY_LIBRARIES ${target_name})
        endif()
    endforeach()

    if(TARGET opentelemetry_trace
       AND TARGET opentelemetry_sdk
       AND TARGET opentelemetry_exporter_otlp_http)
        list(APPEND SUNSHINE_EXTERNAL_LIBRARIES ${APOLLO_OPENTELEMETRY_LIBRARIES})
        set(APOLLO_OPENTELEMETRY_AVAILABLE ON)
    else()
        message(STATUS "OpenTelemetry requested, but suitable SDK/exporter targets were not found. Trace export will remain disabled.")
    endif()
endif()
