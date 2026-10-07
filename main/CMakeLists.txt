# 几何冲刺固件只编译 main.c 与 gd_*.c。
# 基线的 demo_*.c / ui_pixel*.c 仍保留在仓库里(主机测试会用到),但不进固件。
file(GLOB GD_SOURCES "${CMAKE_CURRENT_LIST_DIR}/gd_*.c")

idf_component_register(
    SRCS "main.c" ${GD_SOURCES}
    INCLUDE_DIRS "."
    REQUIRES bsp esp_timer nvs_flash esp_hw_support heap esp_partition
)

# 中文字库子集(tools/gen_gd_fonts.py 生成,见 assets/README)。
file(GLOB GD_FONT_SOURCES "${CMAKE_CURRENT_LIST_DIR}/../assets/fonts/gd_*.c")
target_sources(${COMPONENT_LIB} PRIVATE ${GD_FONT_SOURCES})

# 调试:GD_AUTOPLAY=1 时用通关录像自动游玩,在固定场景下测量性能(PRD §12)。
if(DEFINED ENV{GD_AUTOPLAY})
    target_compile_definitions(${COMPONENT_LIB} PRIVATE GD_AUTOPLAY=$ENV{GD_AUTOPLAY})
endif()

# 音乐包(tools/gen_gd_music.py 从本地 BGM 生成;原曲有版权,音乐包不进 git)。
# 存在时复制进构建目录、注册到 "music" 分区,idf.py merge-bin 会把它并入合并镜像;
# 不存在时照常构建,固件以静音模式运行。可用环境变量 GD_MUSIC_PACK 指定其他路径。
set(GD_MUSIC_SRC "${CMAKE_CURRENT_LIST_DIR}/../build/gd_music/gd_music.bin")
if(DEFINED ENV{GD_MUSIC_PACK})
    set(GD_MUSIC_SRC "$ENV{GD_MUSIC_PACK}")
endif()
if(EXISTS "${GD_MUSIC_SRC}")
    idf_build_get_property(gd_build_dir BUILD_DIR)
    configure_file("${GD_MUSIC_SRC}" "${gd_build_dir}/gd_music.bin" COPYONLY)
    esptool_py_flash_to_partition(flash "music" "${gd_build_dir}/gd_music.bin")
    message(STATUS "几何冲刺:音乐包 ${GD_MUSIC_SRC} 将写入 music 分区并并入合并镜像")
else()
    message(WARNING "几何冲刺:没有找到音乐包 ${GD_MUSIC_SRC},固件将以静音模式运行。"
                    "需要音乐时先运行 tools/gen_gd_music.py 再重新构建。")
endif()
