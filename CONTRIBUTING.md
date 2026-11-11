# Contributing to cyberpower-ups

Thank you for contributing! This document details code quality standards and automated checks used across the repository.

---

## 1. Pull Request Titles (Conventional Commits)

All pull request titles must conform to the [Conventional Commits](https://www.conventionalcommits.org/) specification to ensure clean history and compatible squash-merges:

```
<type>[(<scope>)][!]: <description>
```

### Allowed Types
- `feat`: A new feature or capability
- `fix`: A bug fix or transport hardening
- `docs`: Documentation improvements
- `style`: Formatting or cosmetic changes (no code logic change)
- `refactor`: Code refactoring without behavioral changes
- `perf`: Performance optimizations
- `test`: Test suite additions or corrections
- `build`: Build system or dependency updates (CMake, Makefile)
- `ci`: CI/CD workflows and automated quality gates
- `chore`: Maintenance tasks
- `revert`: Reverting a previous commit

### Examples
- `feat: add a new status field`
- `fix(linux): correct HID report parsing`
- `ci: introduce automated code quality gates`
- `docs: document Linux device permissions`
- `refactor(serial): consolidate POSIX transport logic`
- `feat!: change a public API contract`

You can test a PR title locally using:
```bash
./scripts/check-pr-title.py "feat(linux): my title"
```

---

## 2. Code Formatting (`clang-format`)

C and C++ files follow the `.clang-format` configuration (Google-based C++17 style with 2-space indentation and 100-column limit).

Enforcement in CI is **incremental**: only the lines and files modified in your pull request relative to `origin/main` are checked. This prevents unrelated formatting churn across the existing codebase.

### Check Formatting Locally
```bash
make check-format
# or directly:
./scripts/check-format.sh origin/main
```

### Automatically Format Changed Lines
```bash
make format
# or directly:
git-clang-format origin/main
```

---

## 3. GitHub Actions Workflow Linting (`actionlint`)

All workflows in `.github/workflows/` are checked by [actionlint](https://github.com/rhysd/actionlint).

To run actionlint locally:
```bash
# macOS (via Homebrew)
brew install actionlint

# Run check
actionlint
```

---

## 4. GitHub Actions Security Scanning (`zizmor`)

Workflows are audited for supply-chain security, credential exposure, and least-privilege permissions using [zizmor](https://github.com/zizmorcore/zizmor).

To run zizmor locally:
```bash
# Via cargo or Homebrew
brew install zizmor
# or: cargo install zizmor

# Run audit
zizmor .
```

---

## 5. Running All Quality Checks
```bash
make test          # Offline CTest suite + protocol self-test
make check-format  # clang-format check on changed lines
make lint          # actionlint and zizmor workflow audit
```

