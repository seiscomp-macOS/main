SET(REPICK_TARGET screpick)

SET(
	REPICK_SOURCES
		main.cpp
		repicker.cpp
)

INCLUDE_DIRECTORIES(.)

SC_ADD_EXECUTABLE(REPICK ${REPICK_TARGET})
SC_LINK_LIBRARIES_INTERNAL(${REPICK_TARGET} client)
SC_INSTALL_INIT(${REPICK_TARGET} ${INIT_TEMPLATE})

FILE(GLOB descs "${CMAKE_CURRENT_SOURCE_DIR}/descriptions/*.xml")
INSTALL(FILES ${descs} DESTINATION ${SC3_PACKAGE_APP_DESC_DIR})
