# Variables for generating version.h

set(PRODUCT_NAME "ECWolf")
if(APPLE OR WIN32)
	set(PRODUCT_DIRECTORY "${PRODUCT_NAME}")
else()
	string(TOLOWER "${PRODUCT_NAME}" PRODUCT_DIRECTORY)
endif()

set(VERSION_MAJOR 1)
set(VERSION_MINOR 3)
set(VERSION_PATCH 99999)
set(VERSION_STRING "${VERSION_MAJOR}.${VERSION_MINOR}.${VERSION_PATCH} LW build")
