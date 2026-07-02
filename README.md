# MothProbe

MothProbe is an AI-assisted penetration testing and cybersecurity assessment tool for discovering vulnerabilities, scanning attack surfaces, collecting OSINT digital footprint data, and turning raw findings into clear remediation guidance.

The project is built for authorized security work: internal security audits, bug bounty preparation, red-team support, blue-team validation, compliance checks, and defensive research. It is not intended for unauthorized access, exploitation, or abuse.

## Goals

- Help security teams find and understand vulnerabilities faster.
- Combine scanning, OSINT, and security analysis in one workflow.
- Use AI to explain findings, reduce noise, prioritize risk, and generate reports.
- Keep human approval, scope control, and audit logs at the center of every active scan.
- Support local and cloud AI providers without locking the project to one vendor.

## Planned Capabilities

### Scanning and Enumeration

- TCP and UDP port scanning.
- Host discovery and ping sweep.
- Banner grabbing and service fingerprinting.
- HTTP header and web security checks.
- TLS and certificate configuration checks.
- Technology detection for web applications and exposed services.

### Vulnerability Analysis

- CVE lookup by product, version, and service fingerprint.
- Severity scoring and prioritization.
- False-positive reduction using contextual analysis.
- Evidence collection for each finding.
- Remediation recommendations written for engineers.

### OSINT and Digital Footprint

- Domain and subdomain discovery.
- DNS record inspection.
- Public IP and ASN mapping.
- Email, organization, and exposed metadata collection from approved sources.
- Public web surface mapping.
- Leak, exposure, and misconfiguration indicators where legally accessible.

### AI-Assisted Workflow

- Scan plan generation from a natural-language request.
- Scope and permission validation before tools run.
- Human approval before active scanning.
- Structured JSON findings for LLM analysis.
- Explainable summaries, risk ranking, and report generation.
- Optional integration with local or cloud LLM providers.

## Safety Model

MothProbe should be safe by design:

- Explicit target scope is required before active scanning.
- Dangerous or intrusive actions require human approval.
- Commands and tool arguments must be validated before execution.
- All scans should be logged for auditability.
- Output should distinguish confirmed findings from assumptions.
- Secrets and sensitive target data should not be sent to cloud AI providers unless the user explicitly enables it.

## Architecture

The planned architecture centers on a C++ MCP server:

```mermaid
graph TD
    User[User Request] --> Client[AI Agent / MCP Client]
    Client --> MCP[MothProbe MCP Server]

    MCP --> Scope[Scope and Permission Gate]
    Scope --> Plan[Scan Plan Generator]
    Plan --> Approval[Human Approval]

    Approval --> Tools[Security Tools]
    Tools --> Findings[Structured Findings JSON]
    Findings --> Analyzer[AI Analysis Layer]
    Analyzer --> Report[Markdown / HTML / PDF Report]

    Tools --> Audit[Audit Log]
    Scope --> Audit
    Approval --> Audit
```

## Current Status

MothProbe is in early foundation stage. The repository currently focuses on C++ build setup, dependency integration, smoke tests, and the roadmap for the MCP server and security tool engine.

Implemented foundation pieces:

- CMake project setup.
- C++ dependency integration.
- Catch2 smoke tests.
- Static runtime configuration for MSVC builds.
- Initial cross-compilation toolchain files.

Major work still pending:

- JSON-RPC and MCP protocol implementation.
- Tool registry and `tools/list` / `tools/call` handlers.
- Scope validation and approval workflow.
- Real scanning modules.
- OSINT collectors.
- AI provider abstraction and report generation.

## Build

```powershell
cmake -S . -B build
cmake --build build
```

For Visual Studio multi-config builds, run tests with an explicit configuration:

```powershell
ctest --test-dir build -C Debug -R SmokeTest --output-on-failure
```

## Repository Layout

```text
src/                 Application and test source code
tests/               Future integration and protocol tests
cmake/toolchains/    Cross-compilation toolchain files
data/.mothprobe/     Local runtime data, config, caches, and model/tool assets
third_party/         Vendored dependencies and submodules
TODO.md              Development roadmap
```

## Ethics and Authorization

Use MothProbe only against systems you own or have explicit permission to assess. The project should prioritize defensive security, responsible disclosure, privacy, and explainable evidence over aggressive automation.

## Long-Term Vision

MothProbe aims to become a trusted AI-powered cybersecurity platform for vulnerability assessment, security auditing, OSINT-driven exposure mapping, compliance validation, and defensive operations.
