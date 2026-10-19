# NEUTRON

VST3-синтезатор в духе винтажных полифоников (Prophet-5 / Moog / Juno).
Сейчас готов **этап 1 из 4**, ядро: 2 осциллятора, ladder-фильтр, 2 огибающие и 8 голосов.

```
Osc A ─┐
       ├─ mix ─► 4-pole ZDF ladder LP ─► VCA ─► ×8 голосов ─► Master
Osc B ─┘              ▲                   ▲
                 Filter Env           Amp Env
```

## Что нужно для сборки

- Windows, **Build Tools for Visual Studio 2022** (MSVC, C++20)
- **CMake ≥ 3.25**: подойдёт CMake из Build Tools (`…\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin`)
- **git**: через него CMake скачивает JUCE
- VS Code с расширениями **C/C++** и **CMake Tools** (не обязательно)

JUCE **8.0.16-security** подтягивается автоматически через `FetchContent` (тег зафиксирован в `CMakeLists.txt`).

## Сборка

### VS Code + CMake Tools

1. Откройте папку проекта. CMake Tools сам подхватит `CMakePresets.json`.
2. Configure preset: **Visual Studio 2022 (MSVC x64)**. Первый configure скачает JUCE, это займёт пару минут.
3. Build preset: **Debug** или **Release**, затем **F7**.

### Командная строка

```powershell
cmake --preset vs2022
cmake --build --preset release     # или --preset debug
```

### Где лежит результат

| Что | Путь (от корня репозитория) |
|---|---|
| **VST3, Release** | `build/Neutron_artefacts/Release/VST3/NEUTRON.vst3` |
| **VST3, Debug** | `build/Neutron_artefacts/Debug/VST3/NEUTRON.vst3` |
| Standalone | `build/Neutron_artefacts/<Debug\|Release>/Standalone/NEUTRON.exe` |
| render_test | `build/<Debug\|Release>/render_test.exe` |

`COPY_PLUGIN_AFTER_BUILD` выключен: после сборки плагин никуда не копируется.

## Установка в FL Studio

`NEUTRON.vst3` — это папка-бандл, переносить её нужно целиком.

**Вариант А (для разработки, без копирования):**
1. FL Studio → **Options → Manage plugins**.
2. В **Plugin search paths** добавьте папку `…\NEUTRON\build\Neutron_artefacts\Release\VST3`
   (или `Debug\VST3` для отладки).
3. Нажмите **Find installed plugins**. NEUTRON появится в разделе **Generators**.

После каждой пересборки достаточно заново открыть плагин или проект: FL читает бандл из этой папки.
Перед сборкой закройте FL или удалите плагин из проекта, иначе Windows не даст перезаписать открытую DLL.

**Вариант Б:** скопируйте `NEUTRON.vst3` в `C:\Program Files\Common Files\VST3` (нужны права администратора).

## Отладка

В `.vscode/launch.json` три конфигурации (`cppvsdbg`):

- **Debug Standalone** запускает `NEUTRON.exe` из Debug-сборки. MIDI-вход выбирается в окне Options.
- **Debug in FL Studio** запускает `C:\Program Files\Image-Line\FL Studio 21\FL64.exe` под отладчиком.
  Точки останова срабатывают в плагине из `Debug\VST3`, если FL ищет плагины именно там (вариант А выше)
  и плагин не запущен в отдельном процессе (в настройках обёртки FL не включён режим bridged).
- **Run render_test** запускает тестовый рендер (Debug).

Сначала соберите проект (**F7**), потом нажмите **F5**.

## Тесты

### render_test: рендер в WAV без хоста

```powershell
.\build\Release\render_test.exe render_out
```

Используется тот же код из `Source/dsp`, что и в плагине, без JUCE. Файлы пишутся в 48 кГц, 32-bit float, моно.
Для каждого теста программа печатает время рендера.

| Файл | Что проверять |
|---|---|
| `01_saw_C2/C5/C7.wav` | Спектр. Между гармониками не должно быть заметных пиков (алиасинга) |
| `02_cutoff_sweep_res09.wav` | Свип среза 20 Гц → 20 кГц за 8 с при резонансе 0.9. Не должно быть щелчков и «взрывов» |
| `03_voice_steal.wav` | Аккорд из 8 нот, на 1.0 с приходит 9-я нота. На краже голоса не должно быть щелчка |
| `04_silence_after_release.wav` | Время рендера 5 с тишины должно быть ничтожным (проверка денормалов) |

FTZ/DAZ в render_test **не** включается, в отличие от плагина. Если DSP копит денормалы,
это сразу видно по времени теста 4.

Эталонные результаты (Release, эта машина):

- **C7:** самый сильный алиас ниже 10 кГц −54 дБ, ниже 20 кГц −42 дБ относительно основного тона.
  Для сравнения, у наивной пилы −26 дБ.
