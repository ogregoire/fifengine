# - Try to find OpenAL-Soft
# and define
#
#  OPENALSOFT_FOUND          - System has OpenAL-Soft
#  OPENALSOFT_INCLUDE_DIR    - The OpenAL-Soft include directories
#  OPENALSOFT_LIBRARY        - The OpenAL-Soft libraries

IF(NOT APPLE)
  find_path(OPENALSOFT_INCLUDE_DIR al.h
    PATHS
      /sw # Fink
      /opt/local # DarwinPorts
      /opt/csw # Blastwave
      /opt
      # Linux Include PATH
      /usr/include/AL/
      # Windows SDK Include PATH
      /include/OpenALSoft
  )

  find_library(OPENALSOFT_LIBRARY
    NAMES libopenal openal libOpenAL32 OpenAL32 OpenAL32.dll soft_oal.dll
    PATHS
      ${OPENALSOFT_INCLUDE_DIR}/../lib
      /usr/local/opt/openal-soft/lib
      /lib
      /usr/lib
  )
ENDIF(NOT APPLE)

IF (APPLE)
  find_path(OPENALSOFT_INCLUDE_DIR al.h alext.h
  PATHS
    /usr/local/opt/openal-soft/include/
  )
  find_library(OPENALSOFT_LIBRARY
  NAMES libopenal.dylib openal.dylib
  PATHS
    /usr/local/opt/openal-soft/lib
  )
ENDIF (APPLE)

include(FindPackageHandleStandardArgs)

find_package_handle_standard_args(OPENALSOFT REQUIRED_VARS OPENALSOFT_LIBRARY OPENALSOFT_INCLUDE_DIR)

mark_as_advanced(OPENALSOFT_INCLUDE_DIR OPENALSOFT_LIBRARY)
