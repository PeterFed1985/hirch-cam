// SPDX-License-Identifier: GPL-2.0-or-later
/** @file
 * Hirch CNC: Datei-Fenster (Öffnen, Speichern, Ordner wählen) für die eigenen Dialoge.
 * Unter Windows das Fenster von Windows (Gtk::FileChooserNative – deutsche Ordnernamen, wie Inkscapes
 * Datei-Fenster seit filedialogimpl-gtkmm.h), sonst wie bisher das GTK-Fenster.
 * Nur für hirch-*.cpp – kein öffentlicher Header.
 */
#ifndef HIRCH_DATEIWAHL_H
#define HIRCH_DATEIWAHL_H

#include <memory>
#include <gtkmm/filechooserdialog.h>
#include <gtkmm/filechoosernative.h>
#include <gtkmm/filechooserbutton.h>
#include <glibmm/miscutils.h>

namespace Inkscape::UI::Dialog {

class HirchDateiwahl {
public:
    /// knopf = Beschriftung der Bestätigung («Öffnen», «Speichern», «Wählen» …)
    HirchDateiwahl(Gtk::Window *eltern, Glib::ustring const &titel, Gtk::FileChooserAction art,
                   Glib::ustring const &abbrechen, Glib::ustring const &knopf)
        : titel_(titel)
    {
#ifdef _WIN32
        nativ_ = Gtk::FileChooserNative::create(titel, art, knopf, abbrechen);
        if (eltern) nativ_->set_transient_for(*eltern);
        nativ_->set_modal(true);
#else
        gtk_ = std::make_unique<Gtk::FileChooserDialog>(titel, art);
        if (eltern) gtk_->set_transient_for(*eltern);
        gtk_->add_button(abbrechen, Gtk::RESPONSE_CANCEL);
        gtk_->add_button(knopf, Gtk::RESPONSE_OK);
        gtk_->set_default_response(Gtk::RESPONSE_OK);
#endif
    }

    /// Filter, Ordner, Name usw. – die gemeinsamen Teile beider Fenster
    Gtk::FileChooser &fc()
    {
#ifdef _WIN32
        return *nativ_.get();
#else
        return *gtk_;
#endif
    }

    /// Hinweis unter dem Fenster (GTK); das Windows-Fenster nimmt keine eigenen Teile auf
    /// (GTK fiele sonst auf sein eigenes Fenster zurück) – dort steht der Text in der Titelzeile.
    void hinweis(Gtk::Widget &w, Glib::ustring const &text)
    {
#ifdef _WIN32
        (void)w;
        nativ_->set_title(titel_ + " – " + text);
#else
        (void)text;
        gtk_->set_extra_widget(w);
#endif
    }

    /// true = bestätigt
    bool run()
    {
#ifdef _WIN32
        return nativ_->run() == Gtk::RESPONSE_ACCEPT;
#else
        return gtk_->run() == Gtk::RESPONSE_OK;
#endif
    }

    void hide()
    {
#ifdef _WIN32
        nativ_->hide();
#else
        gtk_->hide();
#endif
    }

private:
    Glib::ustring titel_;
#ifdef _WIN32
    Glib::RefPtr<Gtk::FileChooserNative> nativ_;
#else
    std::unique_ptr<Gtk::FileChooserDialog> gtk_;
#endif
};

/// Knopf «Ordner wählen» in den Einstellungen: unter Windows ein Knopf mit dem Ordnernamen, der das Fenster von
/// Windows öffnet (Gtk::FileChooserButton zeigt eine GTK-Liste mit englischen Ordnernamen), sonst wie bisher.
#ifdef _WIN32
class HirchOrdnerKnopf : public Gtk::Button {
public:
    HirchOrdnerKnopf(Glib::ustring const &titel, Glib::ustring const &abbrechen, Glib::ustring const &waehlen)
        : titel_(titel), abbrechen_(abbrechen), waehlen_(waehlen)
    {
        set_image_from_icon_name("folder");
        set_always_show_image(true);
        zeigen();
    }

    void set_filename(std::string const &ordner)
    {
        ordner_ = ordner;
        zeigen();
    }
    std::string get_filename() const { return ordner_; }
    void unselect_all() { set_filename({}); }
    sigc::signal<void> &signal_file_set() { return file_set_; }

protected:
    void on_clicked() override
    {
        HirchDateiwahl dateiwahl(dynamic_cast<Gtk::Window *>(get_toplevel()), titel_,
                                 Gtk::FILE_CHOOSER_ACTION_SELECT_FOLDER, abbrechen_, waehlen_);
        if (!ordner_.empty()) dateiwahl.fc().set_current_folder(ordner_);
        if (!dateiwahl.run()) return;
        auto neu = dateiwahl.fc().get_filename();
        dateiwahl.hide();
        if (neu.empty()) return;
        set_filename(neu);
        file_set_.emit();
    }

private:
    void zeigen() { set_label(ordner_.empty() ? Glib::ustring("–") : Glib::filename_display_basename(ordner_)); }

    Glib::ustring titel_, abbrechen_, waehlen_;
    std::string ordner_;
    sigc::signal<void> file_set_;
};
#else
class HirchOrdnerKnopf : public Gtk::FileChooserButton {
public:
    HirchOrdnerKnopf(Glib::ustring const &titel, Glib::ustring const &, Glib::ustring const &)
        : Gtk::FileChooserButton(titel, Gtk::FILE_CHOOSER_ACTION_SELECT_FOLDER)
    {}
};
#endif

} // namespace Inkscape::UI::Dialog

#endif // HIRCH_DATEIWAHL_H
