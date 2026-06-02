# AUR: transmission-client-gtk

Packaging for [transmission-client-gtk](https://github.com/DeathKhan/transmission-client-gtk) — GTK UI that controls a remote `transmission-daemon` over HTTP RPC.

## Install (after publishing to AUR)

```bash
yay -S transmission-client-gtk
# or
paru -S transmission-client-gtk
```

## Build locally

```bash
makepkg -si
```

## Publish to AUR (one-time)

1. Create the package on [aur.archlinux.org](https://aur.archlinux.org/) (account required).
2. Clone the empty AUR repo:

   ```bash
   git clone ssh://aur@aur.archlinux.org/transmission-client-gtk.git
   cd transmission-client-gtk
   ```

3. Copy `PKGBUILD` and generate `.SRCINFO`:

   ```bash
   cp /path/to/transmission-client-gtk-aur/PKGBUILD .
   makepkg --printsrcinfo > .SRCINFO
   ```

4. Commit and push:

   ```bash
   git add PKGBUILD .SRCINFO
   git commit -m "Initial commit: transmission-client-gtk"
   git push
   ```

Update `source=(...#commit=...)` and run `makepkg --printsrcinfo` when you cut new GitHub releases.
