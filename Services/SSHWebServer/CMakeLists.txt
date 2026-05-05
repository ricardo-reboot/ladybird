find_package(libssh2 CONFIG REQUIRED)

set(SOURCES
    Service.cpp
)

add_library(sshwebservice STATIC ${SOURCES})
target_include_directories(sshwebservice PRIVATE ${LADYBIRD_SOURCE_DIR})
target_link_libraries(sshwebservice PUBLIC
    LibCore
    LibMain
    LibSSHWeb
    libssh2::libssh2
)

add_executable(SSHWebServer main.cpp)
target_link_libraries(SSHWebServer PRIVATE sshwebservice)
