file(GLOB BASE_SRC "*.cpp" "*.hpp" "*.h" "../external/imgui/*.cpp")
file(GLOB BASE_HEADERS "*.hpp" "*.h")

set(KTX_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../external/ktx)
set(KTX_SOURCES
    ${KTX_DIR}/lib/texture.c
    ${KTX_DIR}/lib/hashlist.c
    ${KTX_DIR}/lib/checkheader.c
    ${KTX_DIR}/lib/swap.c
    ${KTX_DIR}/lib/memstream.c
    ${KTX_DIR}/lib/filestream.c
    ${KTX_DIR}/lib/vkloader.c)

add_library(base STATIC ${BASE_SRC} ${KTX_SOURCES})
if(WIN32)
    target_link_libraries(base ${Vulkan_LIBRARY} ${WINLIBS} ${FIDELITYFX_LIBRARY})
    target_include_directories(base PRIVATE "../external/fidelityfx-sdk/ffx-api/include")
	target_include_directories(base PRIVATE "../external/fidelityfx-sdk/ffx-api/include/ffx_api/")
	target_include_directories(base PRIVATE "../external/fidelityfx-sdk/ffx-api/src")
	target_include_directories(base PRIVATE "../external/fidelityfx-sdk/sdk/include")
	target_include_directories(base PRIVATE "../external/fidelityfx-sdk/sdk/src")
	target_include_directories(base PRIVATE "../external/fidelityfx-sdk/sdk/src/shared")
 else(WIN32)
    target_link_libraries(base ${Vulkan_LIBRARY} ${XCB_LIBRARIES} ${WAYLAND_CLIENT_LIBRARIES} ${CMAKE_THREAD_LIBS_INIT})
endif(WIN32)