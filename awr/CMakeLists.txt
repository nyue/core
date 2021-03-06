cmake_minimum_required(VERSION 2.8.6)

if(BUILD_IN_TRILINOS)
  include(TribitsSubPackageMacros) 
  include(TribitsLibraryMacros) 
  tribits_subpackage(awr) 

  # THIS IS WHERE TRIBITS GETS HEADERS
  include_directories(${CMAKE_CURRENT_SOURCE_DIR})

  configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/awrConfig.cmake.in"
    "${CMAKE_BINARY_DIR}/awrConfig.cmake")

  #pkgconfig {
  set(prefix "${CMAKE_INSTALL_PREFIX}")
  set(exec_prefix "${CMAKE_INSTALL_PREFIX}/bin")
  set(libdir "${CMAKE_INSTALL_PREFIX}/lib")
  set(includedir "${CMAKE_INSTALL_PREFIX}/include")
  set(PACKAGE_VERSION "1.0")
  configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/libawr.pc.in"
    "${PROJECT_BINARY_DIR}/libawr.pc"
    @ONLY)
  #pkgconfig }

  #Base Sources
  set(SOURCES
    awr.cc
    awrProblem.cc
    awrProblemSetup.cc
    awrProblemAssemble.cc
    awrProblemSolve.cc
    awrPoisson.cc
    awrQoI.cc
    awrDomainIntegral.cc
    awrLinearSystem.cc)
  
  #Header
  set(HEADERS
    awr.h)

  #Library
  tribits_add_library(
    awr
    HEADERS ${HEADERS}
    SOURCES ${SOURCES})

  tribits_subpackage_postprocess()
endif(BUILD_IN_TRILINOS) 
