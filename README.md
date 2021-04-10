# Fireflower
**Powerful Nintendo DS patching toolchain**

## Preface
So after almost a year I decided to pretty much finish the patcher along with the two supporting NDS tools, nds-build and nds-extract.
Note that it is *neither* finished *nor* buildable without some build system setup and the two libraries nfsfsh.dll and blz.dll.

I might add an installer and proper instructions later but for now I just want to keep the code here in case anyone is interested.
Also, the supporting tools might be useful if you simply want to build or extract an nds file without worrying about file corruptions. Oh well.

## General
Fireflower is the most advanced patcher for Nintendo DS games. It works fundamentally different to any currently existing patcher and aims towards performance,
user-friendliness and practicability.

The whole toolchain essentially consists of three programs:
1) nds-extract which allows you to extract nds files
2) nds-build used to rebuild your nds file (with several FNT configuration options)
3) fireflower, the main patching program


## Features
**Patching targets

Fireflower supports all possible patching targets (targets are locations where raw code can be inserted, not patched):
1) ARM9 binary
2) ARM9 overlays
3) ARM7 binary
4) ARM7 overlays

Targets are specified using the `main` node in the configuration file. You can specify either single files or whole directories to be compiled to a specific target.
Note that as of now only binary targets are supported. Overlay targets will be supported in a future version.

Cross-processor hooking is *illegal*. From a practical standpoint it wouldn't make any sense anyways since the architechtures don't support the same set of instructions.

Fireflower is the first patcher supporting arm7 targets. **If you ever consider patching the arm7 you're expected to precisely know what you are doing.**
I verified that it works without any issues and I was able to inject code on the (very limited) arm7 heap, so it shouldn't incur any bugs.

arm7 overlays are pretty special in the sense they (1) are literally in no existing game and (2) their use is fairly limited since the arm7 is usually not exposed to filesystem related functions.

In case someone is brave enough to try it out here's a small guide:
1) Set patching target to `ov7_x` where x is the overlay number
2) Load the overlay from the arm9 into main ram or shared wram (remember to first map to arm9, then back to arm7)
3) Notify the arm7 (e.g. via IPC)
4) Jump to the newly loaded arm7 overlay (from hooked arm7 code)


**Basic patching**

Fireflower adds three hook types:
a) `hook`: Causes a direct branch to your code (generates `b`/`bx`). The replaced instruction is **not** saved. Useful for raw assembly modification.

b) `rlnk`: Causes a function call to your code (generates `bl`/`blx)`. The replaced instruction is **not** saved. Useful to replace function calls.

c) `safe`: Causes a function call to a thunk that saves all registers. The replaced instruction **is** saved. Fireflower warns you if the moved instruction will cause different
program behaviour. Note that this hook type is deprecated and should only be used for backwards-compatibility with NSMBe.

Fireflower also adds a replacement type:
`over`: Causes the symbol to overwrite code at the specified address. The size of the overwritten area is determined by the symbol size.

Examples:
```cpp
hook(0x02345678) void doSmth(){} 		//Hooks at 0x02345678 in the arm9 binary
rlnk(0x025673C0, 24) bool execute(){}	//Hooks at 0x025673C0 in arm9 overlay 24

over(0x02004800) unsigned char a = 4;	//Overwrites one byte at 0x02004800 in the arm9 binary
over(0x024588AD, 2) unsigned char b[3] = {
	1, 2, 3
};										//Overwrites three bytes at 0x024588AD in arm9 overlay 2
```

**File IDs**

Fireflower allows you to access any file in the nds tree via file IDs:
1) Add the file in the tree (i.e. `root/test/text.bin`) or use an existing one
2) Add a symbol to the configuration:
```
"file-id": {
        "my_file": "test/text.bin"
}
```
3) Use the symbol in your code:
```cpp
unsigned short myFileID = FID::my_file;
```

