# Patches

## freeink-sdk-rlcd.patch

The RLCD-4.2 board needs changes to the FreeInk SDK submodule that are not in the upstream SDK: the ST7305
display driver (`St7305Driver.*`, `St7305Frame.h`) and edits to `BoardConfig.h`, `FreeInkDisplay.cpp`,
`BatteryMonitor.cpp` and `InputManager.cpp`.

It applies to the SDK commit this repository points at (`634a3f8c067a3714a6a7c622015138af096ab5d2`). After
cloning, with the submodule checked out:

```
git submodule update --init
git -C freeink-sdk apply ../patches/freeink-sdk-rlcd.patch
```

To refresh it after changing the SDK (new files need `git add -N` first so they appear in the diff):

```
git -C freeink-sdk add -N <new files>
git -C freeink-sdk diff HEAD --binary > patches/freeink-sdk-rlcd.patch
git -C freeink-sdk reset -q -- <new files>
```
