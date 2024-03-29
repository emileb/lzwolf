set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_custom_target(revision_check ALL
	COMMAND updaterevision ${CMAKE_CURRENT_BINARY_DIR}/hginfo.h
	WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
	DEPENDS updaterevision
)

set(PLATFORM_EXE_FLAGS "")

include(CheckCXXSourceCompiles)
include(CheckFunctionExists)
include(FindPkgConfig)

if(GPL)
	add_definitions(-DUSE_GPL)
	set(OPL_SOURCES dosbox/dbopl.cpp)

	if(USE_LIBTEXTSCREEN)
		add_definitions(-DUSE_TEXTSCREEN)
		include_directories(${LZWolf_SOURCE_DIR}/textscreen)
		set(EXTRA_LIBRARIES textscreen)
	else()
		set(EXTRA_LIBRARIES)
	endif()
else()
	set(OPL_SOURCES mame/fmopl.cpp)
	set(EXTRA_LIBRARIES)
endif()

check_function_exists(stricmp STRICMP_EXISTS)
check_function_exists(strnicmp STRNICMP_EXISTS)
check_function_exists(atoll ATOLL_EXISTS)
if(NOT STRICMP_EXISTS)
	add_definitions(-Dstricmp=strcasecmp)
endif()
if(NOT STRNICMP_EXISTS)
	add_definitions(-Dstrnicmp=strncasecmp)
endif()
if(NOT ATOLL_EXISTS)
	add_definitions(-Datoll=_atoi64)
endif()

check_cxx_source_compiles(
	"#include <stdarg.h>
	int main() { va_list list1, list2; va_copy(list1, list2); return 0; }"
	HAS_VA_COPY )
if( NOT HAS_VA_COPY )
	check_cxx_source_compiles(
		"#include <stdarg.h>
		int main() { va_list list1, list2; __va_copy(list1, list2); return 0; }"
		HAS___VA_COPY )
	if( HAS___VA_COPY )
		add_definitions( -Dva_copy=__va_copy )
	else()
		add_definitions( -DNO_VA_COPY )
	endif()
endif()

if( ZD_CMAKE_COMPILER_IS_GNUCXX_COMPATIBLE )
	if( CMAKE_CXX_COMPILER_ID STREQUAL "GNU" AND NOT CMAKE_CXX_COMPILER_VERSION VERSION_LESS 7.0 )
		set( CMAKE_CXX_FLAGS "-Wno-implicit-fallthrough ${CMAKE_CXX_FLAGS}")
	endif()
	if( CMAKE_CXX_COMPILER_ID STREQUAL "GNU" AND NOT CMAKE_CXX_COMPILER_VERSION VERSION_LESS 4.4 )
		set( CMAKE_CXX_FLAGS "-Wno-unused-result ${CMAKE_CXX_FLAGS}" )
	endif()
	set( CMAKE_CXX_FLAGS "-Wall -Wextra -Wno-unused -Wno-unused-parameter -Wno-missing-field-initializers ${CMAKE_CXX_FLAGS}" )
endif()

#SET(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -pg")
#SET(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -pg")
#SET(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -pg")

if(WIN32)
	set(NO_GTK ON)
	set(EXTRA_LIBRARIES ${EXTRA_LIBRARIES} comctl32)
	add_definitions(-DWINDOWS -DNO_STDIO_REDIRECT)

	# RC file
	set(EXTRA_FILES ${LZWolf_SOURCE_DIR}/src/win32/windows.rc)

	# Fix stat in v140_xp (broken in RTM and Update 1 so far)
	if( MSVC AND MSVC_VERSION EQUAL 1900 AND CMAKE_GENERATOR_TOOLSET STREQUAL "v140_xp" )
		add_definitions(-D_stat64i32=VS14Stat)
		set(EXTRA_FILES ${EXTRA_FILES} win32/vs2015hack.cpp)
	endif( MSVC AND MSVC_VERSION EQUAL 1900 AND CMAKE_GENERATOR_TOOLSET STREQUAL "v140_xp" )

	set(PLATFORM_EXE_FLAGS WIN32)

	set(EXTRA_FILES
		${EXTRA_FILES}
		win32/i_crash.cpp
		win32/i_main.cpp
		win32/wl_iwad_win32.cpp
	)
