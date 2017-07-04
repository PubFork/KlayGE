SET(BOOST_ROOT "${KLAYGE_ROOT_DIR}/External/boost")
SET(BOOST_LIBRARYDIR "${BOOST_ROOT}/lib/${KLAYGE_PLATFORM_NAME}" "${KLAYGE_ROOT_DIR}/KlayGE/bin/${KLAYGE_PLATFORM_NAME}")
IF(KLAYGE_IS_DEV_PLATFORM)
	SET(BOOST_COMPONENTS program_options)
ELSE()
	SET(BOOST_COMPONENTS "")
ENDIF()
SET(BOOST_NO_SYSTEM_PATHS ON)
SET(Boost_COMPILER "-${KLAYGE_COMPILER_NAME}${KLAYGE_COMPILER_VERSION}")
IF(NOT MSVC)
	IF(ANDROID OR IOS)
		SET(BOOST_COMPONENTS ${BOOST_COMPONENTS} filesystem system)
		SET(Boost_USE_STATIC_LIBS ON)
		SET(Boost_USE_STATIC_RUNTIME ON)
		SET(Boost_THREADAPI "pthread")
	ELSE()
		IF(NOT(KLAYGE_COMPILER_GCC AND (KLAYGE_COMPILER_VERSION STRGREATER "60")))
			SET(BOOST_COMPONENTS ${BOOST_COMPONENTS} filesystem)
		ENDIF()
	ENDIF()
ENDIF()
FIND_PACKAGE(Boost COMPONENTS ${BOOST_COMPONENTS})

IF(NOT Boost_LIBRARY_DIR)
	SET(Boost_LIBRARY_DIR ${Boost_LIBRARY_DIRS})
ENDIF()
