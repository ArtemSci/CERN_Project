#include "Actions.hh"

#include "G4Event.hh"
#include "Randomize.hh"

namespace phase1 {

EventAction::EventAction(uint64_t seed) : seed_(seed) {}

void EventAction::BeginOfEventAction(const G4Event* event) {
  const uint64_t event_seed = seed_ + static_cast<uint64_t>(event->GetEventID());
  G4Random::setTheSeed(event_seed);
}

}  // namespace phase1

