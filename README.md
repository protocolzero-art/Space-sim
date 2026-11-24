# AETHERIA-GR // Relativistic Astrophysics & N-Body Engine

Cross-platform **C++20 + Vulkan 1.1/1.3 + GLSL Compute/Raymarching + SDL3 + Dear ImGui** astrophysics simulation engine engineered for **Desktop (Windows/Linux)** and **Android (`arm64-v8a`)**, tuned specifically for the **OPPO F31 Pro 5G** (MediaTek Dimensity 7300-Energy / Arm Mali-G615 MC2 / 1080p AMOLED).

## Key Subsystems
- **Kerr Black Hole Geodesic Raytracer (`shaders/kerr_blackhole.comp`)**: 4th-order Runge-Kutta (RK4) null-geodesic integration, frame-dragging spin $a_* \in [0, 0.998]$, Bardeen-Press-Teukolsky prograde ISCO, Novikov-Thorne blackbody accretion disk, relativistic Doppler beaming, and Blandford-Znajek polar jets.
- **GPU Symplectic N-Body Galactic Engine (`shaders/nbody_galactic.comp`)**: Shared-memory tiled Velocity Verlet integrator (`local_size_x = 128`), Navarro-Frenk-White (NFW) dark matter halo potential, and 1PN General Relativity perihelion precession near supermassive black holes.
- **Volumetric Multi-Scattering Emission Nebula (`shaders/nebula_volumetric.frag`)**: Domain-warped 3D fBM with Henyey-Greenstein phase scattering, self-shadowing, narrowband Hubble palette ($H\alpha$, $[O\text{ III}]$, $[S\text{ II}]$), and ACES Filmic HDR tonemapping.
- **Floating-Origin Cosmic Director (`src/physics/CosmicDirector.hpp`)**: Double-precision coordinate rebasing with quintic logarithmic hyperspace warp transitions.

## Staged Bring-Up (Zero Termux Cloud Build)
Use the **GitHub Actions** workflow (`.github/workflows/build_all.yml`) and choose `stage`:
1. **`stage = 0` (Vulkan Smoke Test)**: Renders an RGB triangle + GPU Diagnostics HUD (`Mali-G615 MC2`, Vulkan API version, `HDR16F Storage` check).
2. **`stage = 1` (Kerr Black Hole)**: Enables the RK4 geodesic raytracer, accretion disk, relativistic jets, and touch orbit/pinch controls.
3. **`stage = 2` (Full Cosmos)**: Enables the N-body system, galaxy mergers, volumetric nebulae, rogue black hole spawning, and supernova shockwaves. Android starts at 1,024 bodies and desktop at 8,192 bodies because the current direct-gravity solver scales O(N²). Increase the body count gradually in the HUD after measuring GPU time; 16,384 is a stress-test target, not a guaranteed real-time setting.

## First Android Run

1. Run the GitHub Actions workflow manually with `stage = 0`.
2. Download the `AETHERIA-GR-Stage0-OPPO-arm64` artifact and extract `app-release.apk`.
3. Install it on the OPPO F31 Pro. Allow installation from your browser/file manager if Android asks.
4. Confirm the RGB triangle and diagnostics HUD appear, then background and reopen the app once.
5. Only after Stage 0 is stable, build `stage = 1`; then test `stage = 2` with a low body count first.

The project currently has no Gradle wrapper, so the GitHub workflow uses the installed Gradle 8.9 command.
