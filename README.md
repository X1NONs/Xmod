# Xmod

Xmod is a Linux memory analysis and editing framework inspired by tools like Cheat Engine, but built as a modular Linux framework.

It provides a kernel-level memory backend, a userland CLI core, and a plugin system for memory scanning, dumping, pattern searching, pointer discovery, and low-level memory research.

Xmod is designed for:

- reverse engineering
- exploit development research
- memory forensics
- Linux internals learning
- binary analysis
- CTF / lab / debug use cases

> Use Xmod only on systems, processes, and memory you own or have explicit permission to analyze.

---

## Features

### Core framework

- Kernel module backend: `xmod.ko`
- Userland CLI: `xmod-core`
- Shared ioctl API between kernel and userland
- Plugin system using shared objects `.so`
- Process virtual memory read/write
- Physical memory read/write
- Capability reporting
- Confirmation prompts for dangerous physical operations

### Memory targets

| Target | Read | Write | Notes |
|---|---:|---:|---|
| Process virtual memory | Yes | Yes | Attach by PID |
| Physical memory | Yes | Yes | Requires root and confirmation |

### Plugins

Xmod includes starter plugins:

| Plugin | Description |
|---|---|
| `memdump` | Dump process regions or physical memory ranges |
| `valuescan` | Scan process memory for typed values |
| `aobscan` | Scan memory for byte patterns with wildcards |
| `memwatch` | Snapshot and compare memory values |
| `ptrscan` | Scan for pointer candidates near a target address |

---

## Architecture

```text
GUI / TUI / Web UI            planned
        │
        ▼
xmod-core CLI + plugin host
        │
        ▼
xmod plugin API
        │
        ▼
/dev/xmod ioctl interface
        │
        ▼
xmod.ko kernel backend
        │
        ├─ process virtual memory backend
        └─ physical memory backend
