# Kernel fuer das Redmi A5

Basis: https://codeberg.org/ums9230-mainline/linux.git
Basis-Commit: 4a5b97b821b846f9a25a99a4fdcb3040fd910965
Stand: bf4d1e14c Revert "DIAG nt36xxx: Pufferinhalt/verworfene Punkte/fwinfo ausgeben (voruebergehend)"

Bauen:
```
git clone https://codeberg.org/ums9230-mainline/linux.git && cd linux
git checkout 4a5b97b821b8
git am /pfad/zu/kernel/patches/*.patch
make ARCH=arm64 redmi_a5_defconfig
make ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- -j$(nproc) Image
```

Wichtig: Der Kernel muss klein bleiben (Platzbedarf deutlich unter 48 103 424 Bytes),
sonst startet er nach echtem Ausschalten nicht. Siehe STATUS.md.
