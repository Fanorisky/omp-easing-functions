# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [1.0.3.0] - 2026-06-16

### Added
- **Dynamic Memory Pool**: Replaced fixed hard-coded 2048 animation slot limit with a dynamic vector-based pool that grows automatically on demand, eliminating memory waste and hard slot limits
- **Configuration System**: Added `config.json` support for customizing component behavior:
  - `easing.update_rate_fps`: Control animation framerate (default: 30)
  - `easing.batch_process_limit`: Tune max animations per update tick (default: 100)
  - `easing.expected_players`: Memory pre-allocation hint (default: 512)
  - `easing.initial_capacity`: Starting animation pool size (default: 64)
  - `easing.callback_reserve`: Pre-allocated callback buffer (default: 128)
- **Generation-based ID System**: Animation IDs now encode generation counters, preventing stale ID aliasing when slots are recycled
- **New Animation Functions**:
  - `PlayerText_MoveLetterSizeX()`: Animate letter width separately
  - `PlayerText_MoveLetterSizeY()`: Animate letter height separately
  - `PlayerText_MoveTextSizeX()`: Animate text width separately
  - `PlayerText_MoveTextSizeY()`: Animate text height separately
- **Silent Animation Tracking**: New `silentAnimations` stat in `GetAnimationStats()` to monitor callback-less animations
- **Improved Build Pipeline**:
  - Visual Studio version auto-detection via vswhere
  - Support for VS 2019, VS 2022, and VS 2026
  - Build workflow now triggers on all branches and pull requests
  - Added MSBuild setup automation

### Changed
- **Default Parameters**: All animation functions now have sensible defaults:
  - `duration` defaults to `1000` ms
  - `easeType` defaults to `EASE_NONE`
  - `silent` defaults to `false`
- **Memory Layout Optimization**: Changed animation collection from fixed array to vector with free-list recycling for O(1) allocation/deallocation
- **Batch Processing**: Improved round-robin animation processing to honor batch limits without dropping animations - every animation still progresses even under heavy load
- **Performance Metrics**: Update time now uses exponential moving average instead of overwriting each tick
- **Color Helper Improvements**:
  - Renamed `GetColorRGBA()` to `GetColorFromRGBA()` (with backward compatibility via conditional define)
  - Added new `GetRGBAFromColor()` function for color decomposition
  - Added proper bit masking to handle out-of-range values safely
- **Code Structure**: Moved main source file from `main.cpp` to `src/main.cpp`
- **`.gitignore` Updates**: Added `pawn-easing-functions.inc` to ignore list

### Fixed
- **Animation Aliasing**: Generation counters prevent reused slots from accepting commands meant for old animations
- **Active List Management**: O(1) removal from active animations list using stored position instead of linear search
- **Player/TextDraw Validation**: Proper null checking for player and textdraw state before updates
- **Update Cursor**: Added bounds checking to prevent cursor overflow when animations are removed

### Removed
- Hard-coded `MAX_ANIMATIONS = 2048` constant
- Direct array-based animation storage

### Performance Improvements
- Dynamic memory pool reduces memory footprint significantly (starts at 64 slots, grows on demand)
- O(1) animation lookup and removal instead of linear searches
- Hash-set based textdraw batch deduplication for large-scale updates
- Conditional configuration allows tuning for various server sizes

### Breaking Changes
- **Animation IDs are now opaque handles**: Do not use as array indices or assume sequential numbering
- `GetAnimationStats()` now has 4 parameters (added `silentAnimations` as 4th parameter with default 0)
- `PlayerText_MoveLetterSize()` and `PlayerText_MoveTextSize()` split into X/Y variants for finer control
- `GetColorRGBA()` renamed to `GetColorFromRGBA()` (old name still available via conditional compilation)

### Migration Guide
If upgrading from 1.0.2.0:

1. Update includes to use `#include <omp_easing>` 
2. Replace `GetColorRGBA()` calls with `GetColorFromRGBA()` (or use the compatibility macro)
3. Update any `GetAnimationStats()` calls to handle the 4th parameter
4. If using `PlayerText_MoveLetterSize()` or `PlayerText_MoveTextSize()`, split into X/Y variants:
   ```pawn
   // Old: PlayerText_MoveLetterSize(playerid, td, 0.5, 1000, EASE_OUT);
   // New:
   PlayerText_MoveLetterSizeX(playerid, td, 0.5, 1000, EASE_OUT);
   ```
5. Add `easing` configuration block to `config.json` if you want to customize performance tuning

---

## [1.0.2.0] - Previous Release

Initial release with fixed 2048 animation slots and basic easing functionality.
