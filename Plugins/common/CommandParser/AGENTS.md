# CommandParser - Agent entry

All AI agents read this file before entering this plugin.

## Design constraints (strict)

- Responsibility: command-line parsing. The host parses once during initialization; plugins read startup parameters from here uniformly - do not re-parse argc/argv.
- Dependencies only go through `.cplugin` `Dependencies`; include `<CommandParser.h>`, no cross-directory relative includes.
- Implementation notes:
  - `TSingleton<FCommandParser>`; `Get()` is defined in `Private/CommandParser.cpp` (process-unique inside CommandParser.dll).
  - Backed by CLI11 (engine third-party); values are stored as strings, `GetBool/GetInt` parse on read, defaults fall back to 0/false/empty string.
  - Note: the static accessor `Get()` and the query `Get(string_view)` share a name but differ in parameters, so they can legally coexist.
- Follow root [AGENTS.md](../../../../AGENTS.md).
