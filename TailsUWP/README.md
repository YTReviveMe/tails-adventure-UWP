# UWP Xbox Notes

This folder contains the UWP/Xbox Dev Mode solution for the project.

## Save data location

On UWP/Xbox, save data and runtime files are stored under:

`LocalState/tails-adventure`

In a typical Dev Mode path this resolves to:

`Q:/Users/UserMgr0/AppData/Local/Packages/<package-id>/LocalState/tails-adventure`

## Mod loading

At startup, the game scans these mod roots in order:

1. `LocalState/tails-adventure`
2. `E:/TailsAdventureRemake/mods`

If `mods.ini` is found in a mod root, it is used to select and order enabled mods.

If `mods.ini` does not exist and the root is writable, the game creates one automatically.

## mods.ini format

`mods.ini` is an INI file with one section per mod folder:

```ini
[Pinkails over Tails V2]
enabled=true
priority=100
```

- `enabled`: `true` or `false` (defaults to `false` if omitted)
- `priority`: higher values load later (override lower-priority mods)

Each section name must match the folder name inside the mod root.

## Build

1. Run `.\setup_deps.ps1`
2. Open `TailsUWP.sln` in Visual Studio 2022
3. Build/Deploy `Debug|x64` or `Release|x64`
