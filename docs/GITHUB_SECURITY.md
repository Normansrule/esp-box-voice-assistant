# Securing the GitHub side

A checklist for the owner of this repository, and for anyone who forks it and
develops in public. Each item says where to click and why. None of these are
switched on by files in the repository; they are settings you turn on yourself.

Menu names are GitHub's as of late 2026 and occasionally move; each item links
GitHub's own instructions.

## Your account (do these first)

| Setting | Where | Why |
| --- | --- | --- |
| **Two-factor authentication** | Profile picture → **Settings** → **Password and authentication** → *Enable two-factor authentication*. Prefer a **passkey** or an authenticator app over SMS. Save the recovery codes somewhere offline. [Docs](https://docs.github.com/en/authentication/securing-your-account-with-two-factor-authentication-2fa/configuring-two-factor-authentication) | A stolen password alone can then not push to your repository or change its settings. |
| **Sign in from the terminal without passwords** | `gh auth login` ([GitHub CLI](https://cli.github.com/)), GitHub Desktop, or an [SSH key with a passphrase](https://docs.github.com/en/authentication/connecting-to-github-with-ssh) | Avoids long-lived tokens lying around in files. |
| **Tokens, if you need them** | **Settings** → **Developer settings** → **Personal access tokens** → *Fine-grained tokens*. Limit to this one repository, the fewest permissions, and an expiry date. | A leaked fine-grained token can do far less than a classic one. |
| **Review what has access** | **Settings** → **Applications** (authorized OAuth and GitHub Apps) and **Settings** → **SSH and GPG keys** | Remove anything you do not recognise or no longer use. |
| **Optional: signed commits** | [Commit signature verification](https://docs.github.com/en/authentication/managing-commit-signature-verification/about-commit-signature-verification), then turn on *Vigilant mode* | Shows "Verified" on your commits so nobody can pass off commits as yours. |

## This repository

All under the repository's **Settings** tab.

### Advanced Security (called *Code security* on some accounts)

| Setting | Why |
| --- | --- |
| **Private vulnerability reporting** → Enable | [SECURITY.md](../SECURITY.md) tells people to report problems privately through *Security → Report a vulnerability*; that button only exists when this is on. |
| **Dependency graph** → Enable | Needed for Dependabot. |
| **Dependabot alerts** → Enable | Warns you when an Action or the desktop app's npm packages have a known vulnerability. |
| **Dependabot security updates** → Enable | Opens pull requests that fix those alerts. Routine version updates are configured in [`.github/dependabot.yml`](../.github/dependabot.yml). ESP-IDF components are not covered by Dependabot; they stay pinned in `dependencies.lock`. [Docs](https://docs.github.com/en/code-security/dependabot/dependabot-security-updates/configuring-dependabot-security-updates) |
| **Secret scanning** → Enable | GitHub scans every push for known token formats (OpenAI, GitHub, AWS and many more) and alerts you. Free for public repositories. [Docs](https://docs.github.com/en/code-security/secret-scanning/introduction/about-secret-scanning) |
| **Push protection** → Enable | Stronger than scanning: GitHub *rejects* a push that contains a secret before it becomes public. Works alongside this repository's own `scripts/check_secrets.py` hook and CI job. [Docs](https://docs.github.com/en/code-security/secret-scanning/enabling-secret-scanning-features/enabling-push-protection-for-your-repository) |

### Rules → Rulesets (branch protection for `main`)

Create a **branch ruleset** targeting the default branch (`main`).
[Docs](https://docs.github.com/en/repositories/configuring-branches-and-merges-in-your-repository/managing-rulesets/creating-rulesets-for-a-repository)

Recommended for a one-person project:

- **Restrict deletions** and **Block force pushes**: nobody, including a
  stolen token, can rewrite or delete `main`'s history.
- **Require status checks to pass**, and add the CI jobs:
  *Firmware build (ESP-IDF v5.5.5, esp32s3)*, *Host tests and source checks*,
  *No secrets committed*, *Simulator data and README art are fresh*.
  They appear in the list after they have run once.
- **Require a pull request before merging**, with **0 required approvals**.
  Changes then go through a PR where CI runs, without blocking you as the only
  maintainer. Raise it to 1 approval if others get write access.
- Leave yourself in the **bypass list** only if you need emergency pushes, and
  use it rarely.

### Actions → General

- **Workflow permissions**: *Read repository contents and packages
  permissions* (read-only by default). The workflows here already ask for only
  what they need.
- **Fork pull request workflows**: *Require approval for all external
  contributors* (or at least first-time contributors). Stops strangers running
  code in your CI.
- Leave **Allow GitHub Actions to create and approve pull requests** off.

### General

- **Features**: turn off *Wikis* and *Projects* if you do not use them; fewer
  places for spam.
- **Pull Requests**: *Automatically delete head branches* keeps the branch list
  tidy.

### Pages

- **Source**: *GitHub Actions* (the `pages.yml` workflow). **Enforce HTTPS**
  on. The web flasher only works over HTTPS anyway.

### Releases and the web flasher

The release workflow builds firmware from a tag, fails if a secret is found,
and publishes the images the web flasher offers. Only push `v*` tags from a
`main` that passed CI. Optionally add a **tag ruleset** for `v*` that restricts
creation and deletion to you.

## What stays on you

- Never commit Wi-Fi passwords, API keys or recordings. The hook and CI catch
  the common formats, not everything.
- If a secret ever reaches GitHub, **revoke and replace it first**. Rewriting
  history afterwards does not help: forks, clones and caches keep copies.
- Do not share `build/` folders or self-built firmware images; `storage.bin`
  contains `spiffs/openai_key.txt` if you had one.
- Read [SECURITY.md](../SECURITY.md) for what the device itself does and does
  not protect (no flash encryption or secure boot yet).
