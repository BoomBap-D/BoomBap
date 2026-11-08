# BoomBap — Auto-Build (no compiling on your machine)

This folder is the BoomBap source with an auto-build recipe inside it
(`.github/workflows/build.yml`). Put it on GitHub once, and GitHub's
computers build the finished app for Windows, Mac and Linux every time.
You just download the results.

## Route A — browser upload (nothing to install)

1. **Make a free GitHub account** at github.com and sign in.
2. **Download BoomBap-GitHub-Ready.zip** (this package) from the Drive
   folder onto your PC and **unzip it** (right-click → Extract All).
3. **Create the repository:** on github.com, press **+** at the top
   right → **New repository**. Name it `BoomBap`. Private or Public —
   your call (Private gets a free monthly build allowance that covers
   occasional builds; Public builds free without limits). Do NOT tick
   the README / .gitignore / license boxes — ours are already in the
   files. Press **Create repository**.
4. **Upload:** on the empty repository page, click
   **"uploading an existing file"**. Drag in EVERYTHING INSIDE the
   unzipped folder — the contents, not the folder itself — including
   the `.github` folder (it holds the build recipe). Press
   **Commit changes**.
5. The build starts by itself. See "Getting your finished programs".

## Route B — GitHub Desktop app

If you prefer an app: install GitHub Desktop (desktop.github.com),
sign in, *File → New repository* named `BoomBap`, copy everything from
this folder into the repository folder it makes (including `.github`),
then **Commit to main** and **Publish repository**.

## Getting your finished programs

1. Go to your repository page on github.com → the **Actions** tab.
2. The newest run at the top is your build (roughly 10–20 minutes).
3. Open it, scroll to **Artifacts**, and download:
   - **BoomBap-Windows** — `BoomBap.exe` + the Windows VST3
   - **BoomBap-macOS** — the Mac app + VST3 + AU plug-ins
     (built for Apple Silicon Macs)
   - **BoomBap-Linux** — the Linux app + VST3
4. Hand those files to anyone. They just unzip and run — nobody
   builds anything.

## Building again later

Any time the code changes: copy the changed files into the repository
folder, commit, push — or simply press **Run workflow** on the Actions
tab to rebuild the same code. Downloads stay available for 90 days.

## Honest notes

- Mac apps built this way are **unsigned**, so the first time someone
  opens the Mac app they right-click it and choose **Open** (Gatekeeper
  asks once). Selling on the Mac properly later means an Apple
  Developer account for signing — a when-you-sell-it step, not a now
  step.
- Windows may show a similar one-time "unknown publisher" prompt on
  the `.exe`. Same story: normal for a brand-new indie app.
