#pragma once
#include <cstddef>
#include <cstdint>
#include <stdexcept>

namespace terraflux::replay {
// Call once per 60 Hz engine tick. A 0.35-second source-frame interval is
// exactly 84 units of 1/240 second. Rates add 1,2,4,8,16 units per tick.
class ReplayClock {
 public:
  explicit ReplayClock(std::size_t frames):count_(frames),playing_(frames>1) {
    if(!frames || frames>720)throw std::invalid_argument("replay clock requires 1..720 frames");
  }
  static std::uint32_t speed_units(double speed) {
    if(speed==0.25)return 1;
    if(speed==0.5)return 2;
    if(speed==1)return 4;
    if(speed==2)return 8;
    if(speed==4)return 16;
    throw std::invalid_argument("replay speed must be 0.25, 0.5, 1, 2 or 4");
  }
  void set_speed(double speed) {units_=speed_units(speed);}
  double speed() const {return units_/4.0;}
  const char* speed_label() const {
    switch(units_){case 1:return "0.25x";case 2:return "0.5x";case 4:return "1x";case 8:return "2x";default:return "4x";}
  }
  void tick() {
    if(!playing_)return;
    phase_+=units_;
    if(phase_>=84) {
      phase_-=84;++frame_;
      if(frame_==count_-1){playing_=false;phase_=0;}
    }
  }
  void seek(std::size_t frame) {
    if(frame>=count_)throw std::out_of_range("replay frame out of range");
    frame_=frame;phase_=0;playing_=false;
  }
  void set_playing(bool value) {
    const bool next=value && frame_<count_-1;
    if(next==playing_)return;
    playing_=next;phase_=0;
  }
  void toggle() {
    if(playing_)set_playing(false);
    else {if(frame_==count_-1)frame_=0;set_playing(true);}
  }
  void step_forward() {seek(frame_<count_-1?frame_+1:frame_);}
  void step_back() {seek(frame_?frame_-1:0);}
  std::size_t frame() const {return frame_;}
  bool playing() const {return playing_;}
  double pending_seconds() const {return phase_/240.0;}
 private:
  std::size_t count_,frame_=0;
  std::uint32_t units_=4,phase_=0;
  bool playing_;
};
} // namespace terraflux::replay
