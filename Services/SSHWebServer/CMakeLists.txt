find_package(libssh2 CONFIG REQUIRED)

add_executable(SSHWebServer main.cpp)
target_link_libraries(SSHWebServer PRIVATE
    LibCore
    LibMain
    LibSSHWeb
    libssh2::libssh2
)
