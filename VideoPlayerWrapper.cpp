#include "pch.h"

#include "VideoPlayerWrapper.h"

using namespace LibVDWUP::Video;

VideoPlayerWrapper::VideoPlayerWrapper() { 
	m_pVideoPlayer = new VideoPlayer();
}

void VideoPlayerWrapper::PlayPauseVideo() {
        m_pVideoPlayer->PlayPauseVideo();
}

void VideoPlayerWrapper::OpenURL(Platform::String ^ sURL) {
        const WCHAR* url = sURL->Data();
        m_pVideoPlayer->OpenURL(url);
}

void VideoPlayerWrapper::SetPosition(
    Windows::Foundation::TimeSpan position) {
        m_pVideoPlayer->SetPosition(position.Duration);
}

long long VideoPlayerWrapper::GetDuration() {
        return m_pVideoPlayer->GetDuration();
}
