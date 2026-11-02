# Balpassage meten met twee ESP32-C3 SuperMini's

De eerste SuperMini meet een IR-lichtstraal. Als een bal de straal onderbreekt, telt hij één passage en verstuurt hij het nummer via ESP-NOW. De tweede SuperMini verhoogt bij elk nieuw bericht de score op een viercijferig 5461BS-1-display. Beide gebruiken Wi-Fi-kanaal 6. Het MAC-adres van de ontvanger staat in `src/main.cpp`; bij vervanging van dat bord moet het adres daar worden aangepast.

## Display op de ontvanger

De ontvanger gebruikt de aansluitingen van je bestaande displaycode: segmenten a–g en dp op GPIO0–GPIO7, elk via een eigen **330 Ω** weerstand. De vier PNP-digitdrivers zitten op GPIO9, GPIO10, GPIO20 en GPIO21 en zijn actief LOW. Het display begint op **0000**, telt per bal één op en gaat na 9999 terug naar 0000. Na een scoreverhoging worden nieuwe ballen gedurende **5 seconden** genegeerd. De score begint na opnieuw opstarten weer bij 0000.

## IR-aansluiting op de zender

- IR-led: GPIO3 → **220 Ω** → anode (+); kathode (−) → GND.
- IR-fototransistor: collector → GPIO4, emitter → GND.
- **10 kΩ** tussen GPIO4 en 3V3. De collector en GPIO4 zitten op hetzelfde aansluitpunt.

Plaats de IR-led en fototransistor recht tegenover elkaar, met de bal ertussen als die passeert. Dit schema geldt voor een losse fototransistor; een fotodiode of ontvangerprintje kan een andere schakeling nodig hebben.

## Uploaden

Upload de juiste omgeving naar elk bord:

```powershell
pio run -e ir_zender -t upload
pio run -e bal_ontvanger -t upload
```

Als beide borden tegelijk op USB aangesloten zijn, geef dan per opdracht de juiste poort op met `--upload-port COMx`. Open de seriële monitor op **115200 baud**.

Richt de IR-led en ontvanger op elkaar met een **vrije** lichtstraal. De zender kalibreert bij het opstarten en probeert daarna opnieuw zodra hij een voldoende sterk signaal ziet. Bij `WACHT OP LICHTSTRAAL` en `grens=0` is de straal nog niet gekalibreerd; controleer dan de bedrading en uitlijning. Zolang het signaal te zwak is, telt hij geen passages. Tijdens gebruik toont hij `IR`, `basis`, `grens` en `VRIJ` of `ONDERBROKEN`. De tweede ESP toont de score op het display en in de seriële monitor.

Bij een onderbreking verstuurt de zender hetzelfde bericht drie keer. De ontvanger telt die herhalingen maar één keer. De grens voor een onderbreking is de helft van het vrije IR-signaal; na herstel boven 70% van dat signaal kan een volgende bal worden geteld.

## Als de score niet verandert

1. Upload beide omgevingen opnieuw. Open de seriële monitor van de **ontvanger** op 115200 baud en typ `+`. De score moet één omhoog gaan. Dit test het display zonder radio of IR.
2. Laat beide borden aan staan. Open de seriële monitor van de **zender** en typ `t`. De zender meldt `Bal ... gedetecteerd`; de score op de ontvanger moet één omhoog gaan. Dit test ESP-NOW zonder IR.
3. Kijk op de zender naar `IR`, `basis` en `grens`. Met een vrije lichtstraal moet `basis` minstens 60 zijn. Als er `WACHT OP LICHTSTRAAL` staat, telt de zender geen echte balpassages. Controleer dan eerst de IR-led, fototransistor, weerstanden en uitlijning.

In de seriële monitor moet de poort van het juiste bord gekozen zijn. Voor `+` en `t` maakt de ingestelde regeleindoptie niet uit.

De ontvanger meldt nu elke twee seconden hoeveel radiopakketten en geldige balberichten hij heeft gezien. De zender meldt per verzendpoging of ESP-NOW de opdracht accepteerde. Dit helpt bepalen of de fout bij de radio of bij het display zit.
