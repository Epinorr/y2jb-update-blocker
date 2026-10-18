# 🛡️ PS5 YT-Blocker Payload

![Platform](https://img.shields.io/badge/Platform-PS5-003791?style=for-the-badge&logo=playstation)
![Language](https://img.shields.io/badge/Language-C-A8B9CC?style=for-the-badge&logo=c)
![License](https://img.shields.io/badge/License-GPLv3-blue?style=for-the-badge)

**YT-Blocker** is an automated, lightweight PlayStation 5 ELF payload designed for **Y2JB (YouTube Jailbreak)** users. It permanently prevents the PS5 YouTube application from auto-updating or resetting its metadata when connecting to the internet, protecting your console from softlocks and forced updates.

---

## ✨ Features

* **Multi-Region Support:** Automatically detects and patches both **`PPSA01650`** and **`PPSA01651`** Title IDs.
* **Version Agnostic:** Works across all YouTube app versions (specifically tested and optimized for **v01.000.003** and **v01.000.030**).
* **Complete Deep Patching:** Unlike basic blockers that only edit `appinfo.db`, this payload patches all 4 critical system locations simultaneously so the PS5 OS cannot restore the original state.
* **All Jailbroken Firmwares:** Built with `ps5-payload-sdk`, ensuring full compatibility across all jailbroken PS5 firmwares.
* **Instant Feedback:** Triggers a native PS5 system notification upon completion.

---

## 🔧 How It Works (Technical Overview)

Simply modifying `appinfo.db` is not enough—once the console connects to the internet without a DNS block, the system refreshes the app state and overwrites the changes. **YT-Blocker** resolves this by spoofing the version to `99.999.999` and redirecting the update URI to `http://127.0.0.2` across all four system locations:

| Target File / Database | Path | Patched Keys / Columns |
| :--- | :--- | :--- |
| **`appinfo.db`** | `/system_data/priv/mms/appinfo.db` | `CONTENT_VERSION` & `VERSION_FILE_URI` in `tbl_appinfo` |
| **`app.db`** | `/system_data/priv/mms/app.db` | `$.CONTENT_VERSION` & `$.VERSION_FILE_URI` inside `AppInfoJson` (`tbl_contentinfo`) |
| **System `param.json`** | `/system_data/priv/appmeta/{TITLE_ID}/param.json` | `contentVersion` & `versionFileUri` |
| **User `param.json`** | `/user/appmeta/{TITLE_ID}/param.json` | `contentVersion` & `versionFileUri` |

---

## 🚀 Usage Instructions

1. Download the latest **`YT-Blocker.elf`** from the [Releases](../../releases) page (or from the **Actions** build artifacts).
2. Make sure YouTube (`PPSA01650` or `PPSA01651`) is installed on your PS5 and **closed**.
3. Send `YT-Blocker.elf` to your PS5 using your preferred ELF loader (e.g., via Netcat on port `9021`):
   ~~~bash
   nc -q 0 <PS5_IP> 9021 < YT-Blocker.elf
   ~~~
4. Wait for the on-screen notification:

5. **Restart your PS5** for the database and metadata changes to take full effect.

---

## 🛠️ Building from Source

### Automated Build (GitHub Actions)
This repository includes a pre-configured GitHub Actions workflow (`.github/workflows/build.yml`). Every push to the `main` branch automatically:
1. Installs the latest `ps5-payload-sdk`.
2. Fetches the required `SQLite3` (with `JSON1` enabled) and `cJSON` amalgamation sources.
3. Compiles and packages `YT-Blocker.elf` as a downloadable artifact.

### Local Build
If you want to compile it locally on Linux:
~~~bash
export PS5_PAYLOAD_SDK=/opt/ps5-payload-sdk
make clean && make
~~~

---

## 👏 Credits

* **[EPINOR](https://github.com/EPINOR)** — Payload creator & developer.
* **[SvenGDK](https://gist.github.com/SvenGDK/6525ec4af64e45a847e35af45da46ab4)** — Research and discovery of the complete Y2JB YouTube update block method.
* **[John Törnblom](https://github.com/ps5-payload-dev)** — For the `ps5-payload-sdk` toolchain.

---

## 📄 License

This project is licensed under the **GNU General Public License v3.0 (GPL-3.0)** — see the [LICENSE](LICENSE) file for details.
