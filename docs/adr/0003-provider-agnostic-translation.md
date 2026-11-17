# ADR 0003 — Provider-agnostic translation with explicit network egress

- **Status:** Accepted
- **Date:** 2025-11-16
- **Deciders:** Project maintainer
- **Related:** `docs/DESIGN.md` §7–8

## Context

Translation quality, cost, and privacy depend heavily on the engine. Users arrive with existing
credentials (OpenAI-compatible endpoints, DeepL) or with a local model (Ollama, llama.cpp). Screen text is
often sensitive, so the app must never send it somewhere the user did not configure, and must never
silently change where it goes.

## Decision

Define one narrow asynchronous, cancellable provider boundary. Begin with one configurable implementation;
additional services need separate justification and testing. Credentials use platform secure storage, whose
actual backend may differ in a sandbox. There is **no** automatic fallback between providers, no retry storm,
and no bundled provider account. A configured endpoint and its actual privacy properties are shown to users.
Automatic remote transmission requires explicit consent in addition to provider configuration.

## Rationale

- A common request shape and the capabilities each implemented provider actually supports allow hosted and
  local engines without a plugin framework.
- An OpenAI-compatible HTTP implementation may cover some hosted and self-hosted endpoints using the same
  request path. Compatibility and streaming support must be tested for each advertised endpoint.
- A visible provenance line in the popup converts an invisible privacy property into an observable one.
- Explicit failure (auth, rate limit, timeout) is more useful than a hidden failover that changes quality,
  cost, and privacy without consent.

## Consequences

- Adding a provider requires implementing and testing its actual API and configuration; protocol variants
  and authentication requirements may differ.
- Streaming is a provider capability, not an assumption: the popup's loading state must look acceptable
  for non-streaming providers too.
- The app has no built-in free translation service. Documentation and the wizard must be clear that a
  provider (local or remote) is required, avoiding any implication of a bundled service.
- Test prompt construction where an LLM provider needs it. Model output is probabilistic and may not obey
  formatting instructions; do not claim that unit tests guarantee provider behavior.

## Alternatives considered

- **Single hardcoded engine:** least code, but couples the product to one vendor's pricing and policy and
  removes the local-processing option that privacy-conscious users require.
- **Automatic provider failover:** improves perceived reliability while silently changing where text goes
  and who pays — rejected on privacy grounds.
- **Plugin/extension system:** no concrete requirement justifies the API surface and security cost.
