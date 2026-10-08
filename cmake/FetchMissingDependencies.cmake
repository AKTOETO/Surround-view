include_guard(GLOBAL)

include(FetchContent)

function(sv_fetchcontent_requirements package_name source_override)
  if(NOT SV_FETCH_MISSING_DEPS)
    message(FATAL_ERROR
      "${package_name} was not found. Install its development package, or configure with "
      "-DSV_FETCH_MISSING_DEPS=ON to fetch and build the pinned source dependency.")
  endif()
  if(FETCHCONTENT_FULLY_DISCONNECTED)
    if(NOT DEFINED ${source_override} OR "${${source_override}}" STREQUAL "")
      message(FATAL_ERROR
        "${package_name} is missing and FETCHCONTENT_FULLY_DISCONNECTED=ON. "
        "Set ${source_override} to a prepared source tree or install the system development package.")
    endif()
  endif()
endfunction()

function(sv_find_glm)
  find_package(glm CONFIG QUIET)
  if(glm_FOUND)
    message(STATUS "Using system GLM ${glm_VERSION}")
    return()
  endif()

  sv_fetchcontent_requirements(GLM FETCHCONTENT_SOURCE_DIR_GLM)
  message(STATUS "GLM package not found; fetching GLM 1.0.1")
  FetchContent_Declare(glm
    GIT_REPOSITORY https://github.com/g-truc/glm.git
    GIT_TAG 1.0.1
    GIT_SHALLOW TRUE
    GIT_PROGRESS TRUE)
  FetchContent_MakeAvailable(glm)
endfunction()

