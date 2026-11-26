add_test(NAME layout COMMAND python3 ${CMAKE_SOURCE_DIR}/tools/check_layout.py)
add_test(NAME psx_exe_header COMMAND python3 -m unittest discover -s ${CMAKE_SOURCE_DIR}/tests -p "test_*.py")

