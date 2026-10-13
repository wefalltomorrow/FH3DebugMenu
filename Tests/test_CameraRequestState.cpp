#include "CameraRequestState.hpp"
#include <iostream>
#include <cstdlib>
using namespace FH3Native;
struct Fake final : ICameraCommands {
    std::uint32_t mode = 3;
    std::uintptr_t owner = 42;
    bool registered = true;
    bool read = true;
    bool write = true;
    int writes=0;
    bool IsRegistered(std::uint32_t x) const noexcept override { return registered && (x==0 || x==3 || x==6); }
    bool ReadMode(std::uint32_t& x) const noexcept override { if(!read)return false; x=mode; return true; }
    bool WriteMode(std::uint32_t x) const noexcept override { if(!write)return false; ++const_cast<Fake*>(this)->writes; const_cast<Fake*>(this)->mode=x; return true; }
    std::uintptr_t OwnerId() const noexcept override {return owner;}
};
void need(bool ok, const char* msg) {if(!ok) {std::cerr<<"FAIL "<<msg<<"\n"; std::exit(1);}}
int main() {
  CameraRequestState s;
  Fake cam;
  need(s.OnGameFrame(cam)==CommandResult::NoRequest,"idle");
  s.Queue(NativeRequest::Restore);
  need(s.OnGameFrame(cam)==CommandResult::InvalidMode,"refuse restoration without snapshot");
  s.Queue(NativeRequest::Free); cam.read=false;
  need(s.OnGameFrame(cam)==CommandResult::Deferred && cam.writes==0,"defer failed get");
  cam.read=true;cam.registered=false;
  need(s.OnGameFrame(cam)==CommandResult::InvalidMode && cam.writes==0,"refuse unregistered");
  cam.registered=true;
  s.Queue(NativeRequest::Free);
  need(s.OnGameFrame(cam)==CommandResult::Applied,"free applied");
  need(cam.mode==6 && s.HasRestoreMode() && s.SavedMode()==3,"capture previous mode");
  s.Queue(NativeRequest::Free);
  need(s.OnGameFrame(cam)==CommandResult::Applied && s.SavedMode()==3,"do not overwrite restore snapshot");
  s.Queue(NativeRequest::Restore);cam.write=false;
  need(s.OnGameFrame(cam)==CommandResult::CallFailed && s.HasRestoreMode(),"failed restore retains saved mode");
  cam.write=true;
  need(s.OnGameFrame(cam)==CommandResult::RestoreApplied && cam.mode==3 && !s.HasRestoreMode(),"restore prior mode");
  s.Queue(NativeRequest::Free);
  need(s.OnGameFrame(cam)==CommandResult::Applied,"new snapshot");
  s.Queue(NativeRequest::Restore);cam.owner=43;
  need(s.OnGameFrame(cam)==CommandResult::InvalidMode,"never restore into changed owner");
  std::cout<<"PASS: 10 camera request state / restoration checks\n";
}
