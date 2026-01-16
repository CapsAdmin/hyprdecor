# What

A hyprland plugin which adds ninepatch window decorations. You could use it to add custom window bars, or maybe just fancy borders for your windows.

It uses 9 patch bitmap system from android with which adds a 1 pixel border around the image for metadata. This makes it easy to define how the texture should stretch and where the content should be placed.

## Build / Run

```bash
nix run  # for quick testing
```

```bash
nix run .#debug  # runs with gdb
```