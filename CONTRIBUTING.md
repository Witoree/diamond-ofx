# Contribute to Diamond OFX

## Report a problem

Open an issue with the effect name, Diamond version, host edition and build, Windows version, and steps to reproduce. Include relevant parameter values or keyframes and describe what you saw in the preview or render. For performance issues, include the project resolution and format. Use screenshots or media you have permission to share.

## Submit a change

1. Fork the repository and create a branch for your change.
2. Keep the pull request focused on one effect or issue. Use the [build guide](docs/BUILD.md) to verify that the code generates the bundle.
3. Describe what changed and how you verified it. If you tested it in a video editor, include the host edition and build.

Keep the published OpenFX identifiers under `com.diamondfx.*` and internal parameter names stable. Saved projects use these identifiers to locate effects and restore settings. For rendering changes, check how the effect handles alpha, image bounds, and host cancellation.

Contribute original code or components with a compatible license and attribution. If you use another effect's public documentation as a reference, link to it and describe your implementation. The project uses the [MIT license](LICENSE.txt).
