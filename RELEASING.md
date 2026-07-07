# Releasing HAVEN-FSK

The distribution channel is **GitHub Releases** with a portable ZIP.
Users unzip and run — no installer during the beta (see Deferred below).

## Checklist

1. **Bump the version** in `src/dsp/Constants.h` (`APP_VERSION`) and
   `CMakeLists.txt` (`project(... VERSION ...)`) — keep them matching.
2. **Update `CHANGELOG.md`** — new section at the top for this version.
3. **Build Debug and confirm self-tests pass:** run `build.bat`, launch
   `build\HavenFSK.exe`. The GUI appearing means all startup self-tests
   passed (a failed test exits non-zero before the window opens).
4. **Build Release:** run `build-release.bat`.
5. **Package:** `powershell -File scripts\package-release.ps1`
   → produces `dist\HavenFSK-vX.Y.Z-win64.zip` and the matching staging
   folder.
6. **Smoke test the package:** unzip the zip to a fresh folder (not the
   staging dir), run `HavenFSK.exe` there. Confirm: no console window,
   HAVEN icon in title bar/taskbar/Explorer, waterfall runs, a dummy-load
   decode works. Best test: a PC without Qt installed — that's what
   catches a missing DLL.
7. **Commit and tag:**
   ```
   git tag vX.Y.Z
   git push --tags
   ```
8. **Publish:**
   ```
   gh release create vX.Y.Z dist/HavenFSK-vX.Y.Z-win64.zip --prerelease --title "HAVEN-FSK vX.Y.Z" --notes-file <notes>
   ```
   Write the notes from the CHANGELOG section (drop `--prerelease` once
   out of beta).

## Deferred / future

- **Windows installer (Inno Setup)** at v1.0 — Start Menu entry,
  uninstaller. Revisit `WIN32_EXECUTABLE` notes in CMakeLists if the
  console/windowed split changes.
- **Code signing** — removes the SmartScreen "unknown publisher" warning;
  costs ~$100+/year for a certificate. Worth it when the audience grows.
- **winget manifest** — lets users `winget install`; requires stable
  release-asset URLs (already true) and ideally a signed installer.
- **Linux / Raspberry Pi packages** (.deb or AppImage) — source build
  only for now; the CMake build already supports Linux.
