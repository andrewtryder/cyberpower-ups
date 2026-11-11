# Repository Governance & Branch Protection

This document formalizes repository governance, maintenance policies, and recommended GitHub protection settings for `cyberpower-ups`.

---

## 1. Governance & Maintenance Model

`cyberpower-ups` is an independent, community-driven open-source project maintained by Andrew Ryder ([@andrewtryder](https://github.com/andrewtryder)).

### Decision-Making & Review
- **Solo Maintainer**: PR reviews, release tagging, and merges are managed by the primary maintainer.
- **Contributions**: Contributions from the community (bug reports, documentation, protocol reverse-engineering, offline tests, and platform improvements) are welcomed via GitHub issues and pull requests.
- **Merge Strategy**: All pull requests targeting `main` must be squash-merged with a Conventional Commits title (e.g. `feat(scope): ... (#123)`) to maintain a clean and linear Git history.

---

## 2. Automated Dependency Maintenance

Dependency updates are automated via Dependabot ([`.github/dependabot.yml`](../.github/dependabot.yml)):
- **Ecosystem**: `github-actions`
- **Schedule**: Weekly
- **Limit**: Maximum 5 open pull requests
- **Commit Prefix**: `chore(deps)` (ensures automated PR titles comply with Conventional Commits quality gate)
- **Security**: Dependency PRs must pass the identical six automated CI/quality checks as user contributions before merge.

---

## 3. Recommended GitHub Branch Protection for `main`

To maintain stability without creating administrative roadblocks for a single maintainer, apply the following GitHub Ruleset to the `main` branch.

> [!IMPORTANT]
> **Activation Requirement**: Do not enable branch protection until PR #5 has merged into `main` and verified that automated checks succeed.

### Recommended Ruleset Configuration

In **Repository Settings → Rules → Rulesets → New ruleset → New branch ruleset**:

| Setting | Recommended Value | Reason |
| :--- | :--- | :--- |
| **Ruleset Name** | `main-protection` | Descriptive identifier |
| **Enforcement Status** | `Active` | Enforces rules immediately |
| **Target Branches** | Include default branch (`main`) | Protects production branch |
| **Restrict deletions** | `Enabled` | Prevents accidental branch deletion |
| **Block force pushes** | `Enabled` | Preserves linear history and prevents rewrites |
| **Require a pull request before merging** | `Enabled` | Ensures all changes go through PR review and CI |
| **Required approvals** | `0` (or unchecked) | Allows the solo maintainer to merge without requiring an artificial second account approval |
| **Dismiss stale approvals** | `Enabled` | Ensures reviews re-run on new commits |
| **Require status checks to pass** | `Enabled` | Gates merges on automated verification |
| **Require branches to be up to date** | `Enabled` | Ensures tests ran against latest base branch |

### Required Status Checks

Specify the exact check names reported by GitHub Actions:
1. `Ubuntu build + self-test`
2. `macOS build + self-test`
3. `Actionlint`
4. `Conventional PR title`
5. `Clang-format (incremental)`
6. `Zizmor security audit`

> [!NOTE]
> Do **not** enable a Merge Queue unless the workflows are explicitly updated to handle the `merge_group` event.

---

## 4. Repository Settings Activation Checklist for Maintainer

After PR #5 merges:
1. [ ] **Enable Private Vulnerability Reporting**: Navigate to **Settings → Code security and analysis → Private vulnerability reporting** and click **Enable**.
2. [ ] **Configure Dependabot Alerts & Updates**: Ensure **Dependabot alerts** and **Dependabot version updates** are enabled under **Settings → Code security and analysis**.
3. [ ] **Create Branch Ruleset**: Navigate to **Settings → Rules → Rulesets** and create the `main-protection` ruleset with the settings and required checks documented above.

