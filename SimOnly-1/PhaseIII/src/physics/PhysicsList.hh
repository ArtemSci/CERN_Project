#pragma once

#include "G4VModularPhysicsList.hh"

namespace phase3 {

class PhysicsList : public G4VModularPhysicsList {
 public:
  PhysicsList();
  ~PhysicsList() override = default;
};

}  // namespace phase3