elseif(APPLE OR ANDROID)
	set(NO_GTK ON)
else()
	option( NO_GTK "Disable GTK+ dialogs (Not applicable to Windows)" )

	# Use GTK+ for the IWAD picker, if available.
	if( NOT NO_GTK )
		pkg_check_modules( GTK2 gtk+-2.0 )
		if( GTK2_FOUND )
			set( EXTRA_LIBRARIES ${EXTRA_LIBRARIES} ${GTK2_LIBRARIES} )
			include_directories( ${GTK2_INCLUDE_DIRS} )
		else()
			set( NO_GTK ON )
		endif()
	endif()
endif()

if(NO_GTK)
	add_definitions(-DNO_GTK=1)
endif()

if(APPLE)
	set(EXTRA_FILES
		macosx/filesys.mm
		macosx/wl_iwad_picker_cocoa.mm
	)

	if(FORCE_SDL12)
		set(EXTRA_FILES ${EXTRA_FILES} macosx/SDLMain.m)
	endif()

	include(${CMAKE_CURRENT_SOURCE_DIR}/macosx/install.txt)

	option(BUILD_BUNDLE "Build a app bundle on Mac OS X" OFF)
	if(BUILD_BUNDLE)
		set(PLATFORM_EXE_FLAGS MACOSX_BUNDLE)
	endif(BUILD_BUNDLE)
elseif(ANDROID)
	set(EXTRA_FILES
		android/android-jni.cpp
		android/in_android.cpp
		android/wl_iwad_picker_android.cpp
	)

	include_directories(${TOUCHCONTROLS_INCLUDE_DIR} ${SIGCPP_INCLUDE_DIR} ${TINYXML_INCLUDE_DIR} ${LIBPNG_INCLUDE_DIR})

	set(EXTRA_LIBRARIES ${EXTRA_LIBRARIES} ${TOUCHCONTROLS_LIBRARY} GLESv1_CM log)
endif()

# Derive version info and generate header
string(TOUPPER "${PRODUCT_NAME}" PRODUCT_SIGNATURE)
string(TOLOWER "${PRODUCT_NAME}" PRODUCT_BINARY)
math(EXPR VERSION_INTEGER "(${VERSION_MAJOR}<<20) | (${VERSION_MINOR}<<8) | ${VERSION_PATCH}")
configure_file(versiondefs.h.in versiondefs.h)

