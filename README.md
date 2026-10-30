# ESP32-C3 controller en ESP32-S3 Nano Leaphy-ontvanger

Dit PlatformIO-project gebruikt ESP-NOW. De **controller** stuurt elke 50 ms de twee joystickassen en de twee knopstanden. De **receiver** laat de waarden op de seriële monitor zien en stuurt een ontvangstbevestiging terug. Beide ESP32's gebruiken Wi-Fi-kanaal 1; een router of bekend MAC-adres is niet nodig.

## Aansluiten op de controller

| Onderdeel | ESP32-C3 SuperMini |
| --- | --- |
| Joystick VCC | 3V3 |
| Joystick GND | GND |
| Joystick VRx | GPIO0 |
| Joystick VRy | GPIO1 |
| Joystick SW | GPIO5 |
| Extra knop, ene aansluiting | GPIO3 |
| Extra knop, andere aansluiting | GND |

De joystickknop maakt via de joystickmodule zelf verbinding met GND. Voor beide knoppen gebruikt de code interne pull-upweerstanden. In het verzonden bericht is **bit 0 (`buttons & 1`) de puls van de extra knop** en **bit 1 (`buttons & 2`) de joystick-stopknop**. Sluit de joystick op **3,3 V** aan zodat de analoge uitgangen niet hoger worden dan de ESP32-ingangen toelaten.

Voor een eerste test van de draadloze verbinding heeft de receiver alleen voeding nodig. Verbind de ESP32's **niet** met elkaar via GPIO-draden: ESP-NOW is draadloos.

## Uploaden en testen

### Automatisch voor beide boards

Sluit de ESP32-C3 SuperMini en/of de ESP32-S3 Nano via USB aan en druk op de gewone PlatformIO-knop **Upload**. De standaardomgeving `auto_upload` leest het chiptype, uploadt `controller` naar de C3 en `receiver_s3_nano` naar de S3. Je hoeft geen COM-poort of PlatformIO-omgeving te kiezen. Dit werkt ook wanneer maar één van de twee boards aangesloten is. In VS Code kan het ook via **Terminal → Run Task → Upload automatisch (C3 controller / S3 receiver)**.

Via PowerShell kan dezelfde taak zo worden gestart:

```powershell
& "$env:USERPROFILE\.platformio\penv\Scripts\python.exe" scripts\upload_auto.py
```

Sluit een open seriële monitor voordat je uploadt, zodat de uploadtaak de poort kan gebruiken. Na de upload kan de S3 een ander COM-nummer krijgen; controleer dat voor je de monitor opent.

### Handmatig uploaden

Sluit de C3 SuperMini aan en upload `controller`. Sluit daarna de S3 Nano aan en upload `receiver_s3_nano`:

```powershell
pio run -e controller -t upload
pio run -e receiver_s3_nano -t upload
```

Als PlatformIO de juiste poort niet zelf vindt, bekijk de poorten en geef de bootloaderpoort op:

```powershell
pio device list
pio run -e receiver_s3_nano -t upload --upload-port COMx
```

Vervang `COMx` door de **bootloaderpoort** van de S3 Nano, niet automatisch door de poort van het draaiende programma. Dit bord kan tussen twee poorten wisselen: in deze sessie was de bootloader COM7 en de draaiende ontvanger COM8. `COM7` verdwijnt dus zodra de ontvanger draait. Als de ontvanger al draait, hoef je niet opnieuw te uploaden; open dan de seriële monitor op zijn huidige poort.

De omgeving `auto_upload` is de standaard voor de gewone PlatformIO-knop **Upload**. Handmatige `-e controller` en `-e receiver_s3_nano` blijven beschikbaar wanneer je bewust maar één firmwarevariant wilt bouwen of uploaden.

`receiver` is uitsluitend voor een tweede ESP32-C3 SuperMini. De S3 Nano heeft geen joystickaansluitingen nodig als hij de ontvanger is. Een andere S3 Nano-variant kan een andere PlatformIO-boardinstelling nodig hebben.

