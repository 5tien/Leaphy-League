# Twee goals meten met drie ESP32-C3 SuperMini's

Bij elk goal staat een SuperMini met een IR-lichtstraal. Als een bal de straal onderbreekt, verstuurt die zender het doelnummer via ESP-NOW. Een derde SuperMini toont beide scores op een viercijferig 5461BS-1-display. Alle drie gebruiken Wi-Fi-kanaal 6. Het MAC-adres van de ontvanger staat in `src/main.cpp`; bij vervanging van dat bord moet het adres daar worden aangepast.

## Display op de ontvanger

De ontvanger gebruikt de aansluitingen van je bestaande displaycode: segmenten a–g en dp op GPIO0–GPIO7, elk via een eigen **330 Ω** weerstand. De vier PNP-digitdrivers zitten op GPIO9, GPIO10, GPIO20 en GPIO21 en zijn actief LOW. De **linker twee cijfers** tonen goal 1; de **rechter twee cijfers** tonen goal 2. Het display begint op **0000**. Elke goal telt afzonderlijk van 00 tot 99 en begint daarna weer bij 00. Na een scoreverhoging worden nieuwe ballen voor **diezelfde goal** gedurende **5 seconden** genegeerd. Na opnieuw opstarten beginnen beide scores weer op 00.

## IR-aansluiting op de zender

- IR-led: GPIO3 → **220 Ω** → anode (+); kathode (−) → GND.
- IR-fototransistor: collector → GPIO4, emitter → GND.
- **10 kΩ** tussen GPIO4 en 3V3. De collector en GPIO4 zitten op hetzelfde aansluitpunt.

Plaats de IR-led en fototransistor recht tegenover elkaar, met de bal ertussen als die passeert. Dit schema geldt voor een losse fototransistor; een fotodiode of ontvangerprintje kan een andere schakeling nodig hebben.

## Uploaden

Upload de juiste omgeving naar elk van de drie borden. Beide zenders gebruiken dezelfde bedrading, maar krijgen door de gekozen omgeving een ander doelnummer:

```powershell
pio run -e goal_1_zender -t upload
pio run -e goal_2_zender -t upload
pio run -e bal_ontvanger -t upload
```

Als beide borden tegelijk op USB aangesloten zijn, geef dan per opdracht de juiste poort op met `--upload-port COMx`. Open de seriële monitor op **115200 baud**.

Richt de IR-led en ontvanger op elkaar met een **vrije** lichtstraal. De zender kalibreert bij het opstarten en probeert daarna opnieuw zodra hij een voldoende sterk signaal ziet. Bij `WACHT OP LICHTSTRAAL` en `grens=0` is de straal nog niet gekalibreerd; controleer dan de bedrading en uitlijning. Zolang het signaal te zwak is, telt hij geen passages. Tijdens gebruik toont hij `IR`, `basis`, `grens` en `VRIJ` of `ONDERBROKEN`. De tweede ESP toont de score op het display en in de seriële monitor.

Bij een onderbreking verstuurt de zender hetzelfde bericht drie keer. De ontvanger houdt de berichtnummers per goal bij en telt die herhalingen maar één keer. De grens voor een onderbreking is de helft van het vrije IR-signaal; na herstel boven 70% van dat signaal kan een volgende bal worden geteld.

## Als de score niet verandert

1. Upload alle drie de omgevingen opnieuw. Open de seriële monitor van de **ontvanger** op 115200 baud en typ `1` voor de linker score of `2` voor de rechter score. Dit test het display zonder radio of IR. Wacht vijf seconden voordat je dezelfde score opnieuw test.
2. Laat alle drie de borden aan staan. Open de seriële monitor van elke **zender** en typ `t`. De zender meldt `Doel ... bal ... gedetecteerd`; de bijbehorende score op de ontvanger moet één omhoog gaan. Dit test ESP-NOW zonder IR.
3. Kijk op de zender naar `IR`, `basis` en `grens`. Met een vrije lichtstraal moet `basis` minstens 60 zijn. Als er `WACHT OP LICHTSTRAAL` staat, telt de zender geen echte balpassages. Controleer dan eerst de IR-led, fototransistor, weerstanden en uitlijning.

In de seriële monitor moet de poort van het juiste bord gekozen zijn. Voor `+` en `t` maakt de ingestelde regeleindoptie niet uit.

De ontvanger meldt nu elke twee seconden hoeveel radiopakketten en geldige balberichten hij heeft gezien. De zender meldt per verzendpoging of ESP-NOW de opdracht accepteerde. Dit helpt bepalen of de fout bij de radio of bij het display zit.
