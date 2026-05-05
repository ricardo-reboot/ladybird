set(TEST_SOURCES
    TestVersion.cpp
)

foreach(source IN LISTS TEST_SOURCES)
    ladybird_test("${source}" LibSSHWeb LIBS LibSSHWeb)
endforeach()
