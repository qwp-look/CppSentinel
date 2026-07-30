# Configuration Guide (v2)

> 中文版：[configuration.zh-CN.md](configuration.zh-CN.md)

Default config: [`Configuration/Configuration.toml`](../Configuration/Configuration.toml).

## CLI

The config file and target are always explicit (no interactive mode, no recursive search):

```bash
CppSentinel run --config path/to/Configuration.toml --target main.cpp
```

Relative paths inside the config are resolved against the **config file directory**.

Ensure CMake emits `compile_commands.json` (`CMAKE_EXPORT_COMPILE_COMMANDS` is on by
default in this project) and that `[project].compilation_database` points at it
(for example `../build/compile_commands.json`).

## Full schema example

```toml
schema_version = 2

[project]
root = ".."
compilation_database = "../build/compile_commands.json"
report_dir = "../report"

[run]
max_parallel = 1
timeout_seconds = 120
fail_on = "warning"       # never|error|warning|style|information

[environment.native]
kind = "native"
inherit_parent = true

[environment.mingw64]
kind = "msys2"
root = "C:/msys64"
msystem = "MINGW64"       # MINGW64|CLANG64|UCRT64|CLANGARM64|MSYS
inherit_parent = true

[[checker]]
id = "clang-tidy"         # globally unique; maps to log/artifact names
kind = "clang-tidy"
enabled = true
environment = "mingw64"
extra_args = []
```

Allowed top-level keys: `schema_version`, `project`, `run`, `environment`, `checker`
(`[[checker]]`). Legacy `[MethodName]` tables and `command = """..."""` are rejected.

## Defaults

| Field | Default |
|-------|---------|
| `checker.enabled` | `true` |
| `checker.environment` | `native` |
| `run.timeout_seconds` | `120` |
| `run.max_parallel` | `1` (only value supported) |
| `run.fail_on` | `warning` |
| `environment.*.inherit_parent` | `true` |

## Built-in kinds

| kind | Role | Typical `--target` | Needs compile_commands |
|------|------|--------------------|------------------------|
| `clang-tidy` | Static analysis with `-p <build_dir>` | single `.cpp` | **required** |
| `clang-format` | Style dry-run (`--Werror`) | source/header | no |
| `compiler-diagnostics` | Replay compile DB args with `-fsyntax-only` | single `.cpp` | **required** |
| `cppcheck` | Independent analysis; auto `-I include` + project root | file or directory | no |

### Optional fields per kind

Scalar fields on a checker table are stored in an internal `options` map:

| Field | Kinds | Meaning |
|-------|-------|---------|
| `executable` | all | Override default tool binary name |
| `checks` | `clang-tidy` | `-checks=` list |
| `header_filter` | `clang-tidy` | `-header-filter=` |
| `style` | `clang-format` | `--style=` (default `file`) |
| `compiler` | `compiler-diagnostics` | Fallback compiler name |
| `enable` | `cppcheck` | `--enable=` |
| `std` | `cppcheck` | `--std=` |
| `suppressions` | `cppcheck` | `--suppressions-list=` path |
| `extra_args` | all | Extra argv strings |
| `timeout_seconds` | all | Per-checker timeout override |

Default executables and base argv live in code (`BuiltinCheckers`). Config only
toggles enablement, environment, options, and extra args.

## Validation

- `schema_version` must be integer `2`
- Each `checker.id` must be unique (log/artifact file names)
- `environment` must reference a defined profile (`native` is always present)
- Unknown top-level keys / fields are errors (field path; TOML syntax errors include line/column)
- `command` shell templates are rejected
- `kind = "msys2"` on non-Windows is a config error
- Execution order matches `[[checker]]` declaration order

On load failure the CLI prints every `ConfigDiagnostic` and exits with code `1`.

## fail_on

| Value | Fails when |
|-------|------------|
| `never` | Hard failures only |
| `error` | diagnostics ≥ error |
| `warning` | ≥ warning (default) |
| `style` | ≥ style |
| `information` | any diagnostic |

These **CheckerStatus** values always fail the run: `StartError`, `ToolError`,
`TimedOut`, `ConfigError`.

When a tool finishes and only emits diagnostics, status may be `Succeeded` and
`fail_on` decides the exit code. Style issues should appear as `style` diagnostics
(see `clang-format`).

## Tips

1. Configure `compilation_database` before enabling `clang-tidy` / `compiler-diagnostics`.
2. On Windows with MinGW tools, use `environment = "mingw64"` (or your own msys2 profile).
3. Put extra flags in `extra_args`; do not reintroduce multi-line shell `command`s.
4. One `[[checker]]` per concern (format / analysis / compiler diagnostics).
5. For this repo, ensure `compile_commands.json` contains an entry for the target TU.
