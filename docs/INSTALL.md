# Install in VEGAS Pro

Diamond OFX 0.03 was built for Windows x64 and targets VEGAS Pro 23 Steam Edition. A user confirmed that `D_Shake` worked in version 0.01. Version 0.03 has yet to be verified in VEGAS.

## First installation

1. Close VEGAS.
2. Open the repository's **Releases** page, download `Diamond-0.03-Win64.zip` under **Assets**, and extract it.
3. Copy the entire `Diamond.ofx.bundle` folder to `C:\Program Files\Common Files\OFX\Plugins\`. Create the `OFX\Plugins` folder if needed. Windows may ask for administrator permission.
4. Launch VEGAS from Steam. In **Video FX**, search for **D_Shake** or open the **Diamond** group. Drag the effect onto an image or video on the timeline.

Check the full path to the plug-in.

```text
C:\Program Files\Common Files\OFX\Plugins\Diamond.ofx.bundle\Contents\Win64\Diamond.ofx
```

## Update or roll back

Close VEGAS and move the installed `Diamond.ofx.bundle` out of `OFX\Plugins`. Copy the entire bundle folder from the ZIP for the version you want into that directory, then reopen VEGAS. Keep a copy of your project before opening it with an older version. Effects introduced in later versions will be unavailable.

## Uninstall

Close VEGAS and remove the `Diamond.ofx.bundle` folder from `OFX\Plugins`. Projects that use Diamond effects will need the plug-in reinstalled to load those effects.

## If effects are missing

Check that `Diamond.ofx` is in `Contents\Win64` inside the bundle and that there is only one `Diamond.ofx.bundle` level in the path. Restart VEGAS after installation. If the issue persists, report your VEGAS version and build, your Diamond version, and the installation path. See [how to report a problem](../CONTRIBUTING.md).
