#include "SerialCommands.h"

namespace serialcmd {

namespace {
Handler currentHandler = nullptr;
void* currentContext = nullptr;
}  // namespace

void setHandler(Handler handler, void* context) {
  currentContext = context;
  currentHandler = handler;
}

void dispatch(const char* command) {
  if (currentHandler != nullptr) currentHandler(currentContext, command);
}

}  // namespace serialcmd
