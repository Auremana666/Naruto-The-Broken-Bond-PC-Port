#pragma once

namespace localization_strings {

inline constexpr const char* kFr[] = {
    "Naruto: The Broken Bond",  // WindowTitle
    "JOUER",  // TabPlay
    "PARAMÈTRES",  // TabSettings
    "FICHIERS",  // TabGameFiles
    "Qualité",  // GroupQuality
    "Commandes",  // GroupControls
    "Format d'image",  // AspectLabel
    "16:9 - Standard (original)",  // Aspect16x9
    "21:9 - UltraWide",  // Aspect21x9
    "32:9 - Super UltraWide",  // Aspect32x9
    "Ajuste le champ de vision horizontal de la caméra 3D sans étirer ni déformer les personnages.",  // AspectDescription
    "Résolution",  // ResolutionLabel
    "Moteur de rendu",  // RendererLabel
    "Filtrage des textures",  // TextureFilteringLabel
    "Par défaut",  // AnisoDefault
    "FXAA",  // FxaaLabel
    "Désactivé",  // FxaaOff
    "Normal",  // FxaaNormal
    "Extrême",  // FxaaExtreme
    "Post-traitement",  // PostProcessingLabel
    "Bilinéaire (par défaut)",  // PostBilinear
    "CAS (netteté)",  // PostCas
    "FSR EASU (mise à l'échelle)",  // PostFsr
    "Tramage",  // DitherLabel
    "Compteur FPS",  // FpsOverlayLabel
    "Plein écran",  // FullscreenLabel
    "VSync",  // VsyncLabel
    "Laissez cette option activée pour conserver la vitesse correcte des animations et de la physique.",  // VsyncDescription
    "Utiliser SDL pour les manettes (Xbox / PlayStation)",  // SdlInputLabel
    "Manette",  // ControllerLabel
    "Aucune manette détectée (clavier et souris)",  // NoControllerDetected
    "Passer les vidéos d'introduction (logos Ubisoft)",  // SkipIntroLabel
    "JOUER",  // BtnPlay
    "Réglages recommandés",  // BtnResetRecommended
    "Ouvrir le dossier du jeu",  // BtnOpenFolder
    "Les paramètres seront appliqués au lancement du jeu.",  // SettingsAppliedNotice
    "Jeu prêt",  // StatusReady
    "Fichiers du jeu introuvables",  // StatusNotFound
    "Sélectionnez le dossier extrait de Broken Bond dans FICHIERS.",  // FilesMissingDesc
    "(Aucun dossier trouvé)",  // NoneDetected
    "Sélectionner le dossier du jeu extrait...",  // BtnSelectFolder
    "Rétablir le chemin par défaut",  // BtnResetDefault
    "Chemin par défaut rétabli.",  // PathResetMsg
    "Les paramètres sont enregistrés au lancement du jeu ou à la fermeture du lanceur.",  // FooterNotice
    "Jeu lancé !",  // LaunchSuccessMsg
    "Impossible de démarrer le processus : ",  // LaunchFailedMsg
    "Exécutable du jeu introuvable dans : ",  // ExeNotFoundMsg
    "Chemin mis à jour : ",  // PathUpdatedMsg
    "Votre aventure commence ici.",  // PlayHeading
    "Retournez au village de Konoha.",  // PlayIntro
    "Fichiers du jeu détectés. Vous pouvez jouer.",  // ReadyDetail
    "Gérer les fichiers du jeu",  // ManageFiles
    "Associez vos fichiers originaux au port PC.",  // FilesIntro
    "Dossier du jeu",  // FolderHeading
    "Sélectionnez le dossier contenant default.xex. Vos fichiers originaux ne seront ni déplacés ni supprimés.",  // FolderHint
    "Besoin d’extraire votre jeu ?",  // SetupHeading
    "Extrayez l’image de votre disque Xbox 360 original, puis sélectionnez le dossier contenant default.xex.",  // SetupHint
    "Paramètres recommandés appliqués.",  // RecommendedApplied
    "Placez broken_bond.exe à côté de ce lanceur.",  // ExeMissing
    "Port PC introuvable",  // ExeMissingStatus
    "Fichiers du jeu détectés",  // FilesDetected
    "Jeu",  // GroupGame
    "Langue du jeu",  // GameLanguageLabel
    "Anglais",  // GameLanguageEnglish
    "Français",  // GameLanguageFrench
    "Allemand",  // GameLanguageGerman
    "Espagnol",  // GameLanguageSpanish
    "Italien",  // GameLanguageItalian
    "Personnalisée (%u)",  // GameLanguageCustom
    "Appliquée au prochain lancement. Les textes et voix disponibles dépendent des fichiers du jeu.",  // GameLanguageDescription
    "Langue de l'interface du lanceur",  // LauncherLanguageTooltip
    "Xenos (GPU émulé)",  // RendererXenos
    "Natif (Direct3D 12)",  // RendererNative
    "Xenos émule le GPU Xbox 360 et reste le choix par défaut. Le rendu Direct3D 12 natif est disponible dans les versions qui l’incluent ; ses options s’appliquent uniquement avec Natif.",  // RendererDescription
    "Non disponible avec le moteur de rendu natif. Choisissez Xenos dans Moteur de rendu pour l'utiliser.",  // XenosOnlyReason
    "MSAA",  // MsaaLabel
    "4x (original)",  // MsaaOriginal
    "Multi-échantillonnage des scènes 3D du jeu. Le 8x demande un GPU plus puissant.",  // MsaaDescription
    "AA de transparence",  // TransparencyAaLabel
    "Adoucit les bords du feuillage, des clôtures et des autres textures découpées (alpha to coverage).",  // TransparencyAaDescription
    "Fréquence d'images",  // FrameRateLabel
    "30 est la fréquence d'origine du jeu. Au-delà, la logique garde son rythme et les images intermédiaires sont interpolées. Avec la VSync, elle s'arrête à celle de l'écran ; les fréquences élevées demandent un CPU plus rapide.",  // FrameRateDescription
    "Luminosité",  // BrightnessLabel
    "50 correspond à la luminosité d'origine du jeu.",  // BrightnessDescription
    "Latence d'image",  // FrameLatencyLabel
    "1 (minimale)",  // FrameLatencyLowest
    "Images en file d'attente avant l'affichage. 1 offre la latence la plus faible ; 2 ou 3 peuvent lisser des temps d'image irréguliers.",  // FrameLatencyDescription
    "Triple tampon",  // TripleBufferingLabel
    "Trois tampons d'affichage au lieu de deux.",  // TripleBufferingDescription
    "Synchronise l'affichage avec l'écran. Avec le moteur de rendu natif, la vitesse du jeu ne dépend pas de la VSync.",  // VsyncNativeDescription
    "Carte graphique",  // GraphicsDeviceLabel
    "Automatique",  // GraphicsDeviceAuto
    "La carte graphique utilisée par le jeu. Automatique laisse le choix au moteur de rendu. Sur les portables à deux cartes graphiques, choisissez la carte dédiée (NVIDIA ou AMD).",  // GraphicsDeviceDescription
    "Affichage",  // GroupDisplay
    "Avancé",  // GroupAdvanced
    "Xenos uniquement",  // XenosOnlyTag
    "natif uniquement",  // NativeOnlyTag
    "Uniquement avec le moteur de rendu natif. Choisissez Natif dans Moteur de rendu pour l'utiliser.",  // NativeOnlyReason
    "La résolution à laquelle le jeu est dessiné, en multiples des 1280×720 d'origine. Des valeurs plus élevées donnent une image plus nette et demandent une carte graphique plus puissante.",  // ResolutionDescription
    "Le filtrage anisotrope garde les textures nettes sur les surfaces vues de biais, comme les sols et les routes.",  // TextureFilteringDescription
    "Adoucit les bords crénelés en filtrant l'image finale. Extrême lisse davantage et peut flouter les détails fins.",  // FxaaDescription
    "La façon dont l'image est adaptée à l'écran. CAS accentue la netteté, FSR agrandit l'image en l'affinant et le filtrage bilinéaire la garde douce.",  // PostProcessingDescription
    "Ajoute un bruit fin qui masque les bandes de couleur dans les dégradés.",  // DitherDescription
    "Affiche la fréquence et le temps d'image dans un coin de l'écran. F1 l'active ou le désactive en jeu.",  // FpsOverlayDescription
    "Lance le jeu en plein écran. Désactivé, il s'ouvre dans une fenêtre.",  // FullscreenDescription
    "Passe les logos affichés au démarrage du jeu. La cinématique d'ouverture est conservée.",  // SkipIntroDescription
    "Lit les manettes via SDL, qui reconnaît les manettes Xbox, PlayStation et d'autres. Désactivé, le jeu utilise XInput, qui ne reconnaît que les manettes compatibles Xbox.",  // SdlInputDescription
    "La manette connectée en ce moment, avec le nom que lui donne SDL.",  // ControllerDescription
    "Utiliser le clavier et la souris",  // KeyboardMouseLabel
    "Le clavier et la souris remplacent une manette Xbox et fonctionnent avec une manette connectée. Avec la souris sur la caméra, le curseur reste dans la fenêtre du jeu tant qu'elle est active ; Alt+Tab le libère. Le jeu affiche toujours les boutons de la manette.",  // KeyboardMouseDescription
    "La souris déplace la caméra",  // MouseCameraLabel
    "Déplace la caméra (le stick droit) avec la souris. Désactivé, la caméra ne bouge qu'avec les touches du stick droit.",  // MouseCameraDescription
    "Sensibilité",  // MouseSensitivityLabel
    "La vitesse à laquelle la caméra suit la souris. 100 % est la valeur par défaut.",  // MouseSensitivityDescription
    "Touches",  // KeysLabel
    "Les touches et boutons de la souris de chaque commande de la manette, deux par commande.",  // KeysDescription
    "Configurer les touches…",  // MapKeysButton
    "Configurer les touches",  // MapKeysTitle
    "Commandes › Clavier et souris",  // MapKeysPath
    "Boutons",  // KeysButtonsHeading
    "Stick gauche",  // LeftStickName
    "Stick droit",  // RightStickName
    "Croix directionnelle",  // DpadName
    "+ souris",  // KeysPlusMouse
    "Haut",  // DirectionUp
    "Bas",  // DirectionDown
    "Gauche",  // DirectionLeft
    "Droite",  // DirectionRight
    "Cliquez puis appuyez sur la nouvelle touche (5 s). Clic droit pour effacer. Une touche déjà utilisée quitte sa commande précédente.",  // KeysHint
    "Appuyez… %d",  // KeyCapturePrompt
    "Rétablir les touches par défaut",  // KeysRestoreDefaults
    "Terminé",  // KeysDone
    "%s a été retirée de %s.",  // KeyMovedStatus
    "%s est utilisée par le jeu et ne peut pas être attribuée.",  // KeyReservedStatus
    "Touches par défaut rétablies.",  // KeysRestoredStatus
    "Espace",  // KeySpaceName
    "Clic gauche",  // MouseLeftName
    "Clic droit",  // MouseRightName
    "Clic milieu",  // MouseMiddleName
    "Enregistrement impossible. Vérifiez le fichier de configuration et les droits d’écriture du dossier.",  // SaveFailed
    "Indisponible",  // UnavailableTag
    "L’ultrawide attend la validation de la projection de caméra de Broken Bond et de la présentation Xenos. Le jeu utilise le format 16:9.",  // AspectUnavailableReason
};

}  // namespace localization_strings