# Glob for the header files so project generation can include them
file(GLOB HEADER_FILES
	g_shared/*.h
	g_wolf/*.h
	r_data/*.h
	resourcefiles/*.h
	sfmt/*.h
	textures/*.h
	thingdef/*.h
	*.h
)
list(APPEND HEADER_FILES ${CMAKE_CURRENT_BINARY_DIR}/versiondefs.h)

set(SOURCE_FILES
	g_blake/a_smartanim.cpp
	g_blake/a_barrier.cpp
	g_blake/blake_sbar.cpp
	g_blake/blakeaog_sbar.cpp
	g_blake/bibendovsky.cpp
	g_shared/a_armor.cpp
	g_shared/a_action.cpp
	g_shared/a_ambient.cpp
	g_shared/a_deathcam.cpp
	g_shared/a_inventory.cpp
	g_shared/a_keys.cpp
	g_shared/a_patrolpoint.cpp
	g_shared/a_playerpawn.cpp
	g_shared/a_randomspawner.cpp
	g_wolf/a_spearofdestiny.cpp
	g_wolf/wolf_sbar.cpp
	r_2d/r_draw.cpp
	r_2d/r_drawt.cpp
	r_2d/r_main.cpp
	r_2d/r_things.cpp
	r_data/colormaps.cpp
	r_data/r_translate.cpp
	r_data/renderstyle.cpp
	resourcefiles/resourcefile.cpp
	resourcefiles/ancientzip.cpp
	resourcefiles/file_7z.cpp
	resourcefiles/file_audiot.cpp
	resourcefiles/file_directory.cpp
	resourcefiles/file_gamemaps.cpp
	resourcefiles/file_grp.cpp
	resourcefiles/file_pak.cpp
	resourcefiles/file_rff.cpp
	resourcefiles/file_rtl.cpp
	resourcefiles/file_lump.cpp
	resourcefiles/file_macbin.cpp
	resourcefiles/file_vgagraph.cpp
	resourcefiles/file_vswap.cpp
	resourcefiles/file_wad.cpp
	resourcefiles/file_zip.cpp
	resourcefiles/wolfmapcommon.cpp
	sfmt/SFMT.cpp
	textures/anim_switches.cpp
	textures/animations.cpp
	textures/automaptexture.cpp
	textures/bitmap.cpp
	textures/ddstexture.cpp
	textures/emptytexture.cpp
	textures/flattexture.cpp
	textures/imgztexture.cpp
	textures/jpegtexture.cpp
	textures/machudtexture.cpp
	textures/multipatchtexture.cpp
	textures/patchtexture.cpp
	textures/picttexture.cpp
	textures/pcxtexture.cpp
	textures/pngtexture.cpp
	textures/rawpagetexture.cpp
	textures/rottflattexture.cpp
	textures/solidtexture.cpp
	textures/texture.cpp
	textures/texturemanager.cpp
	textures/tgatexture.cpp
	textures/warptexture.cpp
	textures/wolfrawtexture.cpp
	textures/wolfshapetexture.cpp
	thingdef/thingdef.cpp
	thingdef/thingdef_codeptr.cpp
	thingdef/thingdef_expression.cpp
	thingdef/thingdef_parse.cpp
	thingdef/thingdef_properties.cpp
	thingdef/thingdef_type.cpp
	actor.cpp
	am_map.cpp
	cmdlib.cpp
	colormatcher.cpp
	config.cpp
	c_console.cpp
	c_consolebuffer.cpp
	c_cvars.cpp
	c_dispatch.cpp
	c_bind.cpp
	dobject.cpp
	dobjgc.cpp
	farchive.cpp
	files.cpp
	filesys.cpp
	filesys_steam.cpp
	g_conversation.cpp
	g_intermission.cpp
	g_mapinfo.cpp
	gamemap.cpp
	gamemap_planes.cpp
	gamemap_uwmf.cpp
	hginfo.cpp
	id_ca.cpp
	id_in.cpp
	id_sd.cpp
	id_us_1.cpp
	id_vh.cpp
	id_vl.cpp
	language.cpp
	lnspec.cpp
	lumpremap.cpp
	mapedit.cpp
	m_alloc.cpp
	m_argv.cpp
	m_classes.cpp
	m_random.cpp
	m_png.cpp
	name.cpp
	p_switch.cpp
	r_sprites.cpp
	scanner.cpp
	sdlvideo.cpp
	sndinfo.cpp
	sndseq.cpp
	thinker.cpp
	uwmfdoc.cpp
	v_draw.cpp
	v_font.cpp
	v_palette.cpp
	v_pfx.cpp
	v_text.cpp
	v_video.cpp
	w_wad.cpp
	weaponslots.cpp
	wl_act2.cpp
	wl_agent.cpp
	wl_atmos.cpp
	wl_cloudsky.cpp
	wl_debug.cpp
	wl_dir3dspr.cpp
	wl_draw.cpp
	wl_floorceiling.cpp
	wl_game.cpp
	wl_inter.cpp
	wl_iwad.cpp
	wl_loadsave.cpp
	wl_main.cpp
	wl_menu.cpp
	wl_net.cpp
	wl_parallax.cpp
	wl_atmos.cpp
	wl_play.cpp
	wl_state.cpp
	wl_text.cpp
	zstrformat.cpp
	zstring.cpp
)

if(NOT ANDROID)
	add_executable(lzwolf ${PLATFORM_EXE_FLAGS}
		${HEADER_FILES}
		${OPL_SOURCES}
		${SOURCE_FILES}
		${EXTRA_FILES}
	)
else()
	add_library(lzwolf SHARED
		${HEADER_FILES}
		${OPL_SOURCES}
		${SOURCE_FILES}
		${EXTRA_FILES}
	)

	# Link test
	add_executable(link-test android/link-test.cpp)
	target_link_libraries(link-test lzwolf)
endif()

add_dependencies(lzwolf revision_check)
target_link_libraries(lzwolf ${EXTRA_LIBRARIES} SDL2::SDL2_mixer SDL2::SDL2_net SDL2::SDL2 ${ZLIB_LIBRARY} ${BZIP2_LIBRARIES} ${JPEG_LIBRARIES} lzma gdtoa)
target_include_directories(lzwolf PRIVATE
	${ZLIB_INCLUDE_DIR}
	${BZIP2_INCLUDE_DIR}
	${JPEG_INCLUDE_DIR}
	${CMAKE_CURRENT_BINARY_DIR}
	${CMAKE_CURRENT_SOURCE_DIR}
	${CMAKE_CURRENT_SOURCE_DIR}/g_shared
	${LZMA_INCLUDE_DIR}
	${LZWolf_SOURCE_DIR}/deps/gdtoa
	${CMAKE_BINARY_DIR}/deps/gdtoa
	${LZWolf_SOURCE_DIR}/deps/eigen
)

set_target_properties(lzwolf PROPERTIES
	RUNTIME_OUTPUT_DIRECTORY ${OUTPUT_DIR}
	# On Linux look for any libraries we link against in the executable directory.
	INSTALL_RPATH "\$ORIGIN"
	BUILD_WITH_INSTALL_RPATH ON
)

# Install
if(NOT ANDROID)
	install(TARGETS lzwolf BUNDLE DESTINATION ${OUTPUT_DIR} RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR} COMPONENT Runtime)

	if(NOT WIN32 AND NOT APPLE)
		install(FILES "posix/lzwolf.desktop" DESTINATION "${CMAKE_INSTALL_DATAROOTDIR}/applications")
		install(FILES "posix/icon.svg" DESTINATION "${CMAKE_INSTALL_DATAROOTDIR}/icons/hicolor/scalable/apps" RENAME "lzwolf.svg")
	endif()
endif()

# Project file source groupings
source_group("Sources" REGULAR_EXPRESSION "^${CMAKE_CURRENT_SOURCE_DIR}/.+")
source_group("Game Objects\\Decorate" REGULAR_EXPRESSION "^${CMAKE_CURRENT_SOURCE_DIR}/thingdef/.+")
source_group("Game Objects\\Shared" REGULAR_EXPRESSION "^${CMAKE_CURRENT_SOURCE_DIR}/g_shared/.+")
source_group("Game Objects\\Wolf3D" REGULAR_EXPRESSION "^${CMAKE_CURRENT_SOURCE_DIR}/g_wolf/.+")
source_group("Game Objects" REGULAR_EXPRESSION "^${CMAKE_CURRENT_SOURCE_DIR}/(actor|dobj|thinker).+")
source_group("OPL Emulator\\DOSBox" REGULAR_EXPRESSION "^${CMAKE_CURRENT_SOURCE_DIR}/dosbox/.+")
source_group("OPL Emulator\\MAME" REGULAR_EXPRESSION "^${CMAKE_CURRENT_SOURCE_DIR}/mame/.+")
source_group("Renderer" REGULAR_EXPRESSION "^${CMAKE_CURRENT_SOURCE_DIR}/(r_|wl_draw|wl_floorceiling|wl_parallax|wl_atmos|wl_cloudsky).+")
source_group("Renderer\\2D Drawing" REGULAR_EXPRESSION "^${CMAKE_CURRENT_SOURCE_DIR}/r_2d/.+")
source_group("Renderer\\Data" REGULAR_EXPRESSION "^${CMAKE_CURRENT_SOURCE_DIR}/r_data/.+")
source_group("Resource Files" REGULAR_EXPRESSION "^${CMAKE_CURRENT_SOURCE_DIR}/resourcefiles/.+")
source_group("SFMT" REGULAR_EXPRESSION "^${CMAKE_CURRENT_SOURCE_DIR}/sfmt/.+")
source_group("Textures" REGULAR_EXPRESSION "^${CMAKE_CURRENT_SOURCE_DIR}/textures/.+")
source_group("Win32" REGULAR_EXPRESSION "^${CMAKE_CURRENT_SOURCE_DIR}/win32/.+")
