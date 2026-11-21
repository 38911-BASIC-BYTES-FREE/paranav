package pl.paranav.uploader;

import java.util.Locale;

/** App language: Polish when the phone is set to Polish, English otherwise. */
final class Tr {
    private Tr() {}

    static boolean pl = "pl".equals(Locale.getDefault().getLanguage());

    static String t(String polish, String english) {
        return pl ? polish : english;
    }
}
