# [roo_transport 1.1.5](https://github.com/dejwk/roo_transport/releases/tag/1.1.5)

Published 2026-08-29.

## roo_transport 1.1.5

This release expands host emulation coverage, adds ESP-IDF UART support, and improves example usability and test reliability.

### Highlights

- Added a runnable packet transport example:
  `bazel run //examples/Packets:Packets`
- Made all Arduino examples runnable under the `roo_testing` emulator.
- Added an ESP-IDF reliable UART transport and a runnable ESP-IDF benchmark example.
- Adopted `roo_testing` 2.0 host profiles, including Arduino, ESP-IDF, and AddressSanitizer configurations.
- Improved CI hardening and eliminated spurious serial-link test failures.
- Updated Roo dependencies:
  `roo_collections` 1.4.6, `roo_io` 2.2.7, `roo_logging` 1.5.8, `roo_scheduler` 2.1.10, and `roo_threads` 1.2.7.
- Fixed compilation warnings and an underflow-prone RPC deserialization bounds check.

**Full Changelog**: https://github.com/dejwk/roo_transport/compare/1.1.4...1.1.5

---

# [roo_transport 1.1.4](https://github.com/dejwk/roo_transport/releases/tag/1.1.4)

Published 2026-08-07.

Re-formatted the source code.

**Full Changelog**: https://github.com/dejwk/roo_transport/compare/1.1.3...1.1.4

---

# [roo_transport 1.1.3](https://github.com/dejwk/roo_transport/releases/tag/1.1.3)

Published 2026-06-04.

* Fixed a couple of concurrency issues detected via static analysis.
* The library now passes a rigorous stress tests (10K executions, running concurrently).
* Added a programming guide and proper doxygen documentation.  

**Full Changelog**: https://github.com/dejwk/roo_transport/compare/1.1.2...1.1.3

---

# [roo_transport 1.1.2](https://github.com/dejwk/roo_transport/releases/tag/1.1.2)

Published 2026-02-25.

* Updated dependencies,
* Fixed compilation warnings,
* Added doxygen-style documentation.

**Full Changelog**: https://github.com/dejwk/roo_transport/compare/1.1.1...1.1.2

---

# [roo_transport 1.1.1](https://github.com/dejwk/roo_transport/releases/tag/1.1.1)

Published 2026-01-26.

* Fixed tests, broken by the recent Bazel behavior change.
* Added an example for esp-idf.

**Full Changelog**: https://github.com/dejwk/roo_transport/compare/1.1.0...1.1.1

---

# [roo_transport 1.1.0](https://github.com/dejwk/roo_transport/releases/tag/1.1.0)

Published 2026-01-07.

* Refactored, simplified API.
* More examples.
* Added RPC!
* Added multiplexing.
* Made compatible with RP2020.
* Bug fixes: bogus 'check wiring' warnings; better handling of reconnections in messaging.

**Full Changelog**: https://github.com/dejwk/roo_transport/compare/1.0.2...1.1,0

---

# [roo_transport 1.0.2](https://github.com/dejwk/roo_transport/releases/tag/1.0.2)

Published 2025-11-12.

* Changes in the messaging API.
* Small tweaks here and there.
* Updating dependencies.

**Full Changelog**: https://github.com/dejwk/roo_transport/compare/1.0.1...1.0.2

---

# [roo_transport 1.0.1](https://github.com/dejwk/roo_transport/releases/tag/1.0.1)

Published 2025-10-31.

* Adding support for channels to messaging;
* Updated dependencies;
* Added continuous integration.

**Full Changelog**: https://github.com/dejwk/roo_transport/compare/1.0.0...1.0.1

---

# [roo_transport 1.0.0](https://github.com/dejwk/roo_transport/releases/tag/1.0.0)

Published 2025-10-18.

Initial release.

---

