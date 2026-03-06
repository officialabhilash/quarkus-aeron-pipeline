# Contributing Guidelines

First of all, thank you for considering contributing! 
The Software will be built and used by people at large.

This document provides guidelines to ensure a smooth and collaborative contribution process for everyone involved.

## Table of Contents
1. [Code of Conduct](#code-of-conduct)
2. [Getting Started](#getting-started)
3. [Reporting Issues](#reporting-issues)
4. [Pull Request Process](#pull-request-process)
5. [Coding Standards](#coding-standards)

---

## Code of Conduct
By participating in this project, you agree to abide by our Code of Conduct. We expect all contributors to maintain a welcoming, inclusive, and respectful environment. 

## Getting Started
1. **Fork the repository** on GitHub.
2. **Clone your fork** locally: `git clone https://github.com/your-username/repository-name.git`
3. **Create a new branch** for your feature or bugfix: `git checkout -b feature/your-feature-name` or `git checkout -b fix/your-bug-fix`.
4. **Set up the development environment** by following the instructions in the `README.md`.

---

## Reporting Issues

We use a strict categorization system for our issues to keep the backlog organized and easy to navigate. 

When creating a new issue, your title **must** begin with a category in brackets `[]`, followed by a colon `:`, and then a clear, concise title.

### Supported Categories:
* **`[BUG]`**: Something is not working as expected.
* **`[FEATURE REQUEST]`**: A proposal for a new feature or functionality.
* **`[REFACTOR REQUEST]`**: A suggestion to improve the structure or performance of existing code without changing its behavior.
* **`[DOCS]`**: Improvements or additions to documentation.
* **`[QUESTION]`**: General inquiries about the codebase or project direction.

### Issue Format Example:
**Title:** `[BUG]: Authentication middleware fails on token expiration`

**Description:**
Provide as much context as possible in the body of the issue.
* **Describe the issue:** What is happening? What did you expect to happen?
* **Steps to reproduce:** Provide a step-by-step guide on how to trigger the bug or explain the use case for the feature.
* **Environment:** OS, language version, and relevant library versions.
* **Screenshots/Logs:** Include relevant stack traces or screenshots if applicable.

---

## Pull Request Process

1. **Link the Issue:** Ensure your PR addresses an existing issue. Reference the issue number in your PR description (e.g., `Closes #123`).
2. **Keep it focused:** Submit one PR per feature or bug fix. Avoid bundling unrelated changes together.
3. **Write Tests:** If you are adding a new feature or fixing a bug, please include the appropriate tests.
4. **Update Documentation:** If your changes affect how the system is deployed, configured, or used, update the relevant documentation.
5. **Pass CI/CD:** Ensure all automated checks and tests pass before requesting a review.
6. **Code Review:** A maintainer will review your code. Be open to feedback and ready to make requested changes.

---

## Coding Standards
* Write clean, readable, and well-documented code.
* Follow the standard style guides and linters configured for the project's specific languages. 
* Write meaningful commit messages that briefly explain the "what" and "why" of the change.
