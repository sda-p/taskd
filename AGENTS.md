# Agent Working Guidelines

## Build and Test
- Ensure submodules are initialized with `git submodule update --init --recursive`.
- Build the project in a `build/` directory using:
  ```bash
  mkdir -p build && cd build
  cmake ..
  make -j$(nproc)
  ```
- There is no automated test suite yet, so a successful compile is the check to run after modifications.

## Code Style
- Run `clang-format -i` on any modified `.c` or `.h` files before committing.
- The codebase targets C11 and uses clang; compile warnings should be treated as errors where possible.

## Repository Hygiene
- Do not commit the `build/` directory or submodule contents.
- Keep commits focused and descriptive.

## Pull Request Notes
- Summaries should briefly describe what was changed and reference relevant files.
- Mention the build command output in the testing section of the PR message.
