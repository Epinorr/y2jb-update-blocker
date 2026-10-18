
<div align="center">
  <img src="assets/yt-blocker-banner.png" alt="YT-Blocker — Block YouTube Updates on PS5" width="100%">

  ![Platform](https://img.shields.io/badge/Platform-PlayStation%205-003791?style=for-the-badge&logo=playstation)
  ![Language](https://img.shields.io/badge/Language-C-A8B9CC?style=for-the-badge&logo=c)
  ![License](https://img.shields.io/badge/License-GPL--3.0-blue?style=for-the-badge&logo=gnu)

  **A PlayStation 5 payload for blocking YouTube updates**

  [Download Latest Release](../../releases/latest)
</div>

---

---

YT-Blocker modifies YouTube's application metadata on supported PS5 setups in an attempt to prevent the console from applying a YouTube update.

## Download

1. Open **[Releases](../../releases)**.
2. Select the latest release.
3. Under **Assets**, download **`YT-Blocker.elf`**.
4. Send the ELF payload to your PS5 using a payload loader compatible with your setup.

**Most users only need the release ELF.** You do not need to download the source code or build the project yourself.

## Before you use it

> [!WARNING]
> This payload modifies system databases and application metadata. Back up the original files before running it. Incorrect or partial changes may cause problems with the YouTube application.

- Close YouTube before running the payload.
- Do not run it while YouTube is installing or updating.
- A “Done” notification does **not** guarantee that updates are blocked. Verify the result on your console.
- Compatibility can vary by firmware, YouTube version, title ID, and payload environment. It has not been verified on every configuration.

## Supported YouTube title IDs

- `PPSA01650`
- `PPSA01651`
- `PPSA01652`

## What it changes

YT-Blocker attempts to set the following metadata values:

| Field | Target value |
| --- | --- |
| Content version | `99.999.999` |
| Version file URI | `http://127.0.0.2` |

The payload updates relevant entries in the PS5 application databases and YouTube `param.json` metadata. These changes alone may not be sufficient if the system restores its metadata or update services remain reachable.

YT-Blocker does **not** configure DNS or change your network settings.

## Credits

- [EPINOR](https://github.com/EPINOR) — project and payload
- [SvenGDK](https://gist.github.com/SvenGDK/6525ec4af64e45a847e35af45da46ab4) — YouTube metadata-blocking method
- [PS5 Payload SDK](https://github.com/ps5-payload-dev) — payload development tools

## License

YT-Blocker is distributed under the [GNU General Public License v3.0](LICENSE).
