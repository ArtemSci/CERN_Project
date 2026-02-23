cmake_minimum_required(VERSION 3.16)

project(PhaseI LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Geant4 REQUIRED)
include(${Geant4_USE_FILE})

add_executable(phase1_sim
  src/physics/EventAction.cc
  src/physics/PrimaryGeneratorAction.cc
  src/physics/RunAction.cc
  src/physics/StackingAction.cc
  src/physics/SteppingAction.cc
  src/physics/TrackingAction.cc
  src/geometry/DetectorConstruction.cc
  src/physics/FieldMap.cc
  src/io/OutputWriter.cc
  src/config/PhaseIConfig.cc
  src/physics/PhaseIField.cc
  src/physics/PhysicsList.cc
  src/physics/SurfaceRegistry.cc
  src/runtime/main.cc
)

target_include_directories(phase1_sim PRIVATE
  ${PROJECT_SOURCE_DIR}/src
  ${PROJECT_SOURCE_DIR}/src/config
  ${PROJECT_SOURCE_DIR}/src/geometry
  ${PROJECT_SOURCE_DIR}/src/io
  ${PROJECT_SOURCE_DIR}/src/physics
  ${PROJECT_SOURCE_DIR}/src/runtime
  ${PROJECT_SOURCE_DIR}/../dependencies
)

target_link_libraries(phase1_sim ${Geant4_LIBRARIES})
