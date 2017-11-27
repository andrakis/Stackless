# Properties->C/C++->General->Additional Include Directories
include_directories ("include")

include( CheckCXXCompilerFlag )
check_cxx_compiler_flag( "-std=c++14"   COMPILER_SUPPORTS_CXX14 )
if( COMPILER_SUPPORTS_CXX14 )
    if( CMAKE_COMPILER_IS_GNUCXX )
        set( CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=gnu++14" )
    else()
        set( CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++14" )
    endif()
else()
    # MSVC, On by default (if available)
endif()

# Set Properties->General->Configuration Type to Application(.exe)
# Creates stackless.exe with the listed sources
# Adds sources to the Solution Explorer
add_executable (stackless Stackless.cpp samples/Brainfck.cpp samples/SchemeReference.cpp samples/Scheme.cpp)

# Properties->Linker->Input->Additional Dependencies
#target_link_libraries (stackless  math)

# Creates a folder "executables" and adds target 
# project (stackless.vcproj) under it
set_property(TARGET stackless PROPERTY FOLDER "executables")

# Properties->General->Output Directory
set_target_properties(stackless PROPERTIES
                      RUNTIME_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR}/bin)

# Adds logic to INSTALL.vcproj to copy stackless.exe to destination directory
install (TARGETS stackless
         RUNTIME DESTINATION ${PROJECT_SOURCE_DIR}/_install)
