SET(PACKAGE_NAME SHOWEVENT)
SET(APP_NAME scshowevent)

SET(
	${PACKAGE_NAME}_SOURCES
		main.cpp
		sendevent.cpp
)

SET(
	${PACKAGE_NAME}_MOC_HEADERS
		sendevent.h
)


SC_ADD_GUI_EXECUTABLE(${PACKAGE_NAME} ${APP_NAME})
SC_LINK_LIBRARIES_INTERNAL(${APP_NAME} qt)

SC_INSTALL_INIT(${APP_NAME} ${INIT_ND_NB_TEMPLATE})

FILE(GLOB descs "${CMAKE_CURRENT_SOURCE_DIR}/descriptions/*.xml")
INSTALL(FILES ${descs} DESTINATION ${SC3_PACKAGE_APP_DESC_DIR})

