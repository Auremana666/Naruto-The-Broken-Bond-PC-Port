#pragma once

namespace localization_strings {

inline constexpr const char* kDe[] = {
    "Naruto: The Broken Bond",  // WindowTitle
    "SPIELEN",  // TabPlay
    "OPTIONEN",  // TabSettings
    "SPIELDATEIEN",  // TabGameFiles
    "Qualität",  // GroupQuality
    "Steuerung",  // GroupControls
    "Seitenverhältnis",  // AspectLabel
    "16:9 - Standard (Original)",  // Aspect16x9
    "21:9 - UltraWide",  // Aspect21x9
    "32:9 - Super UltraWide",  // Aspect32x9
    "Passt das horizontale Sichtfeld der 3D-Kamera an, ohne die Figuren zu strecken oder zu verzerren.",  // AspectDescription
    "Auflösung",  // ResolutionLabel
    "Renderer",  // RendererLabel
    "Texturfilter",  // TextureFilteringLabel
    "Standard",  // AnisoDefault
    "FXAA",  // FxaaLabel
    "Aus",  // FxaaOff
    "Normal",  // FxaaNormal
    "Extrem",  // FxaaExtreme
    "Nachbearbeitung",  // PostProcessingLabel
    "Bilinear (Standard)",  // PostBilinear
    "CAS (Schärfen)",  // PostCas
    "FSR EASU (Skalierung)",  // PostFsr
    "Dithering",  // DitherLabel
    "FPS-Anzeige",  // FpsOverlayLabel
    "Vollbild",  // FullscreenLabel
    "VSync",  // VsyncLabel
    "Aktiviert lassen, damit Animationen und Physik mit der richtigen Geschwindigkeit laufen.",  // VsyncDescription
    "SDL für Controller verwenden (Xbox / PlayStation)",  // SdlInputLabel
    "Controller",  // ControllerLabel
    "Kein Controller erkannt (Tastatur und Maus)",  // NoControllerDetected
    "Intro-Videos überspringen (Ubisoft-Logos)",  // SkipIntroLabel
    "SPIEL STARTEN",  // BtnPlay
    "Empfohlene Optionen",  // BtnResetRecommended
    "Spielordner öffnen",  // BtnOpenFolder
    "Die Optionen werden beim Spielstart angewendet.",  // SettingsAppliedNotice
    "Spiel bereit",  // StatusReady
    "Spieldateien nicht gefunden",  // StatusNotFound
    "Wähle den entpackten Broken-Bond-Ordner unter SPIELDATEIEN.",  // FilesMissingDesc
    "(Kein Ordner gefunden)",  // NoneDetected
    "Ordner mit extrahierten Spieldateien wählen...",  // BtnSelectFolder
    "Standardpfad verwenden",  // BtnResetDefault
    "Standardpfad wiederhergestellt.",  // PathResetMsg
    "Optionen werden beim Spielstart oder beim Schließen des Launchers gespeichert.",  // FooterNotice
    "Spiel gestartet!",  // LaunchSuccessMsg
    "Prozess konnte nicht gestartet werden: ",  // LaunchFailedMsg
    "Spielprogramm nicht gefunden unter: ",  // ExeNotFoundMsg
    "Pfad aktualisiert: ",  // PathUpdatedMsg
    "Deine Reise beginnt hier.",  // PlayHeading
    "Kehre nach Konoha zurück.",  // PlayIntro
    "Spieldateien erkannt. Du kannst loslegen.",  // ReadyDetail
    "Spieldateien verwalten",  // ManageFiles
    "Verbinde deine Originaldateien mit dem PC-Port.",  // FilesIntro
    "Spielordner",  // FolderHeading
    "Wähle den Ordner mit default.xex. Deine Originaldateien werden weder verschoben noch gelöscht.",  // FolderHint
    "Musst du dein Spiel noch entpacken?",  // SetupHeading
    "Entpacke dein originales Xbox-360-Abbild und wähle den Ordner mit default.xex.",  // SetupHint
    "Empfohlene Optionen angewendet.",  // RecommendedApplied
    "Lege broken_bond.exe neben diesen Launcher.",  // ExeMissing
    "PC-Port nicht gefunden",  // ExeMissingStatus
    "Spieldateien erkannt",  // FilesDetected
    "Spiel",  // GroupGame
    "Spielsprache",  // GameLanguageLabel
    "Englisch",  // GameLanguageEnglish
    "Französisch",  // GameLanguageFrench
    "Deutsch",  // GameLanguageGerman
    "Spanisch",  // GameLanguageSpanish
    "Italienisch",  // GameLanguageItalian
    "Benutzerdefiniert (%u)",  // GameLanguageCustom
    "Wird beim nächsten Spielstart angewendet. Verfügbare Texte und Stimmen hängen von deinen Spieldateien ab.",  // GameLanguageDescription
    "Sprache der Launcher-Oberfläche",  // LauncherLanguageTooltip
    "Xenos (emulierte GPU)",  // RendererXenos
    "Nativ (Direct3D 12)",  // RendererNative
    "Xenos emuliert die Xbox-360-GPU und ist der Standard. Natives Direct3D 12 ist in entsprechenden Builds verfügbar; die Optionen gelten nur bei Auswahl von Nativ.",  // RendererDescription
    "Mit dem nativen Renderer nicht verfügbar. Unter Renderer Xenos wählen, um es zu nutzen.",  // XenosOnlyReason
    "MSAA",  // MsaaLabel
    "4x (Original)",  // MsaaOriginal
    "Multisampling der 3D-Szenen des Spiels. 8x braucht eine stärkere GPU.",  // MsaaDescription
    "Transparenz-AA",  // TransparencyAaLabel
    "Glättet die Kanten von Laub, Zäunen und anderen ausgeschnittenen Texturen (Alpha to Coverage).",  // TransparencyAaDescription
    "Bildrate",  // FrameRateLabel
    "30 ist die Originalbildrate. Darüber behält die Spiellogik ihr Tempo, und die Zwischenbilder werden interpoliert. Mit VSync endet sie bei der Rate des Monitors; hohe Bildraten brauchen eine schnellere CPU.",  // FrameRateDescription
    "Helligkeit",  // BrightnessLabel
    "50 ist die Originalhelligkeit des Spiels.",  // BrightnessDescription
    "Bildlatenz",  // FrameLatencyLabel
    "1 (minimal)",  // FrameLatencyLowest
    "Bilder in der Warteschlange vor der Anzeige. 1 hat die geringste Eingabeverzögerung; 2 oder 3 können ungleichmäßige Bildzeiten glätten.",  // FrameLatencyDescription
    "Dreifachpufferung",  // TripleBufferingLabel
    "Drei Anzeigepuffer statt zwei.",  // TripleBufferingDescription
    "Synchronisiert die Ausgabe mit dem Bildschirm. Mit dem nativen Renderer hängt die Spielgeschwindigkeit nicht von VSync ab.",  // VsyncNativeDescription
    "Grafikkarte",  // GraphicsDeviceLabel
    "Automatisch",  // GraphicsDeviceAuto
    "Die Grafikkarte, auf der das Spiel läuft. Automatisch überlässt die Wahl dem Renderer. Auf Laptops mit zwei Grafikkarten die dedizierte wählen (NVIDIA oder AMD).",  // GraphicsDeviceDescription
    "Anzeige",  // GroupDisplay
    "Erweitert",  // GroupAdvanced
    "nur Xenos",  // XenosOnlyTag
    "nur nativ",  // NativeOnlyTag
    "Nur mit dem nativen Renderer. Unter Renderer Nativ wählen, um es zu nutzen.",  // NativeOnlyReason
    "Die Auflösung, in der das Spiel gezeichnet wird, als Vielfaches der originalen 1280×720. Höhere Werte sehen schärfer aus und brauchen eine stärkere Grafikkarte.",  // ResolutionDescription
    "Anisotrope Filterung hält Texturen auf schräg gesehenen Flächen wie Böden und Straßen scharf.",  // TextureFilteringDescription
    "Glättet gezackte Kanten, indem das fertige Bild gefiltert wird. Extrem glättet stärker und kann feine Details verwischen.",  // FxaaDescription
    "Wie das Bild an den Bildschirm angepasst wird. CAS schärft es, FSR skaliert es mit Schärfung hoch, und Bilinear lässt es weich.",  // PostProcessingDescription
    "Fügt feines Rauschen hinzu, das Farbstufen in Verläufen verbirgt.",  // DitherDescription
    "Zeigt Bildrate und Bildzeit in einer Bildschirmecke. F1 schaltet die Anzeige im Spiel um.",  // FpsOverlayDescription
    "Startet das Spiel im Vollbild. Ausgeschaltet läuft es in einem Fenster.",  // FullscreenDescription
    "Überspringt die Logos beim Spielstart. Das Eröffnungsvideo bleibt erhalten.",  // SkipIntroDescription
    "Liest Controller über SDL, das Xbox-, PlayStation- und andere Controller erkennt. Ausgeschaltet nutzt das Spiel XInput, das nur Xbox-kompatible Controller erkennt.",  // SdlInputDescription
    "Der gerade verbundene Controller, wie SDL ihn nennt.",  // ControllerDescription
    "Tastatur und Maus verwenden",  // KeyboardMouseLabel
    "Tastatur und Maus ersetzen einen Xbox-Controller und funktionieren zusammen mit einem angeschlossenen Controller. Steuert die Maus die Kamera, bleibt der Mauszeiger im Spielfenster, solange es aktiv ist; Alt+Tab gibt ihn frei. Das Spiel zeigt weiterhin die Controller-Tasten an.",  // KeyboardMouseDescription
    "Maus steuert die Kamera",  // MouseCameraLabel
    "Bewegt die Kamera (den rechten Stick) mit der Maus. Aus: Die Kamera bewegt sich nur mit den Tasten des rechten Sticks.",  // MouseCameraDescription
    "Empfindlichkeit",  // MouseSensitivityLabel
    "Wie schnell die Kamera der Maus folgt. 100 % ist der Standard.",  // MouseSensitivityDescription
    "Tasten",  // KeysLabel
    "Die Tasten und Maustasten für jede Controller-Eingabe, zwei pro Eingabe.",  // KeysDescription
    "Tasten belegen…",  // MapKeysButton
    "Tasten belegen",  // MapKeysTitle
    "Steuerung › Tastatur und Maus",  // MapKeysPath
    "Knöpfe",  // KeysButtonsHeading
    "Linker Stick",  // LeftStickName
    "Rechter Stick",  // RightStickName
    "Steuerkreuz",  // DpadName
    "+ Maus",  // KeysPlusMouse
    "Oben",  // DirectionUp
    "Unten",  // DirectionDown
    "Links",  // DirectionLeft
    "Rechts",  // DirectionRight
    "Klicken und die neue Taste drücken (5 s). Rechtsklick löscht. Eine bereits belegte Taste verlässt ihre bisherige Eingabe.",  // KeysHint
    "Taste… %d",  // KeyCapturePrompt
    "Standardtasten wiederherstellen",  // KeysRestoreDefaults
    "Fertig",  // KeysDone
    "%s wurde von %s entfernt.",  // KeyMovedStatus
    "%s wird vom Spiel verwendet und kann nicht belegt werden.",  // KeyReservedStatus
    "Standardtasten wiederhergestellt.",  // KeysRestoredStatus
    "Leertaste",  // KeySpaceName
    "Linke Maus",  // MouseLeftName
    "Rechte Maus",  // MouseRightName
    "Mittlere Maus",  // MouseMiddleName
    "Speichern fehlgeschlagen. Prüfe die Konfigurationsdatei und die Schreibrechte des Ordners.",  // SaveFailed
    "Nicht verfügbar",  // UnavailableTag
    "Ultrawide erfordert noch die Validierung der Kameraprojektion von Broken Bond und der Xenos-Ausgabe. Das Spiel verwendet 16:9.",  // AspectUnavailableReason
};

}  // namespace localization_strings