**New keywords**
Fireflower exposes new keywords to help the user in writing well-defined code:
`thumb`: Causes the function to get compiled in thumb mode

`asm_func`: Causes the function to remove function prologues/epilogues with the constraint of only allowing inline assembly

`nodisc`: Causes the compiler to not discard the function at higher optimization levels. This is especially important when your function is static and you call it from assembly,
in which case the compiler cannot detect the reference and therefore discards it.


## Operation
Fireflower works in a different way compared to already existing patchers. Instead of modifying the .nds in-place you have to extract it first.
To some people this might seem "inconvenient" or "tedious" but I'll promise you it helps in the long run. This was made so you can easily add files or replace existing ones
without having to keep track of which files you already replaced/added.

Other patchers such as NSMBe utilize a Makefile with requires you to install toolchains like devkitPRO and environments like msys2.
NSMBe then proceeds to compile the files and link them with a (very rudimentary) linker script. At this point a symbol map is generated, parsed, extracts the relevant hooking
symbols and then modifies `arm9.bin`.

The symbol map is huge, text based and forces you to name your functions after the hook causing confusion to the user if the hook is not properly documented.


Fireflower took a 180° approach by utilizing the .elf file itself (which must be generated anyways).
Instead of forcing the user to name his hooks appropriately certain macros exist to mark a function as a hook. Those are defined in `internal/ffc.h` and are always included by fireflower itself.

These macros cause the function to get placed in a *special section* where the section name determines where the hook has to go and of what type it is.
Before linking each object file is parsed, hooks are extracted and saved. At link time a linker file per processor is automatically generated and merges the hooks into the `.text` section. This causes the .elf to contain the hooks just like normal code while fireflower "knows" where it has to hook to those functions.

At the end `arm9.bin` gets patched with the hook information and another autoload region gets added, placing the new code into previous heap area.

Since fireflower also allows adding files and/or accessing them from code, the FNT gets extended with new directories (this only works if you place your files into a new directory).
It keeps old file IDs intact in order to avoid file system corruption during rebuild.

## Installation
Extract all tools into one directory using
```
nds-extract.exe YourRom.nds data/
```
This will dump the filesystem's contents into a `data` folder

Grab your configuration dependent on the game you're trying to patch and put it into the project tree's root.
Fireflower will need a file named `buildroot.txt` containing a path to your JSON configuration file so make sure to check
a) it's in the same directory as `fireflower.exe`
b) it points to the JSON with a relative path

You only need ARM's GCC to compile the code. Preferably install it into a subdirectory and link your JSON to the compiler binaries.
Additionally, ensure that a folder named `internal` is in the same directory as `fireflower.exe` containing `ffc.h`.

Finally, modify the build rules file for `nds-build` to point to `data/`.

If you set up everything correctly you can start writing code in the directories you specified in the configuration.
Then run
```
fireflower.exe
```
It should automatically rebuild the nds file. You can also specify other postbuild commands in the configuration.

**Remember to back up your original .nds file! Even though fireflower backs up all your files in `backup` together with uncompressed versions, your original .nds file won't match 1:1!**

In case the patching process failed, fireflower will inform you via the command line. Take warnings seriously, they might contain the reason why it failed.


## Bugs
Note that fireflower is not finished yet and needs a cleaner setup to enhance user experience. Things like overlay creation have been in an early alpha version which got
scrapped due to becoming overly complicated. Initially, different *overlay patching modes* were possible but both introduced difficulties 
with the linker and with the configuration, making setup hardly enjoyable. If you really need such features consider manually modifying the overlays.

Due to fireflower's vastly different nature and low-level-ness bugs can always appear especially with corrupted .elf files (which in the best case should never happen).
Most bugs will be *incredibly* rare or specific that even with debugging it's a chore to fix them.

Should you ever encounter a bug please *immediately* file an issue here on GH or send me an email to `overblade.dev@gmail.com`. Remember to carefully desribe the bug together with instructions on how to replicate it.
