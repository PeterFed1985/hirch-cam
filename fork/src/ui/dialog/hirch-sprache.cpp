// SPDX-License-Identifier: GPL-2.0-or-later
/** @file
 * Hirch CNC: Sprache (deutsch im Quelltext, englischer Katalog)
 */
#include "ui/dialog/hirch-hilfen.h"

namespace Inkscape::UI::Dialog {

// ---------------------------------------------------------------------------
// Sprache: deutsche Texte im Quelltext, englischer Katalog (hirch_cam/sprache/en.json)
// ---------------------------------------------------------------------------
namespace {
struct Katalog {
    bool geladen = false;
    bool en = false;
    std::map<std::string, std::string> fest;
    struct Muster {
        std::vector<std::string> teile_de;    // Text zwischen den {}
        std::vector<std::string> teile_en;
    };
    std::vector<Muster> muster;
};

std::vector<std::string> muster_teile(std::string const &s)
{
    std::vector<std::string> t;
    std::size_t a = 0;
    for (;;) {
        auto p = s.find("{}", a);
        if (p == std::string::npos) {
            t.push_back(s.substr(a));
            return t;
        }
        t.push_back(s.substr(a, p - a));
        a = p + 2;
    }
}
} // namespace


static Katalog &katalog()
{
    static Katalog k;
    if (k.geladen) return k;
    k.geladen = true;
    auto s = Inkscape::Preferences::get()->getString("/hirch/sprache");
    std::string l = s.empty() ? std::string() : s.raw();
    if (l.empty() || l == "auto") {
        char const *e = g_getenv("LANGUAGE");
        if (!e || !*e) e = g_getenv("LC_ALL");
        if (!e || !*e) e = g_getenv("LANG");
        l = (e && std::string(e).rfind("de", 0) == 0) ? "de" : "en";
        if (e && !*e) l = "de";
    }
    k.en = l.rfind("en", 0) == 0;
    if (!k.en) return k;
    try {
        auto pfad = Glib::build_filename(hirch_repo_pfad(), "hirch_cam", "sprache", "en.json");
        auto text = Glib::file_get_contents(pfad);
        auto wert = boost::json::parse(text);
        if (auto o = wert.if_object()) {
            for (auto const &kv : *o) {
                if (!kv.value().is_string()) continue;
                std::string de(kv.key()), en(kv.value().get_string());
                if (en.empty()) continue;
                if (de.find("{}") != std::string::npos) {
                    auto td = muster_teile(de), te = muster_teile(en);
                    if (td.size() == te.size()) {
                        k.muster.push_back({td, te});
                        k.fest[de] = en;              // für TRF: das ganze Muster
                    }
                } else {
                    k.fest[de] = en;
                }
            }
        }
        // längere Muster zuerst (genauer)
        std::stable_sort(k.muster.begin(), k.muster.end(), [](auto const &a, auto const &b) {
            std::size_t la = 0, lb = 0;
            for (auto const &x : a.teile_de) la += x.size();
            for (auto const &x : b.teile_de) lb += x.size();
            return la > lb;
        });
    } catch (...) {
    }
    return k;
}

bool hirch_englisch() { return katalog().en; }
char const *hirch_sprache() { return katalog().en ? "en" : "de"; }

static char const *TR_ohne_einheit(char const *de)
{
    auto &k = katalog();
    if (!k.en || !de) return de;
    auto it = k.fest.find(de);
    return it == k.fest.end() ? de : it->second.c_str();
}

char const *TR(char const *de)
{
    char const *t = TR_ohne_einheit(de);
    if (!t || !hirch_zoll() || !std::strstr(t, "mm")) return t;
    // Zoll: umgerechneter Text, einmal gebildet und behalten (der Zeiger bleibt gültig)
    static std::map<std::string, std::string> zoll;
    auto it = zoll.find(t);
    // nur Zahlen mit Einheit: ein einzelnes TR("mm") hängt oft hinter einer Zahl, die selbst nicht umgerechnet ist
    // («Ø " + zahl + " " + TR("mm")) – «6 Zoll» wäre falsch. Das ganze «6 mm» rechnet die Beschriftung beim Zeigen um.
    if (it == zoll.end()) it = zoll.emplace(t, einheit_text_zahlen(t)).first;
    return it->second.c_str();
}

static bool muster_passt(Katalog::Muster const &m, std::string const &s, std::vector<std::string> &werte)
{
    werte.clear();
    auto const &t = m.teile_de;
    if (s.compare(0, t[0].size(), t[0]) != 0) return false;
    std::size_t pos = t[0].size();
    for (std::size_t i = 1; i < t.size(); ++i) {
        std::size_t p;
        if (i + 1 == t.size()) {
            if (s.size() < t[i].size() || s.size() - t[i].size() < pos) return false;
            p = s.size() - t[i].size();
            if (s.compare(p, t[i].size(), t[i]) != 0) return false;
        } else {
            if (t[i].empty()) return false;
            p = s.find(t[i], pos);
            if (p == std::string::npos) return false;
        }
        werte.push_back(s.substr(pos, p - pos));
        pos = p + t[i].size();
    }
    return pos == s.size();
}

static std::string TR_text(std::string const &de);

std::string TR(std::string const &de)
{
    return einheit_text_zahlen(TR_text(de));
}

static std::string TR_text(std::string const &de)
{
    auto &k = katalog();
    if (!k.en || de.empty()) return de;
    auto it = k.fest.find(de);
    if (it != k.fest.end()) return it->second;
    {
        auto a = de.find_first_not_of(' '), e = de.find_last_not_of(' ');
        if (a == std::string::npos) return de;
        if (a > 0 || e + 1 < de.size()) return de.substr(0, a) + TR(de.substr(a, e + 1 - a)) + de.substr(e + 1);
    }
    std::vector<std::string> werte;
    for (auto const &m : k.muster) {
        if (!muster_passt(m, de, werte)) continue;
        std::string aus = m.teile_en[0];
        for (std::size_t i = 0; i < werte.size(); ++i) {
            auto w = werte[i];
            // Dezimalkomma → Punkt in Zahlen
            if (std::regex_match(w, std::regex(R"(-?\d+,\d+)"))) std::replace(w.begin(), w.end(), ',', '.');
            aus += TR(w) + m.teile_en[i + 1];
        }
        return aus;
    }
    // zusammengesetzt: Zeilen einzeln, sonst «Kopf: Rest», «A – B», «A · B»
    if (de.find('\n') != std::string::npos) {
        std::string aus;
        std::size_t a = 0;
        for (;;) {
            auto q = de.find('\n', a);
            aus += TR(de.substr(a, q == std::string::npos ? std::string::npos : q - a));
            if (q == std::string::npos) break;
            aus += '\n';
            a = q + 1;
        }
        return aus;
    }
    for (char const *trenner : {": ", " – ", " · "}) {
        auto p = de.find(trenner);
        if (p == std::string::npos) continue;
        std::string tr(trenner), kopf = de.substr(0, p), rest = de.substr(p + tr.size());
        auto nk = TR(kopf), nr = TR(rest);
        if (nk != kopf || nr != rest) return nk + tr + nr;
    }
    return de;
}


std::string TRF(char const *muster, std::vector<std::string> const &werte)
{
    std::string m = TR_ohne_einheit(muster);
    std::string aus;
    std::size_t a = 0, i = 0;
    for (;;) {
        auto p = m.find("{}", a);
        if (p == std::string::npos) break;
        aus += m.substr(a, p - a) + (i < werte.size() ? werte[i] : std::string());
        ++i;
        a = p + 2;
    }
    return einheit_text_zahlen(aus + m.substr(a));
}

bool hirch_im_katalog(std::string const &text)
{
    auto &k = katalog();
    if (k.fest.count(text)) return true;
    std::vector<std::string> werte;
    for (auto const &m : k.muster)
        if (muster_passt(m, text, werte)) return true;
    return false;
}

// ---------------------------------------------------------------------------
// Einheit: mm intern, Anzeige mm oder Zoll (siehe hirch-sprache.h)
// ---------------------------------------------------------------------------
constexpr double ZOLL_MM = 25.4;

static void felder_automatisch();

bool hirch_zoll()
{
    // einmal beim Start gelesen, wie die Sprache: alle Fenster zeigen dieselbe Einheit. Umschalten gilt nach einem
    // Neustart (Einstellungen › Programm, Ersteinrichtung startet selbst neu).
    // HIRCH_EINHEIT=in|mm (Tests) geht der Einstellung vor
    static bool const z = g_getenv("HIRCH_EINHEIT") && *g_getenv("HIRCH_EINHEIT")
                              ? std::string(g_getenv("HIRCH_EINHEIT")) == "in"
                              : Inkscape::Preferences::get()->getString("/hirch/einheit") == "in";
    static bool eingerichtet = false;
    if (z && !eingerichtet) {
        eingerichtet = true;
        felder_automatisch();
    }
    return z;
}

char const *hirch_einheit() { return hirch_zoll() ? "in" : "mm"; }

std::string einheit_name(Groesse g)
{
    // deutsch «Zoll» (sonst verwechselbar mit dem Wort «in»), englisch «in» – wie hirch_cam/einheit.py
    std::string e = !hirch_zoll() ? "mm" : (hirch_englisch() ? "in" : "Zoll");
    switch (g) {
    case Groesse::SCHNITT: return hirch_zoll() ? "ft/min" : "m/min";
    case Groesse::VORSCHUB: return e + "/min";
    case Groesse::FLAECHE: return e + "²";
    default: return e;
    }
}

static double faktor(Groesse g)
{
    if (g == Groesse::SCHNITT) return 0.3048;            // m/min je ft/min
    return g == Groesse::FLAECHE ? ZOLL_MM * ZOLL_MM : ZOLL_MM;
}

double einheit_zeigen(double mm, Groesse g) { return hirch_zoll() ? mm / faktor(g) : mm; }
double einheit_lesen(double anzeige, Groesse g) { return hirch_zoll() ? anzeige * faktor(g) : anzeige; }

/// Nachkommastellen in Zoll zu den Stellen in mm: 0,1 mm ≈ 0,004 in – eine Stelle mehr, mindestens drei
static int zoll_stellen(int stellen_mm, Groesse g)
{
    if (g == Groesse::SCHNITT) return 0;
    if (g == Groesse::VORSCHUB) return std::max(1, stellen_mm);
    if (g == Groesse::FLAECHE) return std::max(2, stellen_mm);
    return std::clamp(stellen_mm + 1, 3, 4);
}

static std::string zahl_text(double v, int stellen)
{
    char buf[64];
    std::snprintf(buf, sizeof buf, "%.*f", stellen, v);
    std::string s = buf;
    std::replace(s.begin(), s.end(), ',', '.');
    if (s.find('.') != std::string::npos) {
        while (!s.empty() && s.back() == '0') s.pop_back();
        if (!s.empty() && s.back() == '.') s.pop_back();
    }
    if (s == "-0") s = "0";
    if (!hirch_englisch()) std::replace(s.begin(), s.end(), '.', ',');
    return s;
}

std::string einheit_zahl(double mm, int stellen, Groesse g, bool mit_einheit)
{
    bool z = hirch_zoll();
    auto t = zahl_text(z ? mm / faktor(g) : mm, z ? zoll_stellen(stellen, g) : stellen);
    return mit_einheit ? t + " " + einheit_name(g) : t;
}

std::string zoll_bruch(double zoll)
{
    // 1/32 … 12 Zoll, Abweichung höchstens 0,006 mm: 6,35 → 1/4, 3,18 → 1/8; 6 mm oder 25 mm bleiben Dezimalzahlen
    if (!(zoll >= 1.0 / 32 - 1e-9) || zoll > 12) return {};   // ab 1/32 (Windows 10.11.)
    long r = std::lround(zoll * 32);
    if (std::abs(zoll - r / 32.0) * ZOLL_MM > 0.006) return {};
    long ganz = r / 32, rest = r % 32, nenner = 32;
    while (rest && rest % 2 == 0) { rest /= 2; nenner /= 2; }
    if (!rest) return std::to_string(ganz);
    auto b = std::to_string(rest) + "/" + std::to_string(nenner);
    return ganz ? std::to_string(ganz) + "-" + b : b;
}

std::string einheit_mass(double mm, std::string const &mm_text, bool bruch)
{
    if (!hirch_zoll()) return mm_text;
    auto b = bruch ? zoll_bruch(mm / ZOLL_MM) : std::string();
    return (b.empty() ? zahl_text(mm / ZOLL_MM, 3) : b) + " " + einheit_name();
}

std::string einheit_weg(double mm)
{
    if (hirch_zoll()) return zahl_text(mm / (ZOLL_MM * 12), 1) + " ft";
    return zahl_text(mm / 1000.0, 1) + " m";
}

/// eine Zahl wie im Text: «12», «-3,5», «0.25», «1 300» (schmales Leerzeichen)
static bool zahl_lesen(std::string const &t, double &wert, int &stellen)
{
    std::string s;
    stellen = 0;
    bool nach_komma = false;
    for (std::size_t i = 0; i < t.size(); ++i) {
        unsigned char c = t[i];
        if (std::isdigit(c)) {
            s += (char)c;
            if (nach_komma) ++stellen;
        } else if ((c == ',' || c == '.') && !nach_komma) {
            s += '.';
            nach_komma = true;
        } else if (c == '-' && s.empty()) {
            s += '-';
        } else if (c == 0xE2 && i + 2 < t.size()) {
            i += 2;                                // schmales Leerzeichen (Tausender)
        } else if (c == ' ') {
        } else {
            return false;
        }
    }
    // nicht std::stod: das liest nach dem Gebietsschema (de: «0.5» wäre 0)
    if (s.empty() || s == "-" || s == ".") return false;
    char *ende = nullptr;
    wert = g_ascii_strtod(s.c_str(), &ende);
    return ende && *ende == 0;
}

static std::string einheit_text_art(std::string const &text, bool einzeln);

std::string einheit_text(std::string const &text) { return einheit_text_art(text, true); }
std::string einheit_text_zahlen(std::string const &text) { return einheit_text_art(text, false); }

static std::string einheit_text_art(std::string const &text, bool einzeln)
{
    if (!hirch_zoll() || text.find("mm") == std::string::npos) return text;
    // Zahl (auch «600 × 900») vor «mm/min», «mm²», «mm» als eigenes Wort
    static std::string const z = std::string(R"(-?\d+(?:)") + "\u202F" + R"(\d{3})*(?:[.,]\d+)?)";
    static std::regex const re("((?:" + z + ")(?:\\s*(?:\u00D7|x|\u2026)\\s*" + z + ")*)?(\\s?)(mm/min|mm\u00B2|mm)(?![A-Za-z])");
    std::string aus;
    auto anfang = std::sregex_iterator(text.begin(), text.end(), re);
    std::size_t letzt = 0;
    for (auto it = anfang; it != std::sregex_iterator(); ++it) {
        auto const &m = *it;
        // «mm» mitten in einem Wort (z. B. «Summe»): nicht anfassen
        auto p = (std::size_t)m.position(0);
        if (!m[1].matched && p > 0 && std::isalpha((unsigned char)text[p - 1])) continue;
        if (!m[1].matched && !einzeln) continue;
        aus += text.substr(letzt, p - letzt);
        std::string einheit = m[3].str();
        Groesse g = einheit == "mm/min" ? Groesse::VORSCHUB : (einheit == "mm" ? Groesse::LAENGE : Groesse::FLAECHE);
        if (m[1].matched) {
            // jede Zahl der Kette umrechnen («600 × 900 mm» → «23.622 × 35.433 in»)
            std::string kette = m[1].str(), neu;
            bool mehrere = kette.find("\u00D7") != std::string::npos || kette.find('x') != std::string::npos ||
                           kette.find("\u2026") != std::string::npos;
            auto vor = text.substr(0, p);
            while (!vor.empty() && vor.back() == ' ') vor.pop_back();
            bool durchmesser = vor.size() >= 2 && vor.compare(vor.size() - 2, 2, "\u00D8") == 0;
            static std::regex const zahl_re(z);
            std::size_t l2 = 0;
            for (auto zt = std::sregex_iterator(kette.begin(), kette.end(), zahl_re); zt != std::sregex_iterator(); ++zt) {
                neu += kette.substr(l2, zt->position(0) - l2);
                double w = 0;
                int st = 0;
                if (!zahl_lesen(zt->str(), w, st)) { neu += zt->str(); l2 = zt->position(0) + zt->length(0); continue; }
                // «Ø 6,35 mm» → «Ø 1/4 Zoll» (Fräser, Bohrer); sonst Dezimalzahl
                std::string b = (durchmesser && g == Groesse::LAENGE && !mehrere) ? zoll_bruch(w / ZOLL_MM) : std::string();
                neu += b.empty() ? zahl_text(w / faktor(g), zoll_stellen(st, g)) : b;
                l2 = zt->position(0) + zt->length(0);
            }
            aus += neu + kette.substr(l2);
        }
        aus += m[2].str() + einheit_name(g);
        letzt = p + m.length(0);
    }
    return aus + text.substr(letzt);
}

bool einheit_erkennen(std::string const &b, Groesse &g)
{
    if (b == "mm" || b == "in" || b == "Zoll") { g = Groesse::LAENGE; return true; }
    if (b == "mm/min" || b == "in/min" || b == "Zoll/min") { g = Groesse::VORSCHUB; return true; }
    if (b == "m/min" || b == "ft/min") { g = Groesse::SCHNITT; return true; }
    for (char const *v : {"mm/min ", "in/min ", "Zoll/min "})              // «mm/min · wie das Werkzeug»
        if (b.rfind(v, 0) == 0) { g = Groesse::VORSCHUB; return true; }
    for (char const *v : {"mm ", "Zoll "})                                 // «mm je Seite»
        if (b.rfind(v, 0) == 0) { g = Groesse::LAENGE; return true; }
    // «in» nur mit Zeichen danach, nicht als englisches Wort («in the corners»)
    if (b.rfind("in ", 0) == 0 && b.size() > 3 && !std::isalpha((unsigned char)b[3])) { g = Groesse::LAENGE; return true; }
    return false;
}

std::string einheit_feld(void *sp, Groesse g)
{
    auto *s = static_cast<Gtk::SpinButton *>(sp);
    if (!s || !hirch_zoll()) return einheit_name(g);
    if (s->get_data("hirch-einheit")) return einheit_name(g);
    s->set_data("hirch-einheit", (void *)1);
    // «numeric» liesse bei 0 Nachkommastellen keinen Dezimalpunkt zu – wir lesen den Text selbst
    s->set_numeric(false);
    // Schritte in Zoll: 0,1 mm → 0.005 in, 1 mm → 1/32 in, 10 mm → 0.5 in …
    auto adj = s->get_adjustment();
    double f = faktor(g);
    auto runden = [](double v) {
        for (double r : {0.001, 0.005, 0.01, 0.025, 0.05, 0.1, 0.25, 0.5, 1.0, 5.0, 10.0, 50.0, 100.0})
            if (v <= r * 1.01) return r;
        return 100.0;
    };
    double schritt = runden(adj->get_step_increment() / f) * f;
    adj->set_step_increment(schritt);
    adj->set_page_increment(schritt * 10);
    int stellen = zoll_stellen((int)s->get_digits(), g);
    s->signal_output().connect([s, f, stellen]() {
        s->set_text(zahl_text(s->get_adjustment()->get_value() / f, stellen));
        return true;
    }, false);
    s->signal_input().connect([s, f, stellen](double *neu) {
        std::string t = s->get_text().raw();
        // unverändert angezeigt: Wert nicht durch die Rundung der Anzeige verschieben (610 mm bleibt 610 mm)
        double jetzt = s->get_adjustment()->get_value();
        if (t == zahl_text(jetzt / f, stellen)) {
            *neu = jetzt;
            return 1;
        }
        // Brüche wie «1/4» und Angaben mit «"» oder «in» annehmen
        for (char const *weg : {"\"", "in", " "}) {
            for (auto p = t.find(weg); p != std::string::npos; p = t.find(weg)) t.erase(p, std::strlen(weg));
        }
        double w = 0;
        int st = 0;
        auto bruch = t.find('/');
        if (bruch != std::string::npos) {
            double a = 0, b = 0;
            std::string ganz_teil;
            auto minus = t.find('-');        // «1-1/4»
            if (minus != std::string::npos && minus > 0) {
                ganz_teil = t.substr(0, minus);
                t = t.substr(minus + 1);
                bruch = t.find('/');
            }
            if (!zahl_lesen(t.substr(0, bruch), a, st) || !zahl_lesen(t.substr(bruch + 1), b, st) || b == 0)
                return (int)GTK_INPUT_ERROR;
            w = a / b;
            double g2 = 0;
            if (!ganz_teil.empty() && zahl_lesen(ganz_teil, g2, st)) w += g2;
        } else if (!zahl_lesen(t, w, st)) {
            return (int)GTK_INPUT_ERROR;
        }
        *neu = w * f;
        return 1;
    });     // nach dem Standard-Handler (after): sonst überschreibt dessen «nicht behandelt» unseren Wert
    // gleich in Zoll zeigen. Nicht update(): das läse den Text in mm als Zoll.
    s->set_text(zahl_text(adj->get_value() / f, stellen));
    return einheit_name(g);
}

void einheit_nie(void *sp)
{
    if (auto *s = static_cast<Gtk::SpinButton *>(sp)) s->set_data("hirch-einheit-nie", (void *)1);
}

/// Beschriftung rechts neben einem Zahlenfeld (gleiche Zeile im Grid, 1–2 Spalten weiter, oder das nächste Kind
/// der Box); mit nur_einheit nur, wenn sie eine Längen-/Vorschub-Einheit nennt
static Gtk::Label *nachbar_label(Gtk::SpinButton *s, bool nur_einheit)
{
    if (!s) return nullptr;
    auto *eltern = s->get_parent();
    auto pass = [nur_einheit](Gtk::Widget *w) -> Gtk::Label * {
        auto *l = dynamic_cast<Gtk::Label *>(w);
        if (!l) return nullptr;
        Groesse g;
        return !nur_einheit || einheit_erkennen(l->get_text().raw(), g) ? l : nullptr;
    };
    if (auto *grid = dynamic_cast<Gtk::Grid *>(eltern)) {
        int links = 0, oben = 0, breite = 1;
        gtk_container_child_get(GTK_CONTAINER(grid->gobj()), GTK_WIDGET(s->gobj()), "left-attach", &links, "top-attach",
                                &oben, "width", &breite, nullptr);
        for (int dx = 0; dx < 2; ++dx)
            if (auto *w = grid->get_child_at(links + breite + dx, oben))
                if (auto *l = pass(w)) return l;
        return nullptr;
    }
    if (auto *box = dynamic_cast<Gtk::Box *>(eltern)) {
        auto kinder = box->get_children();
        for (std::size_t i = 0; i + 1 < kinder.size(); ++i)
            if (kinder[i] == s) return pass(kinder[i + 1]);
    }
    return nullptr;
}

void *einheit_nachbar(void *sp) { return nachbar_label(static_cast<Gtk::SpinButton *>(sp), true); }

/// Feld und Beschriftung angleichen: nennt die Beschriftung eine Einheit, rechnet das Feld um; «… mm» im Text wird
/// umgerechnet
static void angleichen(Gtk::SpinButton *s, Gtk::Label *l)
{
    if (!s || !l) return;
    auto t = l->get_text().raw();
    Groesse g;
    if (!s->get_data("hirch-einheit") && !s->get_data("hirch-einheit-nie") && einheit_erkennen(t, g)) einheit_feld(s, g);
    if (t.find("mm") == std::string::npos) return;
    // ein einzelnes «mm» wird nur zur Einheit, wenn das Feld daneben wirklich umrechnet
    auto umrechnen = s->get_data("hirch-einheit") ? einheit_text : einheit_text_zahlen;
    auto neu = umrechnen(t);
    if (neu == t) return;
    if (l->get_use_markup()) l->set_markup(umrechnen(l->get_label().raw()));
    else l->set_text(neu);
}

/// Zoll: jedes Zahlenfeld mit «mm» daneben rechnet von selbst um – beim ersten Zeigen (map). So braucht nicht jede
/// Stelle im Code einen eigenen Aufruf; der Oberflächentest prüft dieselbe Regel (einheit_nachbar).
static gboolean beim_zeigen(GSignalInvocationHint *, guint n, GValue const *werte, gpointer)
{
    if (n < 1) return TRUE;
    auto *gw = static_cast<GtkWidget *>(g_value_get_object(&werte[0]));
    if (gw && GTK_IS_LABEL(gw)) {
        // jede Beschriftung: Zahlen mit «mm» umrechnen, auch wenn sie später neu gesetzt wird
        auto *l = Glib::wrap(GTK_LABEL(gw));
        auto zahlen = [l] {
            auto roh = l->get_label().raw();
            if (roh.find("mm") == std::string::npos) return;
            auto neu = einheit_text_zahlen(roh);
            if (neu == roh) return;
            if (l->get_use_markup()) l->set_markup(neu);
            else l->set_label(neu);
        };
        zahlen();
        if (!l->get_data("hirch-einheit-text")) {
            l->set_data("hirch-einheit-text", (void *)1);
            l->property_label().signal_changed().connect(zahlen);
        }
        return TRUE;
    }
    if (!gw || !GTK_IS_SPIN_BUTTON(gw)) return TRUE;
    auto *s = Glib::wrap(GTK_SPIN_BUTTON(gw));
    if (s->get_data("hirch-einheit-nie")) return TRUE;
    auto *l = nachbar_label(s, false);
    if (!l) return TRUE;
    angleichen(s, l);
    // Erklärungen daneben ändern sich später («mm → 3 Zustellungen …»): dann wieder angleichen
    if (!l->get_data("hirch-einheit-beobachtet")) {
        l->set_data("hirch-einheit-beobachtet", (void *)1);
        l->property_label().signal_changed().connect([s, l] { angleichen(s, l); });
    }
    return TRUE;
}

static void felder_automatisch()
{
    auto *klasse = g_type_class_ref(GTK_TYPE_SPIN_BUTTON);
    auto *klasse2 = g_type_class_ref(GTK_TYPE_LABEL);   // damit «map» auch für Beschriftungen bekannt ist
    guint sig = g_signal_lookup("map", GTK_TYPE_WIDGET);
    if (sig) g_signal_add_emission_hook(sig, 0, beim_zeigen, nullptr, nullptr);
    g_type_class_unref(klasse);
    g_type_class_unref(klasse2);
}

} // namespace Inkscape::UI::Dialog
