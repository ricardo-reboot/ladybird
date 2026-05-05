set(SOURCES
    Manifest.cpp
    Packfile.cpp
    URL.cpp
    Version.cpp
)

ladybird_lib(LibSSHWeb sshweb)
target_link_libraries(LibSSHWeb PRIVATE LibCompress)
