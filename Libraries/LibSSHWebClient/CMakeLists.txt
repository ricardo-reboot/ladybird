compile_ipc(${LADYBIRD_SOURCE_DIR}/Services/SSHWebServer/SSHWebClient.ipc ${CMAKE_BINARY_DIR}/Services/SSHWebServer/SSHWebClientEndpoint.h)
compile_ipc(${LADYBIRD_SOURCE_DIR}/Services/SSHWebServer/SSHWebServer.ipc ${CMAKE_BINARY_DIR}/Services/SSHWebServer/SSHWebServerEndpoint.h)

set(SOURCES
    Client.cpp
)

set(GENERATED_SOURCES
    ../../Services/SSHWebServer/SSHWebClientEndpoint.h
    ../../Services/SSHWebServer/SSHWebServerEndpoint.h
)

ladybird_lib(LibSSHWebClient sshwebclient)
target_link_libraries(LibSSHWebClient PUBLIC LibCore LibIPC LibURL)
