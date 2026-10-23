# Writes a C++ header with the text of a file as a string constant.
#   cmake -DINPUT=<file> -DOUTPUT=<header> -DNAME=<constant> -P EmbedText.cmake
# Used for scripts the renderer adds to pages (the JSON viewer, 006).
file(READ "${INPUT}" text)
set(delimiter "brouk_embedded")
string(FIND "${text}" ")${delimiter}\"" found)
if(NOT found EQUAL -1)
  message(FATAL_ERROR "${INPUT} contains the raw string delimiter")
endif()
file(WRITE "${OUTPUT}.tmp"
  "// Generated from ${INPUT}; do not edit.\n"
  "#pragma once\n"
  "inline constexpr char ${NAME}[] = R\"${delimiter}(${text})${delimiter}\";\n")
file(COPY_FILE "${OUTPUT}.tmp" "${OUTPUT}" ONLY_IF_DIFFERENT)
file(REMOVE "${OUTPUT}.tmp")
