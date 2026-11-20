"""Windows source for Now Playing: whatever Windows' media controls show (Apple Music for Windows,
Spotify, a browser tab, ...). Uses the WinRT "GlobalSystemMediaTransportControls" API through the
pywinrt packages (see requirements.txt). The WinRT imports are inside the methods so this file can be
imported, and its helpers tested, on any system.

Not yet run on a real Windows machine: see README.md.
"""

import asyncio
import time
from typing import Optional

# GlobalSystemMediaTransportControlsSessionPlaybackStatus values.
_PLAYING, _PAUSED, _CHANGING = 4, 5, 2


def state_name(status: int) -> str:
    """playing, paused or stopped, from the WinRT playback status number."""
    if status == _PLAYING:
        return "playing"
    if status in (_PAUSED, _CHANGING):
        return "paused"
    return "stopped"


def live_position(position: float, updated_at: float, now: float, playing: bool, duration: float) -> float:
    """Windows reports the position as it was at `updated_at`; while playing, add the time since."""
    if playing and updated_at > 0:
        position += max(0.0, now - updated_at)
    if duration > 0:
        position = min(position, duration)
    return max(0.0, position)


def _seconds(value) -> float:
    """A WinRT TimeSpan arrives as a timedelta."""
    try:
        return value.total_seconds()
    except AttributeError:
        return 0.0


class WindowsMedia:
    def __init__(self, track_type):
        self._track = track_type  # nowplaying.Track, passed in to avoid importing the main script

    async def _session(self):
        from winrt.windows.media.control import GlobalSystemMediaTransportControlsSessionManager as Manager
        manager = await Manager.request_async()
        return manager.get_current_session()

    def poll(self) -> Optional[object]:
        return asyncio.run(self._poll())

    async def _poll(self):
        session = await self._session()
        if session is None:
            return None
        info = session.get_playback_info()
        state = state_name(int(info.playback_status))
        if state == "stopped":
            return self._track("stopped")
        props = await session.try_get_media_properties_async()
        timeline = session.get_timeline_properties()
        duration = _seconds(timeline.end_time) - _seconds(timeline.start_time)
        updated = timeline.last_updated_time.timestamp() if timeline.last_updated_time else 0.0
        position = live_position(_seconds(timeline.position), updated, time.time(), state == "playing", duration)
        return self._track(state, props.title or "", props.artist or "", props.album_title or "",
                           position, max(duration, 0.0))

    def artwork(self) -> Optional[bytes]:
        """The cover as image file bytes (PNG or JPEG), or None."""
        return asyncio.run(self._artwork())

    async def _artwork(self):
        from winrt.windows.storage.streams import Buffer, InputStreamOptions
        session = await self._session()
        if session is None:
            return None
        props = await session.try_get_media_properties_async()
        if props.thumbnail is None:
            return None
        stream = await props.thumbnail.open_read_async()
        size = int(stream.size)
        if size <= 0:
            return None
        buffer = Buffer(size)
        result = await stream.read_async(buffer, size, InputStreamOptions.READ_AHEAD)
        return bytes(memoryview(result))  # the pywinrt buffer supports the buffer protocol

    def control(self, event: str) -> None:
        asyncio.run(self._control(event))

    async def _control(self, event: str) -> None:
        session = await self._session()
        if session is None:
            return
        if event == "PLAYPAUSE":
            await session.try_toggle_play_pause_async()
        elif event == "NEXT":
            await session.try_skip_next_async()
        elif event == "PREV":
            await session.try_skip_previous_async()
