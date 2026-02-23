#include "Actions.hh"

#include "G4ios.hh"

namespace phase3 {

PhotonPrimaryInfo::PhotonPrimaryInfo(int event_id, int track_id)
    : event_id_(event_id), track_id_(track_id) {}

int PhotonPrimaryInfo::event_id() const {
  return event_id_;
}

int PhotonPrimaryInfo::track_id() const {
  return track_id_;
}

void PhotonPrimaryInfo::Print() const {
  G4cout << "PhotonPrimaryInfo event=" << event_id_
         << " track=" << track_id_ << G4endl;
}

}  // namespace phase3
