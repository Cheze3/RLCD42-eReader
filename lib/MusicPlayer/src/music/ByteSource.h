#pragma once

#include <cstddef>
#include <cstdint>

namespace music {

// Where an MP3 file's bytes come from. The firmware wraps an SD file; the host tests wrap a buffer.
class ByteSource {
 public:
  virtual ~ByteSource() = default;
  // Copies up to n bytes; returns the count, 0 at end of file, negative on error.
  virtual int read(uint8_t* dst, size_t n) = 0;
  virtual bool seek(size_t pos) = 0;
  virtual size_t size() const = 0;
};

}  // namespace music
