.pragma library

function name(code) {
    switch (code) {
    case "auto": return qsTranslate("Languages", "Detect automatically");
    case "en": return qsTranslate("Languages", "English");
    case "zh-Hans": return qsTranslate("Languages", "Chinese (Simplified)");
    case "zh-Hant": return qsTranslate("Languages", "Chinese (Traditional)");
    case "ja": return qsTranslate("Languages", "Japanese");
    case "ko": return qsTranslate("Languages", "Korean");
    case "de": return qsTranslate("Languages", "German");
    case "fr": return qsTranslate("Languages", "French");
    case "es": return qsTranslate("Languages", "Spanish");
    case "ru": return qsTranslate("Languages", "Russian");
    case "pt": return qsTranslate("Languages", "Portuguese");
    case "it": return qsTranslate("Languages", "Italian");
    case "ar": return qsTranslate("Languages", "Arabic");
    default: return code;
    }
}
