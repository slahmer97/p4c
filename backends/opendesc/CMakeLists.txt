set (OPENDESC_HDRS
        opendesc.h
        midend.h
)

set (OPENDESC_SRCS
        opendesc.cpp
        midend.cpp
        Analyzer.cpp
        Analyzer.h
        CompletionAnalyzer.cpp
        CompletionAnalyzer.h
        TopLevelTypeRegistrar.cpp
        TopLevelTypeRegistrar.h
        RssHash.cpp
        RssHash.h
        ControlFlowGraph.cpp
        ControlFlowGraph.h
        SoftwareCostModel.cpp
        SoftwareCostModel.h
)
add_executable(opendesc ${OPENDESC_SRCS})
target_link_libraries(opendesc ${P4C_LIBRARIES} ${P4C_LIB_DEPS})
add_dependencies(opendesc ir-generated frontend)

install(TARGETS opendesc
        RUNTIME DESTINATION ${P4C_RUNTIME_OUTPUT_DIRECTORY})

file(RELATIVE_PATH CURRENT_BINARY_DIR_PATH_REL
        ${P4C_BINARY_DIR} ${CMAKE_CURRENT_BINARY_DIR})
file(RELATIVE_PATH P4C_BINARY_DIR_PATH_REL
        ${CMAKE_CURRENT_BINARY_DIR} ${P4C_BINARY_DIR})

add_custom_target(linkopendesc
        COMMAND ${CMAKE_COMMAND} -E create_symlink
        ${CURRENT_BINARY_DIR_PATH_REL}/opendesc
        ${P4C_BINARY_DIR}/opendesc
        COMMAND ${CMAKE_COMMAND} -E create_symlink
        ${P4C_BINARY_DIR_PATH_REL}/p4include
        ${CMAKE_CURRENT_BINARY_DIR}/p4include
        DEPENDS update_includes
)
add_dependencies(p4c_driver linkopendesc)