function(sv_find_opencv)
  find_package(OpenCV CONFIG QUIET)
  if(OpenCV_FOUND AND NOT OpenCV_VERSION VERSION_LESS 4.5)
    unset(OpenCV_LIBS)
    unset(OpenCV_INCLUDE_DIRS)
    if(OpenCV_VERSION VERSION_LESS 5)
      find_package(OpenCV CONFIG QUIET COMPONENTS core calib3d imgproc imgcodecs objdetect videoio)
    else()
      find_package(OpenCV CONFIG QUIET COMPONENTS core geometry calib imgproc imgcodecs objdetect videoio)
    endif()
    if(OpenCV_FOUND)
      message(STATUS "Using system OpenCV ${OpenCV_VERSION}")
      set(SV_OPENCV_LIBRARIES "${OpenCV_LIBS}" PARENT_SCOPE)
      set(SV_OPENCV_INCLUDE_DIRS "${OpenCV_INCLUDE_DIRS}" PARENT_SCOPE)
      return()
    endif()
  endif()

  sv_fetchcontent_requirements(OpenCV FETCHCONTENT_SOURCE_DIR_SV_OPENCV_SOURCE)
  message(STATUS "Usable OpenCV package not found; fetching and building OpenCV 4.10.0")
  set(FETCHCONTENT_QUIET OFF)
  FetchContent_Declare(sv_opencv_source
    GIT_REPOSITORY https://github.com/opencv/opencv.git
    GIT_TAG 4.10.0
    GIT_SHALLOW TRUE
    GIT_PROGRESS TRUE)
  FetchContent_GetProperties(sv_opencv_source)
  if(NOT sv_opencv_source_POPULATED)
    if(POLICY CMP0169)
      cmake_policy(SET CMP0169 OLD)
    endif()
    FetchContent_Populate(sv_opencv_source)
  endif()

  include(ExternalProject)
  set(_opencv_prefix "${CMAKE_BINARY_DIR}/_deps/opencv-install")
  set(_opencv_build "${CMAKE_BINARY_DIR}/_deps/opencv-build")
  set(_opencv_modules core imgproc imgcodecs videoio calib3d objdetect)
  set(_opencv_libraries)
  set(_opencv_byproducts)
  foreach(_module IN LISTS _opencv_modules)
    list(APPEND _opencv_libraries "opencv_${_module}")
    list(APPEND _opencv_byproducts "${_opencv_prefix}/${CMAKE_INSTALL_LIBDIR}/libopencv_${_module}.so")
  endforeach()

  set(_opencv_cmake_args
    "-DCMAKE_INSTALL_PREFIX=<INSTALL_DIR>"
    "-DCMAKE_INSTALL_LIBDIR=${CMAKE_INSTALL_LIBDIR}"
    "-DOPENCV_LIB_INSTALL_PATH=${CMAKE_INSTALL_LIBDIR}"
    "-DCMAKE_INSTALL_RPATH=\$ORIGIN"
    "-DCMAKE_BUILD_WITH_INSTALL_RPATH=ON"
    "-DCMAKE_INSTALL_RPATH_USE_LINK_PATH=OFF"
    "-DBUILD_LIST=core,imgproc,imgcodecs,videoio,calib3d,objdetect"
    "-DBUILD_SHARED_LIBS=ON"
    "-DBUILD_TESTS=OFF"
    "-DBUILD_PERF_TESTS=OFF"
    "-DBUILD_EXAMPLES=OFF"
    "-DBUILD_opencv_apps=OFF"
    "-DENABLE_CCACHE=OFF"
    "-DWITH_FFMPEG=OFF"
    "-DWITH_GSTREAMER=OFF"
    "-DWITH_GTK=OFF"
    "-DWITH_V4L=ON"
    "-DWITH_OPENCL=OFF"
    "-DWITH_IPP=OFF"
    "-DBUILD_IPP_IW=OFF"
    "-DBUILD_ITT=OFF"
    "-DWITH_CAROTENE=OFF"
    "-DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}")
  if(CMAKE_TOOLCHAIN_FILE)
    list(APPEND _opencv_cmake_args "-DCMAKE_TOOLCHAIN_FILE=${CMAKE_TOOLCHAIN_FILE}")
  endif()
  foreach(_var CMAKE_C_COMPILER CMAKE_CXX_COMPILER CMAKE_SYSROOT CMAKE_FIND_ROOT_PATH
      CMAKE_C_FLAGS CMAKE_CXX_FLAGS)
    if(DEFINED ${_var} AND NOT "${${_var}}" STREQUAL "")
      list(APPEND _opencv_cmake_args "-D${_var}=${${_var}}")
    endif()
  endforeach()

  ExternalProject_Add(sv-opencv-build
    SOURCE_DIR "${sv_opencv_source_SOURCE_DIR}"
    BINARY_DIR "${_opencv_build}"
    INSTALL_DIR "${_opencv_prefix}"
    CMAKE_GENERATOR "${CMAKE_GENERATOR}"
    CMAKE_ARGS ${_opencv_cmake_args}
    BUILD_BYPRODUCTS ${_opencv_byproducts}
    USES_TERMINAL_CONFIGURE TRUE
    USES_TERMINAL_BUILD TRUE
    USES_TERMINAL_INSTALL TRUE)
  install(DIRECTORY "${_opencv_prefix}/${CMAKE_INSTALL_LIBDIR}/"
    DESTINATION "${CMAKE_INSTALL_LIBDIR}/surround-view/opencv"
    FILES_MATCHING PATTERN "libopencv_*.so*")

  set(SV_OPENCV_FETCHED TRUE PARENT_SCOPE)
  set(SV_OPENCV_LIBRARIES "${_opencv_libraries}" PARENT_SCOPE)
  set(SV_OPENCV_INCLUDE_DIRS
    "${_opencv_prefix}/include/opencv4;${_opencv_build};${_opencv_build}/opencv2"
    PARENT_SCOPE)
  set(SV_OPENCV_EXTERNAL_TARGET sv-opencv-build PARENT_SCOPE)
  set(SV_OPENCV_LIBRARY_DIR "${_opencv_prefix}/${CMAKE_INSTALL_LIBDIR}" PARENT_SCOPE)
endfunction()
