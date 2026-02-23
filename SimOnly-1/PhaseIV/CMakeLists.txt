cmake_minimum_required(VERSION 3.16)

project(PhaseIV LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Geant4 REQUIRED)
include(${Geant4_USE_FILE})

add_executable(phase4_sim
  src/processing/DigiProcessor.cc
  src/processing/DigiProcessorModel.cc
  src/processing/DigiProcessorWaveform.cc
  src/processing/DigiProcessorProcess.cc
  src/sources/HitSource.cc
  src/config/PhaseIVConfig.cc
  src/io/PhaseIVOutput.cc
  src/runtime/main.cc
)

target_include_directories(phase4_sim PRIVATE
  ${PROJECT_SOURCE_DIR}/src
  ${PROJECT_SOURCE_DIR}/src/config
  ${PROJECT_SOURCE_DIR}/src/io
  ${PROJECT_SOURCE_DIR}/src/processing
  ${PROJECT_SOURCE_DIR}/src/runtime
  ${PROJECT_SOURCE_DIR}/src/sources
  ${PROJECT_SOURCE_DIR}/../dependencies
)

target_link_libraries(phase4_sim ${Geant4_LIBRARIES})
