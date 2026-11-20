#pragma once

// Lets the Now Playing screen receive `CMD:NP ...` lines from the USB serial port. main.cpp reads the lines
// (it already handles `CMD:SCREENSHOT`) and passes the ones that start with "NP " to the registered
// handler. There is one handler and one prefix; a second consumer would need a prefix-keyed registry.
namespace serialcmd {

using Handler = void (*)(void* context, const char* command);

// One handler at a time; pass nullptr to remove it. Called on the main loop task.
void setHandler(Handler handler, void* context);
// Called by main.cpp with the command text after `CMD:` ("NP ..."), trimmed. Ignored when no handler is set.
void dispatch(const char* command);

}  // namespace serialcmd
