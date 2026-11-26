# Architecture

AnyPS1 is organized around a relinker core, compatibility libraries, command-line tools, and target-platform integrations.

The initial implementation will decode PS-X EXE structures, analyze references and entry points, and produce a target-native executable with equivalent system-library calls.

