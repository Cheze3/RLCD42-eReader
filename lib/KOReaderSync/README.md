# KOReaderSync (legacy name)

The KOReader sync client, credential store and progress comparison were removed from this firmware. What is
left here is still used, and the name is kept so existing includes and tests stay valid:

- `ProgressMapper`, `ChapterXPathResolver` and `CrossPointPosition.h`: map a reading position to and from a
  KOReader-style XPath plus percentage. The reader uses them for bookmarks and saved positions.
- `KOReaderDocumentId`: the binary partial-MD5 of a book file. It is the `document` id in plugin
  reader-session events (see `docs/plugin-events.md`). That value is a contract with plugins: do not change
  how it is calculated.

A rename (for example to `ReaderPosition`) is possible but touches includes in the reader and the tests.
