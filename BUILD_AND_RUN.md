# AETHERIA-GR: first Android run

## What was adjusted in this corrected copy

- Removed the ImGui Vulkan `ApiVersion` assignment that does not exist in the pinned ImGui v1.91.8 backend API.
- Removed the `ImGuiConfigFlags_IsTouchScreen` flag, which is not defined by the pinned ImGui v1.91.8 core. SDL3's backend handles touch-generated mouse events; app finger events still control camera orbit/pinch.
- Made Vulkan/ImGui cleanup safe when initialization fails partway through.
- Fixed app-state ownership and cleanup on SDL window / engine initialization failures.
- Removed `-ffast-math` from the Android build and switched to a conservative ARM64 baseline for initial numerical validation.
- Made shader compilation fail clearly when sources are missing, compile recursively, target Vulkan 1.1, and clean stale SPIR-V outputs.
- Added per-shader and per-native-library APK verification to the CI workflow.
- Lowered Stage 2 default body counts (Android 1,024; desktop 8,192) and added a HUD slider. The current direct-gravity solver is O(N²), so increase gradually after profiling.

## Build Stage 0 with GitHub Actions

1. Create a GitHub repository for the project.
2. Upload the contents of the `AETHERIA-GR` folder to the repository root (do not upload the ZIP as a nested ZIP).
3. Ensure the workflow file is at `.github/workflows/build_all.yml`.
4. Open **Actions** in GitHub and select **Build AETHERIA-GR**.
5. Choose **Run workflow**, select branch `main`, and set stage to `0`.
6. Wait for the Android job to finish. Download artifact `AETHERIA-GR-Stage0-OPPO-arm64`.
7. Extract `app-release.apk`, install it on the OPPO F31 Pro, and allow installation from your browser/file manager if prompted.
8. Confirm the triangle and diagnostic HUD appear. Background and reopen the app once.

If the workflow fails, open the failed job and copy the first actual compiler/build error (not only the final `BUILD FAILED` line) and send it here.

## After Stage 0

- Build stage `1` only after Stage 0 launches reliably.
- Stage 1 exercises the black-hole compute shader and HDR image path.
- Build stage `2` only after Stage 1 is stable. Start with 1,024 bodies and increase the HUD slider in steps. 16,384 bodies is a stress-test target, not a guaranteed mobile real-time count.

## Signing

The workflow's fallback signing key is temporary. It is suitable for installing a test APK, but it will change between runs. To update an already-installed APK without uninstalling, configure a persistent release keystore through GitHub Secrets. Never commit the keystore or passwords.
