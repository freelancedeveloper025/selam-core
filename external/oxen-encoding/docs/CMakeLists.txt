find_program(DOXYGEN doxygen)
if (NOT DOXYGEN)
    message(STATUS "Documentation generation disabled (doxygen not found)")
    return()
endif()
find_program(SPHINX_BUILD sphinx-build)
if (NOT SPHINX_BUILD)
    message(STATUS "Documentation generation disabled (sphinx-build not found)")
    return()
  endif()
  
set(selamc_doc_sources "${DOCS_SRC}")
string(REPLACE ";" " " selamec_doc_sources_spaced "${selamc_doc_sources}")

add_custom_command(
    OUTPUT doxyxml/index.xml
    COMMAND ${DOXYGEN} Doxyfile
    DEPENDS
        ${CMAKE_CURRENT_BINARY_DIR}/Doxyfile
        ${selamc_doc_sources}
)

add_custom_command(
    OUTPUT html/index.html
    COMMAND ${SPHINX_BUILD} -j auto
        -Dbreathe_projects.selamc=${CMAKE_CURRENT_BINARY_DIR}/doxyxml
        -Dversion=${selamc_VERSION} -Drelease=${selamc_VERSION}
        -Aversion=${selamc_VERSION} -Aversions=${selamc_VERSION_MAJOR},${selamc_VERSION_MINOR},${selamc_VERSION_PATCH}
        -b html
        ${CMAKE_CURRENT_BINARY_DIR} ${CMAKE_CURRENT_BINARY_DIR}/html
    DEPENDS
        ${CMAKE_CURRENT_BINARY_DIR}/index.rst
        ${CMAKE_CURRENT_BINARY_DIR}/conf.py
        ${CMAKE_CURRENT_BINARY_DIR}/doxyxml/index.xml
)
add_custom_target(doc DEPENDS html/index.html)
configure_file(conf.py.in conf.py @ONLY)
configure_file(Doxyfile.in Doxyfile @ONLY)
configure_file(index.rst index.rst COPYONLY)
