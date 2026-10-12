# modem-sipc: SIPC-Kern für Mainline 7.1 (UMS9230, Redmi A5)

Status: **Vorbereitung, noch kein Code übernommen.** Stand 11.10.2026.

## Ziel
Die kernelseitige Kommunikationsschicht (sipc, smem, smsg, sbuf) auf Mainline 7.1
bringen, so dass die Gerätekanäle später darauf aufsetzen können. Diese Schicht
ist unabhängig von trusty und wird ohne trusty-Anteile gebaut.

## Quelle und Lizenz
- Vorlage: mu300-linux (dikeckaan), Verzeichnis `upstream/modules/sprd_modem/sipc/`.
- Die übernommenen Dateien bleiben **GPL-2.0-only** mit dem Copyright-Vermerk von
  Spreadtrum/UNISOC. Die MIT-Lizenz des mu300-Projekts gilt nur für dessen eigene Teile.
- Jede übernommene Datei behält ihren SPDX-Kopf und den Vermerk, wir ergänzen nur Anpassungen.

## Abhängigkeiten, die in Mainline fehlen
| Vendor-Teil | Verwendet in | Ersatz (Plan) |
|---|---|---|
| `sprd_pms` (Ressourcen-Anforderung) | sipc_priv.h, smem | nur für nicht-lokalen Speicher nötig; für UMS9230 zunächst weglassen (SMEM_LOCAL) |
| `sprd_mpm` (Energie) | sipc_priv.h | entfällt in Schritt 1 |
| `linux/sipc.h` | sipc.c | eigener Header im Verzeichnis, nur die nötigen Konstanten |
| Mailbox mit Core-ID | sipc.c | Mainline-`sprd-mailbox` (ums9230-mailbox, bei uns gebunden als 641c0000) über `mbox_request_channel` |
| debugfs-Helfer | smem, smsg, sbuf | in Schritt 1 deaktiviert, danach nachrüsten |

## Vorgesehene Reihenfolge
1. **smem**: gemeinsamer Speicherbereich, nur lokaler Modus.
2. **sipc**: Grundstruktur, Mailbox-Anbindung über die Mainline-API.
3. **smsg**: Nachrichten-Ringpuffer.
4. **sbuf**: Pufferkanäle.
5. Erst danach **sblock** und **spipe** (Gerätedateien `spipe_*`).

Jeder Schritt wird einzeln gebaut, auf dem Handy geprüft und mit eigener Änderung dokumentiert.

## Testboden ohne Modem
Der WCN-Code in `kernel/wcn-test/` spricht bereits SIPC-Kanäle (u. a. Kanal 5 Log).
Die Portierung wird dort gegengeprüft, bevor wir sie auf das Modem beziehen.

## Erfolgskriterien für Schritt 1
- Modul baut gegen den Mainline-7.1-Baum ohne Warnungen aus unseren Dateien.
- Auf dem Handy: Kanal 5 liefert Daten, und die WCN-Testmodule laufen wie bisher.

## Ausgeschlossen
- Der trusty-Teil (kernelbootcp, trusty-IPC und alles, was die Firmware-Freigabe betrifft).
- Firmware-Dateien und NV-Daten (bleiben aus dem Repo).
- Änderungen, die Schutzmechanismen umgehen.

## Offen
- Genaue Register-Adressen und Mailbox-IDs unseres SoC gegen die Gerätebeschreibung prüfen.
- Ob der WCN-Pfad die smsg-Schicht bereits benutzt oder einen eigenen Weg hat.
