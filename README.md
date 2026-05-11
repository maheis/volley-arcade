# Volley-Arcade

Kleines Volleyball-Arcade-Spiel, das als Geburtstagsgeschenk mit Hilfe von Copilot erstellt wurde.s

## Voraussetzungen

- Linux (32-bit oder 64-bit)
- GCC
- SDL2-Entwicklerpaket

SDL2 unter Debian/Ubuntu installieren:

```bash
sudo apt-get update
sudo apt-get install libsdl2-dev build-essential
```

Debian 11 (32-bit / i386):

```bash
sudo dpkg --print-architecture
# sollte ausgeben: i386

sudo apt-get update
sudo apt-get install build-essential libsdl2-dev pkg-config
make
```

Optionaler Cross-Build auf 64-bit Debian:

```bash
sudo dpkg --add-architecture i386
sudo apt-get update
sudo apt-get install gcc-multilib libc6-dev-i386 libsdl2-dev:i386 pkg-config
make build32
```

## Build

```bash
make
```

## Starten

```bash
make run
```

## Steuerung

- Startmenue: `UP` 1 Spieler, `DOWN` 2 Spieler, `LEFT/RIGHT` Schwierigkeit, `SPACE` Sound an/aus, `ENTER` starten
- Spieler 1 (Singleplayer): `A/D` oder `LEFT/RIGHT` laufen, `W` oder `UP` oder `SPACE` springen/blocken, `LEFT CTRL` (oder `RIGHT CTRL`) Aufschlag halten/loslassen
- Spieler 2 (2-Spieler-Modus): `LEFT` links, `UP` springen/blocken, `RIGHT` rechts, `ALT GR` Aufschlag halten/loslassen
- nur Singleplayer: Top-7-Highscores werden im Startmenue angezeigt
- nur Singleplayer: bei Game Over oder Verlassen im Match wird ein Name fuer den Eintrag abgefragt
- `ENTER`: Spiel starten / aus Pause fortsetzen
- `P`: Pause an/aus
- `R`: Spiel zuruecksetzen
- `M`: Sound stumm/an
- `ESC`: zurueck ins Menue bzw. beenden

## Highscore-Speicherort

Highscores werden lokal gespeichert unter:

- `$XDG_CONFIG_HOME/volley-arcade/highscores.dat`
- Fallback: `~/.config/volley-arcade/highscores.dat`

Der Ordner wird bei Bedarf automatisch erstellt.

## Dateien

- `src/main.c`: kompletter Prototyp
- `Makefile`: Build- und Run-Targets
- `LICENSE`: MIT-Lizenz

## Naechste Schritte

- Publikum zeichnen
- Schiri zeichnen
- In/Out-Anzeige durch Schiri
