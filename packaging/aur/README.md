# AUR: transmission-client-gtk

Packages the **`transmission-client-gtk`** branch of the [DeathKhan/transmission](https://github.com/DeathKhan/transmission) fork (upstream: [transmission/transmission](https://github.com/transmission/transmission)).

## Install (after publishing to AUR)

```bash
yay -S transmission-client-gtk
```

## Build locally

```bash
makepkg -si
```

## Publish to AUR

1. Create [transmission-client-gtk](https://aur.archlinux.org/packages/transmission-client-gtk) on AUR.
2. Clone `ssh://aur@aur.archlinux.org/transmission-client-gtk.git`, copy `PKGBUILD`, run `makepkg --printsrcinfo > .SRCINFO`, commit, push.

When the fork branch moves, bump `#branch=` or pin `#commit=` in `source=` and refresh `.SRCINFO`.
