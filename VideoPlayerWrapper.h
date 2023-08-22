#pragma once

#include "pch.h"

#include "Video/VideoPlayer.h"

namespace LibVDWUP {
namespace Video {
public
ref class VideoPlayerWrapper sealed {
 public:
  VideoPlayerWrapper();

  void PlayPauseVideo();
  void OpenURL(Platform::String ^ sURL);
  void SetPosition(Windows::Foundation::TimeSpan position);
  long long GetDuration();
  bool GetIsPaused() { return m_pVideoPlayer->GetIsPaused(); }

 private:
  VideoPlayer* m_pVideoPlayer;

};
}  // namespace Video
}  // namespace LibVDWUP
