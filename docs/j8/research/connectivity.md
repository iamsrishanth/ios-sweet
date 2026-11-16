# J8 connectivity and basic peripherals: Wi-Fi, Bluetooth, cellular, audio

Supporting research for the ios-sweet port to SM-J810G (j8y18lte, msm8953 / Snapdragon 450):
what the radios and codecs are, what Android does with them today, and what XNU would have to
supply before iOS sees any of it. Labels: measured (read off this phone), documented (cited),
inferred, unverified. External sources are numbered at the end.

## 0. Evidence base, and what it cannot tell you

- [dmesg-boot.txt](repo/evidence/2026-10-30-first-boot/dmesg-boot.txt) is a **tail**: first
  timestamp 30.94 s, 891 lines, AVC denials stripped by its capture script. Probes for hci_qca,
  lpass, qmi and the subsystem PILs all happen earlier, so **absence of a line is not evidence of
  absence**. [getprop.txt](repo/evidence/2026-10-30-first-boot/getprop.txt) is a full dump from
  the same boot and is the strongest local evidence of which stack is in use. The blob manifest of
  the pinned device tree ([proprietary-files.txt](<https://raw.githubusercontent.com/goldfish07/android_device_samsung_j8y18lte/1b4f25eff98554feac5c0ab945c70be6589c3693/proprietary-files.txt>),
  pin 1b4f25e per [device.md](repo/docs/device.md)) records which userspace radio and audio stacks
  the stock firmware shipped.

## 1. What the log and properties show

| Subsystem | Evidence | Reading |
|---|---|---|
| WLAN | driver.version 3.0.11.85, firmware.version CNSS-PR-4-0-3-0000314, driver.config /data/vendor/wifi/WCNSS_qcom_cfg.ini, driver.ath 0, status ok; the only radio driver that logs | qcacld ("prima") loaded and talking to firmware; integrated WCNSS, not ath10k/QCA6174 |
| Bluetooth | vendor.qcom.bluetooth.soc pronto, ro.bt.bdaddr_path /efs/bluetooth/bt_addr, vendor.bluetooth-1-0-qti running | WCNSS BT core, MAC from efs |
| Modem | vendor.peripheral.modem.state ONLINE, ro.vendor.ril.mbn_copy_completed 1, rild.libargs -d /dev/smd0, "Samsung RIL v4.0", dsds, ril.hasisim 0,0 | modem booted by PIL, rild speaks QMI over SMD, no SIM fitted |
| Subsystems | subsys-restart: subsys_stop(): subsys_stop venus (line 553) | the PIL/subsys driver that would boot WCNSS/ADSP/modem exists |
| Audio | audio-hal-2-0 and audioserver running, adsprpcd running, Fluence voicecall/speaker true | ADSP-based Qualcomm audio; no codec lines in the tail |

From [dmesg-boot.txt](repo/evidence/2026-10-30-first-boot/dmesg-boot.txt#L354-L355):

> [ 32.415871] [04:32:32.220424] [00000000283F7AAE] [VosMC] wlan: [E :PE ] Switching channel to 9
> [ 32.426207] [04:32:32.230759] [00000000284281D1] [VosMC] wlan: [E :PE ] limProcessSwitchChannelRsp: 4153: channelChangeReasonCode 2 status 0

Lines 31.940678-31.941149 then set the regulatory domain to IN (2.4 GHz plus 5170-5835 MHz), so
a wiphy is already registered. The only PCIe-shaped activity in the capture is the USB3
controller: no root-complex enumeration, no cnss2 line (measured, subject to the 30.94 s
caveat). The cnss-daemon failure repeats on lines 391-397, 555-562, 628-635, 712-719, 785-792
and 884-891 as "cannot execve('/system/vendor/bin/cnss-daemon'): Operation not permitted".

## 2. Wi-Fi: WCNSS/prima confirmed, QCA6174-over-PCIe refuted

**Verdict: confirmed.** The J8 runs the integrated WCNSS block in the "prima" family, and the
sailfish driver (patches 0169-0179, bsd/dev/sailfish_wlan.c) is **not reusable**.

1. Documented: on msm8953 the WCNSS supplies **both** Wi-Fi and Bluetooth through an external RF
   module of the wcn3660b class, with its own rails vddpx, vddxo, vddrfa, vddpa, vdddig [1]. A
   companion subsystem, not a PCIe endpoint.
2. Documented: the WCNSS is an ARM9 inside the SoC, firmware verified and booted through the
   TrustZone peripheral authentication service (PAS ID 6, image wcnss.mdt), host interface a set
   of MMIO register blocks named pmu, ccu and dxe [3].
3. Documented: after boot the host drives the WCNSS_CTRL service (version request, NV download,
   calibration, cold-boot-complete, PM config) and pushes wlan/prima/WCNSS_qcom_wlan_nv.bin in
   3072-byte fragments [2]; mainline documents the same NV file [5].
4. Measured: the blob manifest [9] has **no** bdwlan.bXX, athwlan.bin, otp.bin or qca6174
   firmware, no ath10k userspace and no 64-bit cnss2 stack, while it does carry prima-era
   userspace: libwifi-hal-qcom.so, supplicant and hostapd HIDL 2.x, cnss-daemon, cnss_diag,
   athdiag, libfm-hci.so (FM over HCI, a WCNSS feature).
5. Measured: the only driver trace is firmware-side policy-engine traffic (VOS/PE tags), which
   is qcacld, not ath10k.

**Why sailfish cannot be reused.** Its own headers describe the topology: "power the QCA6174
(Rome) and bring up PCIe root complex 0, its only host interface, then read the chip's
config-space ID", citing the Pixel kernel's cnss_pci.c and pci-msm.c
([0170](work/refs/ios-sailfish/xnu/patches/0170-sailfish-wlan-firmware.patch)). It hard-codes
BAR0 at 0x0c400000, a DMA arena at 0xa0000000, the anoc0 SMMU at 0x01640000, CE ring registers
inside BAR0, and the ath10k-style BMI firmware transfer over the bus
([0171](work/refs/ios-sailfish/xnu/patches/0171-sailfish-wlan-htc-wmi.patch)). On a J8 there is
no root complex, no BAR, no WLAN SMMU context and no host-side firmware push.

### What a WCNSS driver must implement on msm8953

1. **Rails and clocks**: vddpx, vddxo, vddrfa, vddpa, vdddig plus XO and RF clocks [1][3], i.e.
   RPM/RPMh votes from XNU. The sweet tree has the primitive (sweet_rpm.c) but the J8 PMIC is a
   PM8953, not the PM8994 that code was written against.
2. **Firmware boot**: reserve and map the WCNSS region, load wcnss.mdt plus b00..bNN segments,
   authenticate through SCM (PAS ID 6) [3]. Same shape as modem and ADSP, so build it once.
3. **WCNSS_CTRL**: version, NV download in 3072-byte fragments, calibration, cold-boot-complete,
   PM config [2]. Without this the core never leaves reset.
4. **Host data path**: either the DXE rings mainline wcn36xx uses, or the CE/HTC/WMI scheme
   qcacld uses. Which one this Samsung kernel expects is **unverified** (3.0.11.85 names the
   driver, not its ring layout). Either way XNU needs descriptor allocation, interrupts and a
   command/event codec.
5. **Configuration**: parse the cfg ini (device points at /data/vendor/wifi/WCNSS_qcom_cfg.ini)
   or hard-code the subset needed.
6. **Scan**: channel list, probe request/response, beacon and IE parsing into the records wifid
   consumes.
7. **Join and WPA2**: association plus the 4-way handshake. In the qcacld model the firmware
   associates and handshakes with a PMK handed down; in the mainline model the host does it.
   Sailfish had to put PBKDF2-HMAC-SHA1, the 802.11 PRF, EAPOL-Key parsing and AES unwrap **in
   the kernel** ([0173](work/refs/ios-sailfish/xnu/patches/0173-sailfish-wlan-connect.patch))
   because ath10k expected the host to be the supplicant. Whether WCNSS firmware accepts the
   "here is the PMK" style is unverified; if not, that crypto lands in XNU too.
8. **Apple-facing face**: an IOEthernetController subclass publishing an interface with
   setInterfaceSubType(kIONetworkInterfaceSubTypeWiFi) before registerService (wifid matches on
   it), an IO80211Interface subclass overriding performCommand, the SIOCGA80211/SIOCSA80211
   surface (POWER 19, SCAN_REQ 10, SCAN_RESULT 11, ASSOCIATE 20, ASSOCIATE_RESULT 21,
   DISASSOCIATE 22, BSSID 9, SSID 1, STATE 13, RSSI 16, CHANNEL 4 and friends), and events on
   KEV_VENDOR_APPLE class 6 subclass 1
   ([0179](work/refs/ios-sailfish/xnu/patches/0179-sailfish-wlan-apple80211.patch)).

Reusable from sailfish: items 6-8 and the IONetworkingFamily staging approach
([0175-stage-ionetworking.sh](work/refs/ios-sailfish/xnu/patches/0175-stage-ionetworking.sh)),
which stages IONetworkingFamily-170.140.2 at a pinned commit with four textual fixes; but it
hard-fails unless MACOS_VERSION is 13.5 (xnu-8796.141.3) while ios-sweet tracks xnu-8796.101.5,
so the guard needs redoing. Everything below the Apple interface is new code. Risk: high, and
unverifiable until firmware download works, because every later stage depends on it.

## 3. Bluetooth

Measured: vendor.qcom.bluetooth.soc = pronto, MAC at /efs/bluetooth/bt_addr, QTI BT HAL 1.0
running, and libbt-hidlclient.so, btconfigstore@1.0, libbluetooth_audio_session.so and
Bluetooth_cal.acdb in the vendor tree. Documented: on msm8953 the BT core is part of the same
WCNSS subsystem as WLAN [1], and it has no persistent BD-address storage, so the platform
supplies that [4] (matching the /efs path here).

Transport, documented for WCNSS BT: two shared-memory channels named APPS_RIVA_BT_CMD and
APPS_RIVA_BT_ACL exposed as an HCI device of bus type HCI_SMD [4]. The alternative shape for this
era is a high-speed UART running hci_qca with the "pronto"/"rome" handling. Which one the Samsung
3.18 kernel uses is **unverified** (no hci or smd line in the captured tail): check dmesg,
/sys/bus/smd/devices and the pinned defconfig (CONFIG_BT_QCOMSMD versus CONFIG_BT_HCIUART_QCA).
Firmware/NVM: the manifest has no crbtfw*.tlv or crnv*.bin, so BT firmware is either inside the
WCNSS image or on the modem firmware partition; which file, and who uploads it, is unverified.

What an iOS-facing stack needs: an HCI transport in the kernel, a controller service of the class
Apple's stack binds to, and Apple-compatible firmware upload. Measured in xnu-8796: there is
**no** Bluetooth controller or HCI transport code at all, only kdebug codes (DBG_IOBLUETOOTH,
DBG_DRVBLUETOOTH) and bpf's DLT_BLUETOOTH_HCI_H4 constants; IOBluetoothFamily is closed-source
and absent. The iOS 16 userland does contain bluetoothd, and on sweet it relaunches continuously
with no controller behind it ([NOTES.md](ios-sweet/NOTES.md), measured). Apple's firmware uploader
for Broadcom-class parts is BlueTool [8]; a WCNSS part would need an equivalent path Apple's stack
recognises (inferred, unverified). Verdict: not on the critical path; park it. Risk: very high.

## 4. Cellular

Measured: modem subsystem ONLINE (booted and healthy **right now**, by Android),
ro.vendor.ril.mbn_copy_completed 1, "Samsung RIL v4.0", rild.libargs -d /dev/smd0, rild.libpath
/vendor/lib64/libril-qc-qmi-1.so, rild.libpath2 /vendor/lib/libsec-ril-dsds.so,
persist.radio.multisim.config dsds with stackid 0,1, ril.halservice.registered.slot1/2 true,
ril.modem.board SDM450, ril.sw_ver J810GDDU6CUI1. The vendor tree also ships libqmi*.so,
libqmi_client_qmux.so, ATFWD-daemon and the iwlan/qmi HIDL jars. No SIM is fitted
(ril.hasisim 0,0; also [HANDOVER-2026-11-15.md](HANDOVER-2026-11-15.md) section 1). One oddity:
rild.libpath points at a **64-bit** vendor library while the ROM is a 32-bit userspace with no
/system/lib64 (unverified whether /vendor/lib64 exists).

Prerequisites for a call, in order:

1. **Modem boot from XNU**: carve-out mapping, mdt/ELF segment loading, PAS through SCM,
   watchdog and stop handshake. Android's PIL does this today; the log proves the kernel facility
   exists (subsys_stop venus, line 553), not that XNU has one.
2. **A transport**: here QMI multiplexed over SMD (rild is handed /dev/smd0, links QMUX). XNU
   needs SMD/IPCR shared-memory channels (discovery from SMEM, FIFO state machine, interrupts)
   before one QMI byte moves. GLINK/QRTR and IPA are later designs and are **not evidenced**
   anywhere in this device's properties.
3. **SIM/EFS access**: /efs is a 16 MiB partition of identity and calibration
   ([device.md](repo/docs/device.md) section 4) that must never be written; reading it needs
   eMMC plus Samsung's layout.
4. **A telephony stack iOS accepts**: CommCenter is the iPhone telephony daemon and talks to the
   baseband over Apple Remote Invocation (ARI), with per-baseband-vendor command-driver libraries
   (libARI, libARIServer plus Intel or Qualcomm drivers) [7]. A Snapdragon modem that only speaks
   QMI is invisible to that interface; the port would need a translator plus a kernel service
   that looks like the transport CommCenter binds to. The sweet port already had to remove
   CommCenter from launchd.plist to stop Setup blocking on CoreTelephony
   ([NOTES.md](ios-sweet/NOTES.md)).
5. **Voice routing**: call audio needs the ADSP voice paths (Fluence is enabled for voicecall
   here) and the codec routes of section 5. Without audio a call connects silent.

**Why modem-detected is not calls-work.** vendor.peripheral.modem.state = ONLINE says only that
Android's PIL released the modem from reset and QMI is flowing to Samsung's rild. It says
nothing about XNU, which has no modem boot, no SMD, no QMI client, no SIM access and no
ARI-facing daemon; even a working QMI stack would sit behind the CommCenter/ARI wall. Cellular
is out of scope for a first usable port. Risk: extreme.

## 5. Audio

Measured: vendor audio HAL 2.0 and audioserver running, adsprpcd running (ADSP RPC),
persist.vendor.audio.fluence.voicecall and .speaker true, ro.vendor.audio.sdk.fluencetype none,
vendor.voice.path.for.pcm.voip true. Vendor tree: vendor/lib/hw/audio.primary.msm8953.so,
audio_platform_info.xml (plus _diff and _extcodec variants), mixer_paths_wcd9335.xml among QRD
leftovers, many .acdb calibration files including Bluetooth_cal.acdb, and
vendor/firmware/Tfa9896.cnt.

Codec and DSP (inferred, documented where marked): a Qualcomm WCD-class codec of the WCD9335
family, a standalone Slimbus (or I2S) codec with seven DACs, nine interpolators, MBHC, class-H
and mic-activity detection [6]; plus an NXP TFA9896 smart speaker amplifier (its .cnt container
firmware is in the vendor tree); driven by the msm8953 audio HAL over the ADSP (LPASS) with
APR/RPC, calibrated by Samsung's acdb and platform-info files.

Paths to build: earpiece = WCD EAR/RX, mono, from the ADSP receive port over a Slimbus RX port.
Speaker = PCM/I2S from the LPASS into the TFA9896, which needs its .cnt firmware and an I2C
control channel before it makes sound. Mic = WCD analog mic (or DMIC) capture with Fluence on
the ADSP, plus the modem voice path for calls. Headset = MBHC detection in the codec, which
also decides the 4-pole jack type.

Missing kernel driver: XNU has no Slimbus controller, no LPASS/ADSP PIL and APR, no I2S DMA, no
WCD codec driver, no TFA989x driver and no Apple audio service to hand to mediaserverd. Not
hypothetical: on sweet, mediaserverd crashed in a loop inside the HAL plug-in VirtualAudio.plugin
because there was no audio hardware, no driver and no /product/audio node, and the stopgap was
moving the plug-in out of /Library/Audio/Plug-Ins/HAL/ ([NOTES.md](ios-sweet/NOTES.md),
measured). Risk: high, but the smallest of the four: a mono speaker plus wired-headset path is
contained once RPM, ADSP PIL and a Slimbus controller exist.

## 6. Summary

| Feature | J8 hardware | Android stack | sailfish equivalent | Reusable? | XNU work required | Risk |
|---|---|---|---|---|---|---|
| Wi-Fi | WCNSS + wcn3660b-class RF [1] | qcacld 3.0.11.85, fw CNSS-PR-4-0-3 | QCA6174 over PCIe RC0, ath10k-style (0169-0179) | only the Apple-facing 20 % | rails/XO, PAS boot of wcnss.mdt, WCNSS_CTRL + NV, DXE or CE/HTC/WMI, scan/join/4-way, Apple80211 + IONetworking | high |
| Bluetooth | WCNSS BT core, "pronto", MAC in /efs | QTI BT HAL 1.0, btconfigstore, hci_qca or HCI-SMD | none | nothing | HCI transport, controller service, firmware upload, closed Apple stack | very high |
| Cellular | Snapdragon modem, QMI over /dev/smd0 | Samsung RIL v4.0, libqmi/QMUX, ATFWD, PIL | none | nothing | modem PIL, SMD/IPCR, QMI client, SIM/EFS, ARI translation, voice routing | extreme |
| Audio | WCD9335-family Slimbus codec + TFA9896 [6] | HAL 2.0, msm8953 audio HAL, ADSP RPC, Fluence | none (sweet had no audio) | nothing | RPM votes, ADSP PIL + APR, Slimbus controller, codec and amp drivers, Apple audio service | high |

Planning note: Wi-Fi and audio both stand on the same two primitives, a working RPM/RPMh vote path
and a working PAS/PIL loader. Prove those once (ADSP or WCNSS, whichever is less risky to brick)
before starting either driver.

## Sources

1. msm8953-flipkart-rimob: Enable WiFi/Bluetooth (WCNSS with qcom,wcn3660b and its vddpx/vddxo/vddrfa/vddpa/vdddig rails). https://lkml.iu.edu/hypermail/linux/kernel/2603.1/00245.html — accessed 2026-11-17
2. drivers/soc/qcom/wcnss_ctrl.c (WCNSS_CTRL: version, NV download in 3072-byte fragments from wlan/prima/WCNSS_qcom_wlan_nv.bin, calibration, cold boot). https://raw.githubusercontent.com/torvalds/linux/master/drivers/soc/qcom/wcnss_ctrl.c — accessed 2026-11-17
3. "Qualcomm WCNSS remoteproc" series (WCNSS is an ARM9 inside the SoC; PAS in TrustZone, PAS ID 6; wcnss.mdt; pmu/ccu/dxe register blocks). https://lore.kernel.org/linux-arm-kernel/1459222625-11440-6-git-send-email-bjorn.andersson@linaro.org/t.atom — accessed 2026-11-17
4. drivers/bluetooth/btqcomsmd.c (qcom,wcnss-bt: HCI over APPS_RIVA_BT_CMD and APPS_RIVA_BT_ACL; no persistent BD address in the controller). https://git.progress-linux.org/horok/linux/src/commit/330561d860f6d003dcfdbd5d75c144e0e07295e5/drivers/bluetooth/btqcomsmd.c — accessed 2026-11-17
5. Linux wireless documentation, wcn36xx (loads WCNSS_qcom_wlan_nv.bin; WCN3660/WCN3680). https://wireless.docs.kernel.org/en/latest/_sources/en/users/drivers/wcn36xx.rst.txt — accessed 2026-11-17
6. "ASoC: Add support to WCD9335 Audio Codec" cover letter and Kconfig (standalone Hi-Fi codec, Slimbus or I2S/I2C, seven DACs, MBHC). https://lore.kernel.org/alsa-devel/20190128142753.22396-2-srinivas.kandagatla@linaro.org/t/#u — accessed 2026-11-17
7. "ARIstoteles: Dissecting Apple's Baseband Interface" (CommCenter talks to the baseband over Apple Remote Invocation, with per-vendor command-driver libraries). https://arxiv.org/html/2604.23457 — accessed 2026-11-17
8. Infineon application note, "Downloading Firmware to CYW20733 Modules Using BlueTool" (BlueTool as the Apple-side BT firmware downloader). https://www.infineon.com/dgdl/Infineon-AN214858_Downloading_Firmware_to_CYW20733_Modules_Using_BlueTool-ApplicationNotes-v03_00-EN.pdf — accessed 2026-11-17
9. goldfish07 pinned blob manifest for j8y18lte (device tree 1b4f25e), filtered for WLAN/BT/audio/modem entries. https://raw.githubusercontent.com/goldfish07/android_device_samsung_j8y18lte/1b4f25eff98554feac5c0ab945c70be6589c3693/proprietary-files.txt — accessed 2026-11-17
