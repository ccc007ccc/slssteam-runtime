# Changelog

## [Unreleased]

### Added

- Manager/runtime ABI marker `ContentRuntimeABI: 1`.
- Missing Depot injection through `BuildDepotDependency`.
- Manager-provided manifest sizes for non-zero Steam install estimates.
- Configured DepotKey response replacement.
- Asynchronous Manifest Request Code lookup and response replacement.
- Container-based CI for both 32-bit audit libraries.

### Fixed

- Grow Steam Depot vectors through the exported `Plat_Realloc` wrapper instead of relying on the
  private allocator C++ vtable layout.
- Preserve completed asynchronous request-code results until the matching Steam job response.
- Allow manager-configured injected apps to update when global `DisableUpdates` is enabled.
- Clamp manifest request-code lookup timeouts to 1-60 seconds so invalid configuration cannot
  indefinitely block Steam's managed content response path.

## Upstream baseline

This fork is based on AceSLS/SLSsteam commit `9cc2ff2e57ae71620614c580d54195bb1d4b6d43`.
