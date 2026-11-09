INCLUDE_DIRECTORIES( ${INDI_INCLUDE_DIR} )
INCLUDE_DIRECTORIES( "${CMAKE_CURRENT_SOURCE_DIR}/../../drivers/weather" )

# Offline Astrospheric tests: fixture JSON only, no network, no API key.
ADD_EXECUTABLE(test_astrospheric_weather
    test_astrospheric_weather.cpp
    "${CMAKE_CURRENT_SOURCE_DIR}/../../drivers/weather/astrospheric_forecast.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../drivers/weather/indi_astrospheric_weather.cpp"
)

TARGET_COMPILE_DEFINITIONS(test_astrospheric_weather PRIVATE
    ASTROSPHERIC_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data")

TARGET_LINK_LIBRARIES(test_astrospheric_weather
    indidriver
    ${CURL}
    ${JSONLIB}
    ${GTEST_BOTH_LIBRARIES}
    ${CMAKE_THREAD_LIBS_INIT}
)

ADD_TEST(test_astrospheric_weather test_astrospheric_weather)