Als beide ESP32's tegelijk aangesloten zijn, geef dan per opdracht de juiste seriële poort op met `--upload-port COMx`.

Open de seriële monitor op **115200 baud**. De controller toont elke 200 ms gekalibreerde waarden van **-255 tot 255**, bijvoorbeeld `X=0 Y=-180 stop=0 extra_knop=0 extra_puls=0 verbinding=VERBONDEN`. Zolang de receiver niet antwoordt, staat er `verbinding=NIET VERBONDEN`. De joystickklik blijft een stopknop. De **extra knop geeft één puls per klik**, zodra je hem indrukt (na 30 ms ontdendering). De controller meldt `Extra knop: puls verzonden` en de ontvanger meldt `Extra knop: puls ontvangen #...`. De puls duurt één verzonden bericht van 50 ms en verandert de motorsnelheid niet. Laat de joystick los tijdens het opstarten. Houd voor herkalibratie **beide knoppen 2 seconden** ingedrukt en laat daarna beide knoppen en de joystick los; deze combinatie geeft geen extra puls als beide knoppen tegelijk worden ingedrukt. De motoren blijven tijdens het wachten op het loslaten gestopt.

Upload na een protocolwijziging **beide ESP32's**. De ontvanger negeert berichten met een andere protocolversie, zodat een oude bitindeling niet als een andere knop wordt gelezen.

De receiver toont elke 200 ms de gecodeerde aswaarden (midden **2048**), knopstanden en motordoelen. Als er **200 ms** geen nieuw bericht binnenkomt, meldt de code `Verbinding verloren` en stopt de S3 de motoren direct.

## Leaphy-motoren op de S3 Nano

De omgeving `receiver_s3_nano` gebruikt de huidige vier Nano-aansluitingen op de **motor-driver**. Houd de PWM- en richtingspin van iedere fysieke motor als paar aangesloten:

| S3 Nano | Functie |
| --- | --- |
| D3 | PWM linker motor |
| D11 | PWM rechter motor |
| D4 | Richting linker motor |
| D2 | Richting rechter motor |

De joystick bestuurt de twee wielen. De middenstand wordt bij het opstarten gemeten; de dode zone is 180 ruwe ADC-stappen. De snelheid loopt op tot de maximale PWM-waarde **255**. In de voorste joystickhoeken stopt het binnenste wiel bij een volledige diagonale beweging, terwijl het buitenste wiel op volle snelheid draait. Met X opzij en Y in het midden draait de Leaphy ter plekke. De motorsnelheid verandert geleidelijk met maximaal 10 PWM-stappen per 10 ms; bij omkeren gaan de motoren eerst door nul. De joystickknop stopt de motoren meteen zolang je hem indrukt. Als de controller langer dan **200 ms** geen nieuwe waarden stuurt, stoppen de motoren ook meteen. De S3-monitor toont `doel_links` en `doel_rechts`, zodat je kunt zien welke motorsnelheid wordt aangevraagd. De C3-omgeving `receiver` toont alleen de waarden en bestuurt geen motoren.

De besturing gebruikt jouw joystickrichtingen: **Y=0 is vooruit**, **Y=4095 is achteruit**, **X=0 is links** en **X=4095 is rechts**. Verwachte doelen bij volledig uitslaan: boven `links=255 rechts=255`, linksboven `links=0 rechts=255`, rechtsboven `links=255 rechts=0`, beneden `links=-255 rechts=-255`. Als de fysieke motoren anders reageren dan `doel_links` en `doel_rechts` op de seriële monitor aangeven, controleer dan de paren **D3/D4 links** en **D11/D2 rechts**. Test met de wielen vrij van de grond. Sluit de motoren op de motor-driver aan, niet rechtstreeks op de S3-pinnen.
