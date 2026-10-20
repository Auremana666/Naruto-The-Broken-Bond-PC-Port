#pragma once

namespace localization_strings {

inline constexpr const char* kIt[] = {
    "Naruto: The Broken Bond",  // WindowTitle
    "GIOCA",  // TabPlay
    "OPZIONI",  // TabSettings
    "FILE DI GIOCO",  // TabGameFiles
    "Qualità",  // GroupQuality
    "Controlli",  // GroupControls
    "Formato schermo",  // AspectLabel
    "16:9 - Standard (originale)",  // Aspect16x9
    "21:9 - UltraWide",  // Aspect21x9
    "32:9 - Super UltraWide",  // Aspect32x9
    "Regola il campo visivo orizzontale della telecamera 3D senza allungare o deformare i personaggi.",  // AspectDescription
    "Risoluzione",  // ResolutionLabel
    "Renderer",  // RendererLabel
    "Filtro texture",  // TextureFilteringLabel
    "Predefinito",  // AnisoDefault
    "FXAA",  // FxaaLabel
    "Disattivato",  // FxaaOff
    "Normale",  // FxaaNormal
    "Estremo",  // FxaaExtreme
    "Post-processing",  // PostProcessingLabel
    "Bilineare (predefinito)",  // PostBilinear
    "CAS (nitidezza)",  // PostCas
    "FSR EASU (ridimensionamento)",  // PostFsr
    "Dithering",  // DitherLabel
    "Indicatore FPS",  // FpsOverlayLabel
    "Schermo intero",  // FullscreenLabel
    "VSync",  // VsyncLabel
    "Lascia attiva questa opzione per mantenere la velocità corretta delle animazioni e della fisica.",  // VsyncDescription
    "Usa SDL per i controller (Xbox / PlayStation)",  // SdlInputLabel
    "Controller",  // ControllerLabel
    "Nessun controller rilevato (tastiera e mouse)",  // NoControllerDetected
    "Salta i video introduttivi (loghi Ubisoft)",  // SkipIntroLabel
    "GIOCA",  // BtnPlay
    "Opzioni consigliate",  // BtnResetRecommended
    "Apri cartella del gioco",  // BtnOpenFolder
    "Le opzioni vengono applicate all'avvio del gioco.",  // SettingsAppliedNotice
    "Gioco pronto",  // StatusReady
    "File di gioco non trovati",  // StatusNotFound
    "Seleziona la cartella estratta di Broken Bond in FILE DI GIOCO.",  // FilesMissingDesc
    "(Nessuna cartella trovata)",  // NoneDetected
    "Seleziona cartella del gioco estratto...",  // BtnSelectFolder
    "Ripristina percorso predefinito",  // BtnResetDefault
    "Percorso predefinito ripristinato.",  // PathResetMsg
    "Le opzioni vengono salvate all'avvio del gioco o alla chiusura del launcher.",  // FooterNotice
    "Gioco avviato!",  // LaunchSuccessMsg
    "Impossibile avviare il processo: ",  // LaunchFailedMsg
    "Eseguibile del gioco non trovato in: ",  // ExeNotFoundMsg
    "Percorso aggiornato: ",  // PathUpdatedMsg
    "Il tuo viaggio inizia qui.",  // PlayHeading
    "Torna al Villaggio della Foglia.",  // PlayIntro
    "File di gioco rilevati. Puoi giocare.",  // ReadyDetail
    "Gestisci i file di gioco",  // ManageFiles
    "Collega i tuoi file originali alla versione per PC.",  // FilesIntro
    "Cartella del gioco",  // FolderHeading
    "Seleziona la cartella contenente default.xex. I file originali non vengono spostati né eliminati.",  // FolderHint
    "Devi estrarre il gioco?",  // SetupHeading
    "Estrai l’immagine del tuo disco Xbox 360 originale e seleziona la cartella contenente default.xex.",  // SetupHint
    "Opzioni consigliate applicate.",  // RecommendedApplied
    "Posiziona broken_bond.exe accanto a questo launcher.",  // ExeMissing
    "Versione per PC non trovata",  // ExeMissingStatus
    "File di gioco rilevati",  // FilesDetected
    "Gioco",  // GroupGame
    "Lingua del gioco",  // GameLanguageLabel
    "Inglese",  // GameLanguageEnglish
    "Francese",  // GameLanguageFrench
    "Tedesco",  // GameLanguageGerman
    "Spagnolo",  // GameLanguageSpanish
    "Italiano",  // GameLanguageItalian
    "Personalizzata (%u)",  // GameLanguageCustom
    "Applicata all'avvio del gioco. I testi e le voci disponibili dipendono dai file di gioco.",  // GameLanguageDescription
    "Lingua dell'interfaccia del launcher",  // LauncherLanguageTooltip
    "Xenos (GPU emulata)",  // RendererXenos
    "Nativo (Direct3D 12)",  // RendererNative
    "Xenos emula la GPU Xbox 360 ed è la scelta predefinita. Direct3D 12 nativo è disponibile nelle build che lo includono; le sue opzioni si applicano solo selezionando Nativo.",  // RendererDescription
    "Non disponibile con il renderer nativo. Scegli Xenos in Renderer per usarlo.",  // XenosOnlyReason
    "MSAA",  // MsaaLabel
    "4x (originale)",  // MsaaOriginal
    "Multicampionamento delle scene 3D del gioco. 8x richiede una GPU più potente.",  // MsaaDescription
    "AA della trasparenza",  // TransparencyAaLabel
    "Ammorbidisce i bordi di fogliame, recinzioni e altre texture ritagliate (alpha to coverage).",  // TransparencyAaDescription
    "Frequenza fotogrammi",  // FrameRateLabel
    "30 è la frequenza originale del gioco. Oltre, la logica mantiene il suo ritmo e i fotogrammi intermedi vengono interpolati. Con il VSync attivo si ferma a quella del monitor; le frequenze alte richiedono una CPU più veloce.",  // FrameRateDescription
    "Luminosità",  // BrightnessLabel
    "50 è la luminosità originale del gioco.",  // BrightnessDescription
    "Latenza dei fotogrammi",  // FrameLatencyLabel
    "1 (minima)",  // FrameLatencyLowest
    "Fotogrammi in coda prima della visualizzazione. 1 ha il ritardo di input più basso; 2 o 3 possono uniformare tempi dei fotogrammi irregolari.",  // FrameLatencyDescription
    "Triplo buffer",  // TripleBufferingLabel
    "Tre buffer di visualizzazione invece di due.",  // TripleBufferingDescription
    "Sincronizza la presentazione con lo schermo. Con il renderer nativo, la velocità del gioco non dipende dal VSync.",  // VsyncNativeDescription
    "Scheda video",  // GraphicsDeviceLabel
    "Automatica",  // GraphicsDeviceAuto
    "La scheda video su cui gira il gioco. Automatica lascia la scelta al renderer. Sui portatili con due schede video, scegli quella dedicata (NVIDIA o AMD).",  // GraphicsDeviceDescription
    "Schermo",  // GroupDisplay
    "Avanzate",  // GroupAdvanced
    "solo Xenos",  // XenosOnlyTag
    "solo nativo",  // NativeOnlyTag
    "Solo con il renderer nativo. Scegli Nativo in Renderer per usarlo.",  // NativeOnlyReason
    "La risoluzione a cui viene disegnato il gioco, in multipli dei 1280×720 originali. Valori più alti sono più nitidi e richiedono una scheda video più potente.",  // ResolutionDescription
    "Il filtro anisotropico mantiene nitide le texture delle superfici viste di sbieco, come pavimenti e strade.",  // TextureFilteringDescription
    "Ammorbidisce i bordi seghettati filtrando l'immagine finale. Estremo leviga di più e può sfocare i dettagli fini.",  // FxaaDescription
    "Come l'immagine viene adattata allo schermo. CAS aumenta la nitidezza, FSR la ingrandisce con nitidezza e Bilineare la lascia morbida.",  // PostProcessingDescription
    "Aggiunge un rumore fine che nasconde le bande di colore nelle sfumature.",  // DitherDescription
    "Mostra frequenza e tempo dei fotogrammi in un angolo dello schermo. F1 lo attiva o disattiva durante il gioco.",  // FpsOverlayDescription
    "Avvia il gioco a schermo intero. Disattivato, gira in una finestra.",  // FullscreenDescription
    "Salta i loghi mostrati all'avvio del gioco. Il filmato d'apertura resta.",  // SkipIntroDescription
    "Legge i controller tramite SDL, che riconosce controller Xbox, PlayStation e altri. Disattivato, il gioco usa XInput, che riconosce solo controller compatibili con Xbox.",  // SdlInputDescription
    "Il controller collegato ora, con il nome che gli dà SDL.",  // ControllerDescription
    "Usa tastiera e mouse",  // KeyboardMouseLabel
    "Tastiera e mouse sostituiscono un controller Xbox e funzionano insieme a un controller collegato. Con il mouse sulla telecamera, il cursore resta nella finestra del gioco finché è attiva; Alt+Tab lo libera. Il gioco continua a mostrare i pulsanti del controller.",  // KeyboardMouseDescription
    "Il mouse muove la telecamera",  // MouseCameraLabel
    "Muove la telecamera (la levetta destra) con il mouse. Disattivato, la telecamera si muove solo con i tasti della levetta destra.",  // MouseCameraDescription
    "Sensibilità",  // MouseSensitivityLabel
    "La velocità con cui la telecamera segue il mouse. 100% è il valore predefinito.",  // MouseSensitivityDescription
    "Tasti",  // KeysLabel
    "I tasti e i pulsanti del mouse per ogni comando del controller, due per comando.",  // KeysDescription
    "Assegna tasti…",  // MapKeysButton
    "Assegna tasti",  // MapKeysTitle
    "Controlli › Tastiera e mouse",  // MapKeysPath
    "Pulsanti",  // KeysButtonsHeading
    "Levetta sinistra",  // LeftStickName
    "Levetta destra",  // RightStickName
    "Croce direzionale",  // DpadName
    "+ mouse",  // KeysPlusMouse
    "Su",  // DirectionUp
    "Giù",  // DirectionDown
    "Sinistra",  // DirectionLeft
    "Destra",  // DirectionRight
    "Fai clic e premi il nuovo tasto (5 s). Il clic destro cancella. Un tasto già usato lascia il comando precedente.",  // KeysHint
    "Premi… %d",  // KeyCapturePrompt
    "Ripristina tasti predefiniti",  // KeysRestoreDefaults
    "Fatto",  // KeysDone
    "%s è stato tolto da %s.",  // KeyMovedStatus
    "%s è usato dal gioco e non può essere assegnato.",  // KeyReservedStatus
    "Tasti predefiniti ripristinati.",  // KeysRestoredStatus
    "Spazio",  // KeySpaceName
    "Mouse sx",  // MouseLeftName
    "Mouse dx",  // MouseRightName
    "Mouse centrale",  // MouseMiddleName
    "Salvataggio non riuscito. Controlla il file di configurazione e i permessi di scrittura della cartella.",  // SaveFailed
    "Non disponibile",  // UnavailableTag
    "Ultrawide richiede ancora la verifica della proiezione della telecamera di Broken Bond e della presentazione Xenos. Il gioco usa 16:9.",  // AspectUnavailableReason
};

}  // namespace localization_strings
