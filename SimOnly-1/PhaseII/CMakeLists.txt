cmake_minimum_required(VERSION 3.16)

project(PhaseII LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Geant4 REQUIRED)
include(${Geant4_USE_FILE})

add_executable(phase2_sim
  src/generation/PhotonGenerator.cc
  src/generation/PhotonGeneratorOptics.cc
  src/generation/PhotonGeneratorGenerate.cc
  src/config/PhaseIIConfig.cc
  src/io/PhaseIIOutput.cc
  src/optics/OpticsModels.cc
  src/runtime/main.cc
)

target_include_directories(phase2_sim PRIVATE
  ${PROJECT_SOURCE_DIR}/src
  ${PROJECT_SOURCE_DIR}/src/config
  ${PROJECT_SOURCE_DIR}/src/generation
  ${PROJECT_SOURCE_DIR}/src/io
  ${PROJECT_SOURCE_DIR}/src/optics
  ${PROJECT_SOURCE_DIR}/src/runtime
  ${PROJECT_SOURCE_DIR}/../dependencies
)

target_link_libraries(phase2_sim ${Geant4_LIBRARIES})
