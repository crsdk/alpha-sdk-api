# Security Policy

## Supported Versions

Security fixes are handled on the default branch until the first public release is tagged. After versioned releases begin, supported versions will be listed here.

## Reporting a Vulnerability

Please do not open a public GitHub issue for security vulnerabilities.

Report suspected vulnerabilities privately by emailing the repository maintainers or by using GitHub's private vulnerability reporting when it is enabled for this repository. Include:

- Affected commit, tag, or package version.
- Steps to reproduce.
- Impact and any known workarounds.
- Whether Sony Camera Remote SDK files or camera/network credentials are involved.

## Credential and SDK Handling

Do not commit camera credentials, network captures containing secrets, Sony Camera Remote SDK files, SDK zips, or vendor binaries. Sony SDK files must be downloaded separately by each contributor and placed locally using [docs/SDK_SETUP.md](docs/SDK_SETUP.md).
