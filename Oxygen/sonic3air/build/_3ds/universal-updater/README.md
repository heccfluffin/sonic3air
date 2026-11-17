# Universal-Updater store

[Universal-Updater](https://github.com/Universal-Team/Universal-Updater) is a homebrew "app store" for the 3DS.
This folder holds what's needed to install and update the port through it:

- `make_unistore.py`: generates `sonic3air-3ds.unistore` (UniStore format version 3). The release job in
  `.github/workflows/3ds.yml` runs it for every version tag and attaches the result to the GitHub Release,
  together with the icon sheet `sonic3air-3ds.t3x` (built from `../icon.png` with tex3ds).
- `store-qr.png`: QR code of the store URL, for adding the store in Universal-Updater with the camera.

The store URL always points to the latest release:

    https://github.com/heccfluffin/sonic3air/releases/latest/download/sonic3air-3ds.unistore

Universal-Updater reloads the store when its `revision` increases (the workflow uses the run number),
and downloads the CIA / 3DSX from the latest non-prerelease GitHub Release.

## Publishing a new version

    git tag v0.1.0
    git push origin v0.1.0

The workflow builds everything and publishes the release "Sonic 3 A.I.R. for New 3DS v0.1.0".
Use a new, higher version number for each release. If the fork is renamed or moved, regenerate the QR code
and update the URL in INSTALL.md.

## Test builds

Every build of the development branch (`3ds-gpu-drawer`) is published automatically as the **test-build** pre-release,
replacing the previous one (job `test-release` in the workflow). It has its own store at a fixed URL:

    https://github.com/heccfluffin/sonic3air/releases/download/test-build/sonic3air-3ds-test.unistore

QR code: `test-store-qr.png`. In Universal-Updater, the test store shows up as "Sonic 3 A.I.R. for New 3DS (test builds)"
and always offers the newest build. Test builds can be unstable; the normal store only offers tagged releases.
