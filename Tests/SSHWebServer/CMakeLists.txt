set(TEST_SOURCES
    TestKnownHosts.cpp
)

foreach(source IN LISTS TEST_SOURCES)
    ladybird_test("${source}" SSHWebServer LIBS sshwebservice)
endforeach()
