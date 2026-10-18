# nlohmann/json (vendored)

- Upstream: https://github.com/nlohmann/json
- Version: v3.11.3 (`single_include/nlohmann/json.hpp`)
- License: MIT, see `LICENSE.MIT`
- Why: the project needs to read JSON (baselines, result cache). The vendored
  toml++ is a trimmed build that only provides `json_formatter` (TOML -> JSON
  output), not a JSON parser, so a single-header parser is the smallest correct
  dependency.
- Refresh: replace `json.hpp` and `LICENSE.MIT` from the upstream tag.
