#pragma once
#include "AppContext.h"
#include "../core/CrashReport.h"
#include "../core/CoreReservation.h"
#include "VideoSource.h"

namespace td
{
/** The folder of fermata-log.txt and the crash reports: Documents\Fermata. */
inline juce::File fermataLogFolder() { return juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("Fermata"); }

/** The manual (an A4 PDF, not built into the program): the one chosen before, else a PDF with "manual" in its name next to Fermata.exe or in
    Documents\Fermata. File() if none is found. */
inline juce::File findFermataManual (AppContext& app)
{
    if (auto* st = app.props.getUserSettings())
        if (const juce::File chosen (st->getValue ("manualPdf")); chosen.existsAsFile()) return chosen;
    const auto exeFolder = juce::File::getSpecialLocation (juce::File::currentExecutableFile).getParentDirectory();
    for (auto& folder : { exeFolder, fermataLogFolder() })
        for (auto& f : folder.findChildFiles (juce::File::findFiles, false, "*.pdf"))
            if (f.getFileName().containsIgnoreCase ("manual")) return f;
    return {};
}

inline void openFermataManual (AppContext& app)
{
    if (const auto f = findFermataManual (app); f != juce::File()) { f.startAsProcess(); return; }
    juce::AlertWindow::showAsync (juce::MessageBoxOptions().withIconType (juce::MessageBoxIconType::InfoIcon).withTitle ("Fermata manual")
                                      .withMessage ("Fermata could not find its manual. Put the manual PDF next to Fermata.exe (with \"manual\" in its name), or show Fermata where it is.")
                                      .withButton ("Choose the PDF...").withButton ("Cancel"),
                                  [&app] (int r)
                                  {
                                      if (r != 1) return;
                                      auto chooser = std::make_shared<juce::FileChooser> ("Where is the Fermata manual?", fermataLogFolder(), "*.pdf");
                                      chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [&app, chooser] (const juce::FileChooser& fc)
                                      {
                                          const auto f = fc.getResult();
                                          if (! f.existsAsFile()) return;
                                          if (auto* st = app.props.getUserSettings()) { st->setValue ("manualPdf", f.getFullPathName()); st->saveIfNeeded(); }
                                          f.startAsProcess();
                                      });
                                  });
}

/** Everything someone helping with a problem needs to know about this copy of Fermata, as text (About Fermata, and its Copy button). */
inline juce::String aboutFermataText (AppContext& app)
{
    auto* inst = juce::JUCEApplication::getInstance();
    juce::String t;
    t << "Fermata " << (inst != nullptr ? inst->getApplicationVersion() : juce::String()) << "\n"
      << "Built " << __DATE__ << "\n\n"
      << "Windows: " << juce::SystemStats::getOperatingSystemName() << "\n"
      << "CPU: " << juce::SystemStats::getCpuModel() << "\n";
    if (const auto m = CoreReservation::get().describeMachine(); m.isNotEmpty()) t << "Cores: " << m << "\n";
    t << "Memory: " << juce::String (juce::SystemStats::getMemorySizeInMegabytes() / 1024.0, 0) << " GB\n"
      << "CPU cores for the audio: " << CoreReservation::get().statusText().upToFirstOccurrenceOf ("\n", false, false) << "\n";
    if (auto* dev = app.devices.getCurrentAudioDevice())
        t << "Audio device: " << dev->getTypeName() << " / " << dev->getName() << ", " << juce::String (dev->getCurrentSampleRate(), 0) << " Hz, buffer "
          << dev->getCurrentBufferSizeSamples() << "\n";
    else
        t << "Audio device: none open\n";
    if (app.hasProject()) t << "Project: " << app.project.name << " (" << app.project.projectFolder().getFullPathName() << ")\n";
    t << "Log: " << fermataLogFolder().getChildFile ("fermata-log.txt").getFullPathName();
    return t;
}

/** 3.73.0 (27 November 2026: video could not be added, and why could not be seen): writes fermata-video-report.txt beside the log - what reads
    video in this copy, this Windows, and the report its build made about the video reader (CMakeLists.txt) - and shows it, to be sent. */
inline void saveVideoReport()
{
    auto* inst = juce::JUCEApplication::getInstance();
    juce::String t;
    t << "Fermata " << (inst != nullptr ? inst->getApplicationVersion() : juce::String()) << "  (built " << __DATE__ << ")\n"
      << "Saved: " << juce::Time::getCurrentTime().toString (true, true) << "\n"
      << "Video read by: " << video::systemName() << "\n"
      << "Windows: " << juce::SystemStats::getOperatingSystemName() << "\n\n"
      << video::buildReport() << "\n";
    const auto f = fermataLogFolder().getChildFile ("fermata-video-report.txt");
    f.getParentDirectory().createDirectory();
    if (f.replaceWithText (t)) f.revealToUser();
    else juce::AlertWindow::showAsync (juce::MessageBoxOptions().withIconType (juce::MessageBoxIconType::WarningIcon).withTitle ("Video report")
                                           .withMessage ("The report could not be written to " + f.getFullPathName() + ".").withButton ("OK"), nullptr);
}

/** The Help menu (3.14.0): the manual, the keys, the log, the crash reports (3.73.0: the video report) and About. */
inline void showHelpMenu (AppContext& app, juce::Component& target)
{
    const auto reports = crashreport::allReports();
    juce::PopupMenu m;
    m.addItem (1, "Fermata manual (PDF)");
    m.addItem (2, "HotKeys: every key, and changing them");
    m.addSeparator();
    m.addItem (3, "Show the log file (fermata-log.txt)");
    m.addItem (4, reports.isEmpty() ? juce::String ("Crash reports (none)") : "Show the crash reports (" + juce::String (reports.size()) + ")", ! reports.isEmpty());
    m.addItem (6, "Save the video report (how this copy's video reader was built)");
    m.addSeparator();
    m.addItem (5, "About Fermata");
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&target), [&app] (int r)
    {
        if (r == 1) openFermataManual (app);
        else if (r == 2) { if (app.showHotKeys) app.showHotKeys(); }
        else if (r == 3) fermataLogFolder().getChildFile ("fermata-log.txt").revealToUser();
        else if (r == 4) { const auto all = crashreport::allReports(); if (! all.isEmpty()) all.getFirst().revealToUser(); }
        else if (r == 6) saveVideoReport();
        else if (r == 5)
        {
            const auto text = aboutFermataText (app);
            juce::AlertWindow::showAsync (juce::MessageBoxOptions().withIconType (juce::MessageBoxIconType::InfoIcon).withTitle ("About Fermata")
                                              .withMessage (text).withButton ("Copy").withButton ("OK"),
                                          [text] (int b) { if (b == 1) juce::SystemClipboard::copyTextToClipboard (text); });
        }
    });
}
} // namespace td
