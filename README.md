# PRATEEKOS

**A small x86-64 operating system with a custom kernel, graphical desktop, calculator, text console and persistent file storage.**

Created by **Prateek Shrivastava** as a project for learning operating-system development. The kernel is written in C and x86-64 assembly and boots through Limine. Run it inside QEMU using the instructions below.

[Source code](https://github.com/Prateekshrivastava26/prateekOS) · [Releases](https://github.com/Prateekshrivastava26/prateekOS/releases) · [Report an issue](https://github.com/Prateekshrivastava26/prateekOS/issues)

> PRATEEKOS is an experimental learning OS. The instructions target QEMU on Windows/WSL and Linux. It has its own kernel and does not run Windows installers, Linux applications, Chrome or Android APKs. Direct installation on a physical PC and ARM hosts have not been validated.

## Features

- Graphical desktop, mouse pointer, taskbar and movable application window.
- File Manager: create, preview, edit and delete small disk text files.
- Calculator: addition, subtraction, multiplication, division, brackets and decimals.
- Text console with shell commands and memory tests.
- Settings: clock format, pointer speed and selectable wallpapers.
- Included Cheems wallpaper and a converter for importing other pictures.
- Current date/time from the virtual RTC, plus a separate uptime counter.
- System Monitor with managed RAM, free RAM and virtual-disk capacity.
- A persistent 5 GB virtual-disk file.
- Basic memory allocation, virtual-memory mapping, task-switching demonstrations and user-mode ELF program loading.

**Wireless support:** the current Wi-Fi/Bluetooth buttons open Windows Settings through an optional local helper. Native guest Wi-Fi, Bluetooth and internet networking are not implemented. Windows pairing does not make Bluetooth devices available to PRATEEKOS applications.

## Requirements

| Component | Requirement |
| --- | --- |
| Guest architecture | x86-64 / AMD64 |
| Recommended host | A Windows or Linux PC with an Intel/AMD 64-bit processor |
| Host memory | 8 GB recommended when running Windows, WSL and QEMU together |
| Guest memory | The launcher assigns 2 GiB |
| Storage | Space for a 5 GB virtual disk, plus build tools and downloads |
| Emulator | `qemu-system-x86_64` |
| Build environment | Ubuntu/WSL or a compatible x86-64 Linux system |
| Internet | Required on the host for downloading source dependencies |

The launcher grows a regular image file to **5,000,000,000 bytes**. This does not add physical RAM or create a Windows disk partition. The amount of host disk space immediately allocated depends on the host filesystem.

## Choose how to run

- **Try a published ISO:** follow [Run a release ISO](#run-a-release-iso). No compiler is needed.
- **Build the latest source:** follow [Build from source](#build-from-source). This also provides the wallpaper converter and optional Windows connection tools.

GitHub's **Code → Download ZIP** downloads source files, not a ready-to-boot operating system. If no release ISO has been published yet, build from source.

## Run a release ISO

### Download

1. Open [GitHub Releases](https://github.com/Prateekshrivastava26/prateekOS/releases).
2. If a release contains an `.iso` asset, download it.
3. Save or rename it as **`prateekos.iso`** in a folder reserved for PRATEEKOS.
4. Install QEMU using its [official download page](https://www.qemu.org/download/).

Keep your ISO and disk image together. The ISO contains the OS; your private disk image contains your saved files.

### Ubuntu/Linux or Ubuntu inside WSL

Install QEMU:

```bash
sudo apt update
sudo apt install -y qemu-system-x86 qemu-utils
```

Open a terminal in the folder containing `prateekos.iso`. Create a disk **only if one does not already exist**:

```bash
if [ ! -e prateekos-disk.img ]; then
    qemu-img create -f raw prateekos-disk.img 5000000000
fi
```

Launch:

```bash
TZ=Asia/Kolkata qemu-system-x86_64 -M pc -m 2G \
  -cdrom prateekos.iso -boot d \
  -drive file=prateekos-disk.img,format=raw,if=ide,index=0 \
  -rtc base=localtime,clock=host
```

Use this command again to restart with the same saved files. Change `Asia/Kolkata` to your preferred timezone if desired.

### Windows PowerShell, without WSL

Install the Windows QEMU distribution linked from the [QEMU download page](https://www.qemu.org/download/). Open PowerShell in your ISO folder.

These commands assume QEMU is installed in `C:\Program Files\qemu`. Adjust `$qemuDirectory` if you installed it elsewhere.

```powershell
$qemuDirectory = "C:\Program Files\qemu"

if (!(Test-Path .\prateekos-disk.img)) {
    & "$qemuDirectory\qemu-img.exe" create -f raw .\prateekos-disk.img 5000000000
}

& "$qemuDirectory\qemu-system-x86_64.exe" -M pc -m 2G `
  -cdrom .\prateekos.iso -boot d `
  -drive "file=prateekos-disk.img,format=raw,if=ide,index=0" `
  -rtc base=localtime,clock=host
```

Here, the RTC uses your Windows local time. The standalone ISO launch does not enable the buttons that open Windows connection settings; those require the source launcher under WSL.

## Build from source

### 1. Prepare Ubuntu

On Windows, install Ubuntu through WSL if you do not already have it. In **PowerShell as Administrator**:

```powershell
wsl --install -d Ubuntu
```

Restart if requested, open **Ubuntu** from the Start menu and complete the Linux username/password setup. If Ubuntu is already installed, open it directly.

The remaining commands in this section belong in **Ubuntu/WSL or a Linux terminal**, not in Windows PowerShell or the PRATEEKOS console.

### 2. Install tools

```bash
sudo apt update
sudo apt install -y \
  build-essential binutils clang lld nasm make git curl \
  qemu-system-x86 qemu-utils xorriso mtools python3 python3-pil
```

`sudo` asks for your Ubuntu password. Nothing appears while you type it.

### 3. Download the project

```bash
git clone https://github.com/Prateekshrivastava26/prateekOS.git
cd prateekOS
```

Public repositories can be cloned without logging into GitHub.

### 4. Fetch dependencies and build

```bash
sh kernel/get-deps
make
```

The dependency script populates the managed dependency folders. The build also downloads Limine when needed. A successful build produces:

```text
template.iso
```

This is the bootable ISO. Dependency downloads require internet access on the host.

### 5. Start PRATEEKOS

Use this portable command on Ubuntu/Linux or WSL:

```bash
PRATEEKOS_DISK="$PWD/prateekos-disk.img" make run
```

It creates or reuses a disk in the project folder and starts QEMU. Use the same command for future launches so that the same disk is attached.

On Windows/WSL with a mounted **D:** drive, this shorter command uses the launcher's default storage location:

```bash
make run
```

The default live disk is:

```text
D:\prateekos-storage\prateekos-disk.img
```

On its first use, the default launcher copies an existing project disk there, if present, before growing the copy. An existing destination image is reused and larger images are never shrunk. Your original project image is retained when it was copied to a different destination.

To choose another drive or timezone:

```bash
PRATEEKOS_DISK=/mnt/e/prateekos-disk.img make run
PRATEEKOS_DISK="$PWD/prateekos-disk.img" PRATEEKOS_TZ=Etc/UTC make run
```

After QEMU closes, return to the host terminal. There is currently no dedicated guest shutdown command.

## First start: initialize a new disk

This is needed only for a **new, unformatted virtual disk**.

1. Open **Terminal** in PRATEEKOS or press **F4**.
2. Type `diskls` and press Enter.
3. If the disk is new and reports that it is not formatted, type `diskformat`.
4. Type `desktop` to return to the GUI.
5. Open **Files** and create your first file.

> `diskformat` resets the PRATEEKOS file directory, including saved appearance settings. Do not run it each time you boot or on a disk whose files you want to preserve.

## Desktop controls

| Key/action | Function |
| --- | --- |
| Click an icon | Open its application |
| Drag the title bar | Move the current window |
| X or Esc | Close a window; Esc cancels editing |
| F1 | Shortcut guide |
| F2 | File Manager |
| F3 | System Monitor |
| F4 | Terminal |
| F5 | Refresh the file list |
| F6 | Settings |
| F7 | Calculator |
| Enter on the desktop | Terminal |
| Ctrl+Alt+G | Release QEMU's captured keyboard/mouse |

Application shortcuts operate outside the text editor. Save or cancel editing before switching applications. Some laptop keyboards require **Fn** with function keys.

### File Manager

Files are marked **[R]** for built-in RAM files and **[D]** for writable disk files.

- **New / N:** type a filename, press Enter, type the contents, then press Ctrl+S or click Save.
- **Edit / E:** select a [D] file, edit it and save.
- **Delete / D:** select a [D] file and confirm by clicking Delete again or pressing D again.
- **J / K:** select the next/previous file and scroll the list.
- **Tab:** switch between the filename and text fields.

Current disk filesystem limits:

| Item | Limit |
| --- | --- |
| Files | 15 total |
| Filename | 1–27 characters |
| Text per file | Up to 512 bytes |
| Directory structure | Flat names; no folders |

The 5 GB device capacity does **not** remove these filesystem limits. Saving appearance preferences creates `prateek-ui.cfg`, which occupies one file slot. New or edited RAM files are temporary and disappear when the VM restarts.

### Calculator

Click **Calculator** or press **F7**. Use the mouse keypad or type an expression such as:

```text
2+3*4
(2+3)*4
12.5/2
```

Press **Enter** or `=` to calculate. Press **C** to clear and Backspace to delete. Brackets, unary signs and up to four decimal places are supported. Results truncate to four decimal places. Very large intermediate calculations display an error. This is a basic calculator, not a scientific calculator.

### Wallpaper and appearance

Open **Settings / F6**. Click **Wallpaper**, or press **G**, to cycle through **Custom, Ocean, Sunset and Forest**. Custom uses the included Cheems image by default.

Click **Save**, or press **S** in Settings, to remember the wallpaper, clock format and pointer speed on your disk. Formatting must have succeeded and a file slot must be available.

To import another picture, close QEMU and run these commands **on the host**, in the project folder:

```bash
python3 scripts/set_wallpaper.py /mnt/d/my-picture.jpg
PRATEEKOS_DISK="$PWD/prateekos-disk.img" make run
```

Replace the path with your picture's actual path. If you use the default D: disk, launch with `make run` instead. Select **Custom** in Settings after boot.

The converter accepts Pillow-supported formats, including JPEG, PNG and WebP, and creates `ramfs/wallpaper.bmp`. It limits the image to 1600×1000 and preserves its proportions. To restore the included image, if that asset is present:

```bash
python3 scripts/set_wallpaper.py assets/wallpaper-original.webp
```

### Date, time and memory

The taskbar shows the virtual hardware clock's date and time. The source launcher defaults to **Asia/Kolkata (IST)**. Clock format is selectable in Settings.

Uptime is a separate timer-based measurement. If uptime remains zero while the date/time updates, the timer interrupt path needs debugging.

System Monitor reports the RAM managed by the page allocator, rather than every byte of installed host memory. Disk size appears in decimal MB: **5000 MB = 5 GB**.

### Wi-Fi and Bluetooth tools

With Windows/WSL and the updated source launcher, Settings can open your **Windows Wi-Fi** and **Windows Bluetooth** pages. Select a network, enter its password or pair a device in the Windows window. Use Alt+Tab to switch back if needed.

These are host connection tools. The PRATEEKOS kernel does not yet have native wireless drivers, Bluetooth pairing support or a working internet stack. The controls report unavailable on unsupported hosts or when Windows/WSL interop is unavailable.

You can also launch the Windows pages directly from Ubuntu/WSL:

```bash
python3 scripts/host_tools.py --wifi
python3 scripts/host_tools.py --bluetooth
```

## Console commands

Run these inside the **PRATEEKOS Terminal**. Ubuntu commands such as `sudo`, `apt`, `git`, `make` and `readelf` belong on the host and will not work in this shell.

| Command | Purpose |
| --- | --- |
| `help` | Show the main command list |
| `clear` | Clear the console |
| `about` | Show OS information |
| `desktop` | Return to the GUI |
| `ls` | List RAM files |
| `cat etc/motd.txt` | Read a RAM file |
| `write docs/note.txt hello` | Create/update a temporary RAM text file |
| `rm docs/note.txt` | Remove a RAM file |
| `diskls` | List persistent disk files |
| `diskwrite note.txt hello` | Create/update a disk text file |
| `diskcat note.txt` | Read a disk text file |
| `diskrm note.txt` | Delete a disk file |
| `diskformat` | Initialize/reset the disk filesystem directory |
| `disktest` | Write/check a persistent test marker |
| `mem` | Show page allocator statistics |
| `alloc` / `free` | Allocate/free the test page |
| `heaptest` | Exercise heap allocation |
| `vmtest` | Exercise a virtual-memory mapping |
| `tasktest` | Run the task-switching demonstration |
| `usertest` | Run the bundled user-mode ELF module |
| `cheems bin/hello.elf` | Run the bundled program from RAM storage |

The current console input buffer holds up to 79 characters per command. The test commands are development demonstrations, not a complete application or process-management system.

## Save files and back up your disk

Disk files persist only when you restart with the **same image path**. The ISO and RAM filesystem do not contain your later disk edits.

Close QEMU before copying or resizing its disk. For a project-local disk on Linux/WSL:

```bash
cp --sparse=always prateekos-disk.img prateekos-disk-backup.img
```

If you use the default D: disk, back up the image in `D:\prateekos-storage` instead. Keep personal disk images out of public GitHub commits and releases.

`make clean` removes generated build outputs, including `template.iso` and `initramfs.tar`. It does not delete `prateekos-disk.img` or the default D: disk.

## Troubleshooting

| Problem | What to check |
| --- | --- |
| `make`, `nasm`, `xorriso` or QEMU not found | Install the Ubuntu packages listed above. |
| `Please run the ./get-deps script first` | From the project root, run `sh kernel/get-deps`, then `make`. |
| Missing Limine/protocol headers | Fetch dependencies; check host internet access. |
| D: is not mounted | Use `PRATEEKOS_DISK="$PWD/prateekos-disk.img" make run`. |
| A disk is reported as in use | Close other QEMU windows using that image before restarting. |
| Disk test or formatting fails | Attach the image as IDE index 0 and use `-M pc`, as in the documented commands. |
| Saving a file fails | Check disk formatting, the 15-file limit, filename length and text length. |
| Saved files disappeared | Check that you reopened the same disk image and used the disk commands rather than RAM commands. |
| No Wallpaper control or Calculator icon | Update the source and run `make clean` followed by your usual launch command. |
| Custom wallpaper is missing | Confirm `ramfs/wallpaper.bmp` exists, select Custom and rebuild the ISO. |
| Importer cannot find Pillow | Run `sudo apt install python3-pil` in Ubuntu. |
| Keyboard characters appear when moving the mouse | Update the keyboard driver; it must reject auxiliary-device bytes. |
| Mouse stays captured | Press Ctrl+Alt+G. |
| Wi-Fi/Bluetooth controls are unavailable | They require the updated launcher on Windows/WSL with working Windows executable interop. |

If a problem remains, [open an issue](https://github.com/Prateekshrivastava26/prateekOS/issues) with your host OS, launch command, full build error and the feature you were using. Do not include passwords or access tokens.

## Project layout

| Path | Contents |
| --- | --- |
| `kernel/src/` | Kernel, drivers, desktop, calculator and wallpaper decoder |
| `kernel/src/arch/x86_64/` | Interrupt, GDT and context-switch assembly/code |
| `kernel/linker-scripts/` | Kernel linker configuration |
| `kernel/get-deps` | Dependency download script |
| `user/` | Bundled user-mode program and linker script |
| `ramfs/` | Files and wallpaper packaged into the boot archive |
| `scripts/` | Disk preparation, host tools and wallpaper converter |
| `run-prateekos.sh` | QEMU launcher |
| `limine.conf` | Boot menu and boot-module configuration |
| `GNUmakefile` | Main build rules |

## Build targets

| Command | Result |
| --- | --- |
| `make` | Build `template.iso` |
| `make run` | Build and launch with the default D: disk |
| `PRATEEKOS_DISK="$PWD/prateekos-disk.img" make run` | Build and launch with a project-local disk |
| `make clean` | Remove generated outputs; keep saved disk images |

Additional HDD/UEFI targets inherited from the Limine template remain in the makefile. They use different launch paths; the supported walkthrough above uses the PC/IDE ISO configuration.

## Publish a downloadable release — maintainer

Build and prepare the ISO from the project root:

```bash
make
cp template.iso prateekos.iso
sha256sum prateekos.iso > SHA256SUMS
```

On GitHub, open **Releases → Draft a new release**, choose a version tag such as `v0.1`, and attach **`prateekos.iso`** and **`SHA256SUMS`**. Include the version's known limitations in the release notes. A GitHub source upload alone does not publish an ISO.

Publish the bootable ISO instead of your personal `.img` disk. Recipients can create a clean disk using the instructions above. Keep generated `.o`, `.elf`, `initramfs.tar`, emulator logs and downloaded dependency folders out of source commits when possible; the build recreates them.

## Current limitations and validation

- A custom learning kernel with limited hardware support and incomplete process isolation.
- QEMU PC/IDE, PS/2 input and a 32-bit RGB framebuffer are the intended configuration.
- No native Wi-Fi/Bluetooth, general internet access, browser, package manager or general-purpose application compatibility.
- Small filesystem and console limits as documented above.
- Builds can fetch a newer Limine binary release; record working dependency versions for reproducible published releases.

Changed kernel sources have been compiled with the project's freestanding flags. Host tests have exercised calculator behavior, BMP validation, simulated GUI input, appearance settings, disk-image growth and helper protocols. These tests do not replace booting and testing the published ISO on the target host. Treat hardware support outside the documented QEMU configuration as unverified.

## Credits and license

- PRATEEKOS development: **Prateek Shrivastava**.
- Bootloader/protocol and original project template: [Limine](https://github.com/Limine-Bootloader).
- Freestanding support: [osdev0](https://github.com/osdev0).
- Bitmap font: Daniel Hepper's [font8x8](https://github.com/dhepper/font8x8), with its public-domain attribution retained in the source.
- Included Cheems wallpaper: supplied for this project by the project author.

See [LICENSE](LICENSE) and the notices in individual dependencies for licensing terms. Preserve the original template's copyright notice and third-party notices when redistributing the source.

## Useful documentation

- [QEMU downloads](https://www.qemu.org/download/)
- [QEMU system invocation](https://qemu-project.gitlab.io/qemu/system/invocation.html)
- [Microsoft WSL installation](https://learn.microsoft.com/en-us/windows/wsl/install)
- [Windows Settings URI reference](https://learn.microsoft.com/en-us/windows/apps/develop/launch/launch-settings)
