cmake_minimum_required(VERSION 3.16)

project(PhaseIII LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Geant4 REQUIRED)
include(${Geant4_USE_FILE})

add_executable(phase3_sim
  src/physics/BoundaryUtils.cc
  src/physics/DetectorSD.cc
  src/physics/PhotonPrimaryInfo.cc
  src/physics/PrimaryGeneratorAction.cc
  src/physics/RunAction.cc
  src/physics/SteppingAction.cc
  src/physics/TrackingAction.cc
  src/geometry/DetectorConstruction.cc
  src/geometry/DetectorConstructionUtils.cc
  src/geometry/DetectorConstructionMaterials.cc
  src/geometry/DetectorConstructionSurfaces.cc
  src/geometry/DetectorConstructionBuild.cc
  src/config/PhaseIIIConfig.cc
  src/io/PhaseIIIOutput.cc
  src/physics/PhysicsList.cc
  src/sources/PhotonSource.cc
  src/runtime/main.cc
)

target_include_directories(phase3_sim PRIVATE
  ${PROJECT_SOURCE_DIR}/src
  ${PROJECT_SOURCE_DIR}/src/config
  ${PROJECT_SOURCE_DIR}/src/geometry
  ${PROJECT_SOURCE_DIR}/src/io
  ${PROJECT_SOURCE_DIR}/src/physics
  ${PROJECT_SOURCE_DIR}/src/runtime
  ${PROJECT_SOURCE_DIR}/src/sources
  ${PROJECT_SOURCE_DIR}/../dependencies
)

target_link_libraries(phase3_sim ${Geant4_LIBRARIES})
