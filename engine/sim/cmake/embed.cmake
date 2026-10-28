# Turns a binary file into a C++ array: cmake -DIN=file -DOUT=out.cpp -DNAME=kRelief -P embed.cmake
# A missing file embeds nothing (the continent then uses its procedural design).
set(bytes "")
set(size 0)
if(EXISTS "${IN}")
    file(READ "${IN}" hex HEX)
    string(LENGTH "${hex}" len)
    math(EXPR size "${len} / 2")
    string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," bytes "${hex}")
endif()
file(WRITE "${OUT}" "// Generated from ${IN} by embed.cmake. Do not edit.\n#include <cstddef>\nnamespace illyria::sim::data {\nextern const unsigned char ${NAME}[];\nextern const size_t ${NAME}Size;\nconst unsigned char ${NAME}[] = {${bytes} 0};\nconst size_t ${NAME}Size = ${size};\n}  // namespace illyria::sim::data\n")
