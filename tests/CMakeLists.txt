add_executable(diorascii_tests test_main.cpp)
target_link_libraries(diorascii_tests PRIVATE diorascii_core diorascii_term)
diorascii_warnings(diorascii_tests)
add_test(NAME diorascii_tests COMMAND diorascii_tests)

# The binary reports a version that CMake took from the changelog (or the release tag).
add_test(NAME diorascii_reports_version COMMAND diorascii --version)
string(REGEX REPLACE "([.+])" "\\\\\\1" DIORASCII_VERSION_REGEX "${DIORASCII_VERSION_STRING}")
set_tests_properties(diorascii_reports_version PROPERTIES
  PASS_REGULAR_EXPRESSION "^diorascii ${DIORASCII_VERSION_REGEX}")
