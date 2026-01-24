# Contributing to DirectLink

Thank you for your interest in contributing to DirectLink! This document provides guidelines and instructions for contributing to the project.

## Table of Contents

- [Code of Conduct](#code-of-conduct)
- [Getting Started](#getting-started)
- [Development Workflow](#development-workflow)
- [Commit Guidelines](#commit-guidelines)
- [Pull Request Process](#pull-request-process)
- [Coding Standards](#coding-standards)
- [Testing](#testing)

## Code of Conduct

Be respectful, professional, and constructive in all interactions. We're building this together as a team.

## Getting Started

### Prerequisites

- Docker & Docker Compose
- Visual Studio Code with Dev Containers extension (recommended)
- Git

## Development Workflow

### Branch Naming

Use the following prefixes for branch names:

- `feat/` - New features
- `fix/` - Bug fixes
- `docs/` - Documentation changes
- `style/` - Code style/formatting changes
- `refactor/` - Code refactoring
- `perf/` - Performance improvements
- `test/` - Adding or updating tests
- `chore/` - Maintenance tasks, dependency updates, etc.
- `ci/` - CI/CD changes

**Examples:**
- `feat/add-multi-camera-support`
- `fix/websocket-reconnection-issue`
- `docs/update-api-documentation`
- `chore/update-dependencies`

## Commit Guidelines

We use [Conventional Commits](https://www.conventionalcommits.org/) with scopes to maintain a clear and structured commit history.

### Commit Message Format

```
<type>(<scope>): <subject>

[optional body]

[optional footer]
```

### Types

- `feat` - A new feature
- `fix` - A bug fix
- `docs` - Documentation only changes
- `style` - Changes that don't affect code meaning (formatting, white-space, etc.)
- `refactor` - Code change that neither fixes a bug nor adds a feature
- `perf` - Performance improvement
- `test` - Adding or updating tests
- `chore` - Maintenance tasks, dependency updates
- `ci` - CI/CD pipeline changes
- `build` - Build system or external dependency changes

### Scopes

Use component-specific scopes to indicate which part of the codebase is affected:

**Backend (Go):**
- `signaling` - Signaling server
- `sfu` - Selective Forwarding Unit
- `redis` - Redis integration
- `api` - API endpoints and handlers
- `config` - Configuration management

**Video Core (C++):**
- `capture` - Video capture functionality
- `encode` - Video encoding
- `decode` - Video decoding
- `pipeline` - Video processing pipeline
- `utils` - Video utilities

**Client (Qt/C++):**
- `ui` - User interface components
- `network` - Client networking
- `controls` - UI controls and widgets
- `app` - Application logic

**Infrastructure:**
- `k8s` - Kubernetes configurations
- `terraform` - Terraform infrastructure
- `docker` - Docker configurations
- `monitoring` - Monitoring and observability

### Examples

```
feat(signaling): add WebSocket reconnection logic

Implements automatic reconnection with exponential backoff
when WebSocket connections are dropped unexpectedly.

Closes #123
```

```
fix(encode): correct H.264 keyframe interval calculation

The keyframe interval was being calculated incorrectly,
causing issues with stream seeking and initialization.
```

```
docs(api): update signaling protocol documentation

- Add sequence diagrams for connection flow
- Document new error codes
- Update examples with latest API changes
```

```
perf(sfu): optimize packet routing algorithm

Reduces CPU usage by ~15% under high load by implementing
a more efficient routing table lookup.
```

### Commit Message Guidelines

- Use the imperative mood in the subject line ("add" not "added" or "adds")
- Don't capitalize the first letter of the subject
- No period at the end of the subject line
- Separate subject from body with a blank line
- Use the body to explain *what* and *why*, not *how*

## Pull Request Process

1. **Update documentation** - Ensure all relevant documentation is updated
2. **Add tests** - Add tests for new functionality or bug fixes
3. **Run tests locally** - Ensure all tests pass before submitting
4. **Code formatting** - Run formatters (will be automated via pre-commit hooks)
5. **Fill out PR template** - Provide a clear description of changes
6. **Link issues** - Reference related issues using "Closes #123" or "Fixes #456"
7. **Request review** - Tag appropriate team members for review
8. **Address feedback** - Respond to and address all review comments
9. **Squash commits** - Maintain a clean commit history (if requested)

### PR Title Format

PR titles should follow the same convention as commits:

```
<type>(<scope>): <description>
```

**Examples:**
- `feat(signaling): add WebSocket reconnection`
- `fix(encode): correct keyframe interval`
- `docs(api): update protocol documentation`

## Coding Standards

### Go (Backend)

- TBA

### C++ (Video Core & Client)

- Follow the project's `.clang-format` configuration
- Follow the project's `.clang-tidy` rules
- Use modern C++ features
- Follow RAII principles
- Use smart pointers over raw pointers
- Naming conventions (enforced by clang-tidy):
  - `camelCase` for functions
  - `lower_case` for variables
  - `CamelCase` for classes and structs
  - `UPPER_CASE` for global constants
  - `lower_case` for namespaces

## Testing
TBA
