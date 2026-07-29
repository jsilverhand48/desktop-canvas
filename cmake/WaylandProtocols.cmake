# canvas_add_wayland_protocols(<target> <xml files...>)
#
# Runs wayland-scanner over each protocol XML, generating a client header and
# private code file into the build tree, and adds them to the target. The
# generated headers land in <build>/wayland-protocols and that directory is
# added to the target include path.
find_program(WAYLAND_SCANNER_EXECUTABLE wayland-scanner REQUIRED)

function(canvas_add_wayland_protocols target)
    set(out_dir "${CMAKE_CURRENT_BINARY_DIR}/wayland-protocols")
    file(MAKE_DIRECTORY "${out_dir}")
    foreach(xml IN LISTS ARGN)
        get_filename_component(base "${xml}" NAME_WE)
        set(header "${out_dir}/${base}-client-protocol.h")
        set(code "${out_dir}/${base}-protocol.c")
        add_custom_command(
            OUTPUT "${header}"
            COMMAND "${WAYLAND_SCANNER_EXECUTABLE}" client-header "${xml}" "${header}"
            DEPENDS "${xml}" VERBATIM)
        add_custom_command(
            OUTPUT "${code}"
            COMMAND "${WAYLAND_SCANNER_EXECUTABLE}" private-code "${xml}" "${code}"
            DEPENDS "${xml}" VERBATIM)
        target_sources("${target}" PRIVATE "${header}" "${code}")
    endforeach()
    target_include_directories("${target}" PRIVATE "${out_dir}")
endfunction()