- **5 с тишины:** 2.8 мс (≈1700× быстрее реального времени), 0 активных голосов, выход ровно 0.
- **8 голосов:** около 24 мс на 1 с звука (≈2.4% одного ядра).

### pluginval

Скачайте [pluginval](https://github.com/Tracktion/pluginval/releases) и запустите:

```powershell
pluginval.exe --strictness-level 5 --validate-in-process --validate "build\Neutron_artefacts\Release\VST3\NEUTRON.vst3"
```

Это GUI-приложение, поэтому в PowerShell для просмотра вывода запускайте его через
`Start-Process -Wait -NoNewWindow -RedirectStandardOutput log.txt`. Код возврата 0 означает, что тесты пройдены.

## Параметры

ID стабильные: по ним хранятся состояние проекта и будущие пресеты. Менять их нельзя.

| ID | Диапазон | По умолчанию |
|---|---|---|
| `osc_a_wave`, `osc_b_wave` | Saw / Pulse / Triangle | Saw |
| `osc_a_octave`, `osc_b_octave` | −2…+2 | 0 |
| `osc_a_semi`, `osc_b_semi` | −12…+12 | 0 |
| `osc_a_fine`, `osc_b_fine` | ±50 центов | 0 / +7 |
| `osc_a_pw`, `osc_b_pw` | 5…95 % | 50 % |
| `osc_a_level`, `osc_b_level` | 0…1 | 1.0 / 0.5 |
| `filter_cutoff` | 20 Гц…20 кГц (лог.) | 3 кГц |
| `filter_resonance` | 0…1 | 0.15 |
| `filter_env_amount` | −1…+1 (±8 октав) | +0.35 |
| `filter_key_track` | 0…100 % (центр C4) | 50 % |
| `filter_vel_amount` | 0…1 (velocity → env amount) | 0.3 |
| `filter_env_attack` / `amp_env_attack` | 1 мс…10 с | 2 мс |
| `filter_env_decay` / `amp_env_decay` | 1 мс…20 с | 600 / 400 мс |
| `filter_env_sustain` / `amp_env_sustain` | 0…1 | 0.25 / 0.8 |
| `filter_env_release` / `amp_env_release` | 1 мс…20 с | 400 / 350 мс |
| `amp_vel_sens` | 0…1 (velocity → громкость) | 0.7 |
| `pitch_bend_range` | 0…12 полутонов | 2 |
| `master_volume` | −60…+6 дБ | −6 дБ |

MIDI: note on/off, pitch bend, CC1 (mod wheel: пока только запоминается),
CC120 (All Sound Off: быстрое затухание), CC123 (All Notes Off).

## Устройство кода

```
Source/
  PluginProcessor.*   APVTS, разбор MIDI, вызов VoiceManager, сохранение состояния
  PluginEditor.*      пока juce::GenericAudioProcessorEditor
  dsp/                чистый C++ без JUCE: тестируется отдельно (render_test)
    Oscillator.h      PolyBLEP saw/pulse, PolyBLAMP triangle, свободная фаза в double
    LadderFilter.h    4-полюсный ZDF/TPT lowpass (Zavalishin), место под tanh в контуре
    Envelope.h        ADSR с экспоненциальными сегментами (RC-кривые с overshoot)
    Smoother.h        линейное сглаживание параметров
    Voice.h           2 осц → микс → фильтр → VCA, затухание при краже
    VoiceManager.h    8 голосов, выбор/кража, сглаживание, pitch bend
tools/render_test.cpp
```

Как это работает:
- **Sample-accurate MIDI.** Блок хоста режется по позициям MIDI-событий: до события рендер, потом событие, потом рендер дальше.
- **Сглаживание.** Все непрерывные параметры проходят через рампу 20 мс. Срез сглаживается в октавах, а не в герцах.
  Октава, полутоны и форма волны переключаются мгновенно, как переключатели на железе.
- **Real-time.** В `processBlock` нет аллокаций, мьютексов, логов и исключений. Голоса лежат в фиксированном массиве,
  MIDI читается из сырых байтов, в начале стоит `juce::ScopedNoDenormals`.
- **Кража голоса.** Сначала крадётся отпущенный голос (в release), иначе самый старый. Он затухает за 4 мс,
  поэтому новая нота на украденном голосе стартует на 4 мс позже. Свободные голоса используются по кругу.

## Известные ограничения этапа 1

- **Фильтр линейный.** Обратная связь ограничена k = 3.96 (максимум при k = 4): у линейного фильтра на k = 4
  амплитуда резонанса ничем не ограничена. Поэтому на максимуме резонанс звенит сотни миллисекунд, но не
  самовозбуждается бесконечно. Полное самовозбуждение появится, когда в контур встанет `tanh`.
- Как у настоящего Moog, с ростом резонанса проседает бас (на максимуме до −14 дБ).
- Форма волны, переключённая во время ноты, меняется мгновенно и может дать щелчок.
- Sustain-педаль (CC64) пока не поддерживается.
