# Kernel fuer das Redmi A5

Basis: https://codeberg.org/ums9230-mainline/linux.git
Basis-Commit: 4a5b97b821b846f9a25a99a4fdcb3040fd910965
Stand: d745a45cf Redmi A5: NVMEM_RMEM eingebaut - Temperatursensoren starten von selbst (11 Zonen, critical 110C)

Bauen:
```
git clone -b ums9230 https://codeberg.org/ums9230-mainline/linux.git && cd linux
git checkout 4a5b97b821b8
git am /pfad/zu/kernel/patches/*.patch
make ARCH=arm64 redmi_a5_defconfig
make ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- -j$(nproc) Image
```

Wichtig: Der Kernel muss klein bleiben (Platzbedarf deutlich unter 48 103 424 Bytes),
sonst startet er nach echtem Ausschalten nicht. Siehe STATUS.md.

Hinweis: Der Basis-Branch ist ein WIP-Stand und kann upstream umgeschrieben werden - der Commit-Hash oben ist massgeblich.

Noch nicht dokumentiert: Bau des startfaehigen Images (vendor_boot mit Device-Tree, mkbootimg). Siehe werkzeuge/pack.sh und werkzeuge/deploy.sh.
