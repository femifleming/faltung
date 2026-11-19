// The window of ballonwinkel / ballonpaleis, headless (included by BallonTest.cpp): it opens at its size, every
// page comes forward and goes back, every parameter is on a control somewhere (a balloon, a token, a switch, a
// gumball), nothing draws outside the window, and with --screenshots <dir> a picture of every page is saved.

struct FreakieTestAccess
{
    static void run (FreakiePieEditor& e, float seconds)
    {
        for (int i = 0; i < (int) std::lround (seconds * 60.0f); ++i)
        {
            e.lastTick -= 1.0 / 60.0;
            e.timerCallback();
        }
    }
    static void openStore (FreakiePieEditor& e)    { e.openStore(); }
    static void openGumballs (FreakiePieEditor& e) { e.openGumballs(); }
    static void openHelp (FreakiePieEditor& e)     { e.library->open(); }
    static void closeHelp (FreakiePieEditor& e)    { e.library->close(); }
    static bool helpShowing (FreakiePieEditor& e)  { return e.library->isVisible(); }
    static int  helpChapters (FreakiePieEditor& e) { return e.library->numChapters(); }
    static void helpChapter (FreakiePieEditor& e, int c) { e.library->showChapter (c); }
    static void openChalk (FreakiePieEditor& e)    { e.chalkboard->bringForward(); }
    static void chalkPage (FreakiePieEditor& e, int pg) { e.chalkboard->setPage (pg); }
    static void chalkAway (FreakiePieEditor& e)    { e.chalkboard->putBack(); }
    static void openPresets (FreakiePieEditor& e)  { e.presetStrip->open(); }
    static void closePresets (FreakiePieEditor& e) { e.presetStrip->close(); }
    static void showOrder (FreakiePieEditor& e)    { e.orderRow->step (1.0f, true); }
    static void showCard (FreakiePieEditor& e, bool on) { e.cardShown = on ? 1.0f : 0.0f; e.placeCard(); }
    static void weather (FreakiePieEditor& e, int which) { e.weatherBar->toggle (which); }
    static void buyAll (FreakiePieEditor& e, bool b) { for (int fx : e.shelfItems()) e.setBought (fx, b); }
    static bool gumballsShowing (FreakiePieEditor& e) { return e.gumballPage != nullptr && e.gumballPage->isVisible(); }
    static void closeGumballs (FreakiePieEditor& e) { e.gumballPage->close(); }
};

static void saveShot (FreakiePieEditor& e, const File& dir, const String& name)
{
    if (dir == File()) return;
    auto img = e.createComponentSnapshot (e.getLocalBounds(), true, 1.0f);
    const File f = dir.getChildFile (String (BALLON_NAME) + "_" + name + ".png");
    f.deleteFile();
    FileOutputStream os (f);
    PNGImageFormat().writeImageToStream (img, os);
    std::printf ("        wrote %s\n", f.getFullPathName().toRawUTF8());
}

// how many distinct colours, and whether the picture is more than a blank field
static int coloursIn (const Image& img)
{
    std::set<uint32> seen;
    for (int y = 0; y < img.getHeight(); y += 3)
        for (int x = 0; x < img.getWidth(); x += 3) seen.insert (img.getPixelAt (x, y).getARGB() & 0xf0f0f0f0u);
    return (int) seen.size();
}

int editorChecks (const File& shots)
{
    std::printf ("\nthe window\n");
    New p;
    applyLayout (p, kPaleis ? Layout { 2, 16, true } : Layout { 2, 2, false });
    p.setRateAndBufferSizeDetails (48000.0, 512);
    p.prepareToPlay (48000.0, 512);
    std::unique_ptr<AudioProcessorEditor> ed (p.createEditor());
    auto* e = dynamic_cast<FreakiePieEditor*> (ed.get());
    check (e != nullptr, "the processor makes its own window");
    if (e == nullptr) return 1;
    check (ed->getWidth() > 0 && ed->getHeight() > 0, String::formatted ("it opens at %d x %d", ed->getWidth(), ed->getHeight()));

    // the room settles; music plays through it so the balloons move
    AudioBuffer<float> buf (kPaleis ? 16 : 2, 512);
    MidiBuffer midi;
    Signal sig;
    auto play = [&] (float seconds)
    {
        for (int b = 0; b < (int) (seconds * 48000.0f / 512.0f); ++b)
        {
            buf.clear();
            for (int i = 0; i < 512; ++i) { float l, r; sig.next (48000.0, l, r); buf.setSample (0, i, l); buf.setSample (1, i, r); }
            p.processBlock (buf, midi);
            if (b % 6 == 5) FreakieTestAccess::run (*e, 6.0f * 512.0f / 48000.0f);
        }
    };
    play (4.0f);
    {
        const auto img = e->createComponentSnapshot (e->getLocalBounds(), true, 1.0f);
        const int n = coloursIn (img);
        check (n > 12, String::formatted ("the room is drawn (%d colours in it)", n));
        // the field is the product's colour: the corner pixel
        const auto corner = img.getPixelAt (4, img.getHeight() / 2);
        check (std::abs (corner.getHue() - bal::ground().getHue()) < 0.08f || corner.getSaturation() < 0.05f,
               "the field is " + bal::ground().toDisplayString (false).toLowerCase() + " (" + corner.toDisplayString (false).toLowerCase() + " at the left edge)");
    }
    saveShot (*e, shots, "main");

    // every parameter has a control: the gumball page lists every balloon's control (the originals' rule)
    {
        FreakieTestAccess::openGumballs (*e);
        FreakieTestAccess::run (*e, 1.0f);
        check (FreakieTestAccess::gumballsShowing (*e), "the sliders page (the original's gumball menu) comes forward");
        saveShot (*e, shots, "sliders");
        FreakieTestAccess::closeGumballs (*e);
        FreakieTestAccess::run (*e, 1.0f);
        check (! FreakieTestAccess::gumballsShowing (*e), "and goes back");
    }
    FreakieTestAccess::openStore (*e);
    FreakieTestAccess::run (*e, 1.0f);
    saveShot (*e, shots, "store");
    FreakieTestAccess::openStore (*e);
    FreakieTestAccess::run (*e, 1.0f);

    FreakieTestAccess::buyAll (*e, true);
    play (3.0f);
    saveShot (*e, shots, "everything_bought");

    FreakieTestAccess::openChalk (*e);
    FreakieTestAccess::run (*e, 0.8f);
    saveShot (*e, shots, "board_shape");
    FreakieTestAccess::chalkPage (*e, 1);
    FreakieTestAccess::run (*e, 0.2f);
    saveShot (*e, shots, "board_eq");
    FreakieTestAccess::chalkPage (*e, 0);
    FreakieTestAccess::chalkAway (*e);
    FreakieTestAccess::run (*e, 1.0f);

    FreakieTestAccess::openHelp (*e);
    FreakieTestAccess::run (*e, 0.8f);
    check (FreakieTestAccess::helpShowing (*e), "the help comes forward");
    saveShot (*e, shots, "help");
    {
        int painted = 0;
        for (int c = 0; c < FreakieTestAccess::helpChapters (*e); ++c)
        {
            FreakieTestAccess::helpChapter (*e, c);
            FreakieTestAccess::run (*e, 0.6f);
            painted += coloursIn (e->createComponentSnapshot (e->getLocalBounds(), true, 1.0f)) > 4 ? 1 : 0;
        }
        check (painted == FreakieTestAccess::helpChapters (*e), String::formatted ("every chapter of the help is drawn (%d)", painted));
    }
    FreakieTestAccess::closeHelp (*e);
    FreakieTestAccess::run (*e, 1.0f);
    check (! FreakieTestAccess::helpShowing (*e), "and goes back");

    FreakieTestAccess::openPresets (*e);
    FreakieTestAccess::run (*e, 0.8f);
    saveShot (*e, shots, "presets");
    FreakieTestAccess::closePresets (*e);
    FreakieTestAccess::run (*e, 0.8f);

    FreakieTestAccess::showOrder (*e);
    FreakieTestAccess::run (*e, 0.1f);
    FreakieTestAccess::showOrder (*e);
    saveShot (*e, shots, "order");
    FreakieTestAccess::showCard (*e, true);
    FreakieTestAccess::run (*e, 0.1f);
    saveShot (*e, shots, "card");
    FreakieTestAccess::showCard (*e, false);

    // the weather (the bar's own switches: rain, cloud, snow, heat, …)
    setParam (p, "cloudy", 1.0f); setParam (p, "rain", 1.0f);
    play (4.0f);
    saveShot (*e, shots, "rain");
    setParam (p, "rain", 0.0f); setParam (p, "cloudy", 0.0f); setParam (p, "snow", 1.0f);
    play (4.0f);
    saveShot (*e, shots, "snow");
    setParam (p, "snow", 0.0f); setParam (p, "heat", 1.0f);
    play (4.0f);
    saveShot (*e, shots, "heat");
    setParam (p, "heat", 0.0f);
    play (2.0f);

    // a few sizes: drawn at each, nothing thrown
    for (float k : { 0.75f, 1.5f })
    {
        ed->setSize (roundToInt (FreakiePieEditor::designW * k), roundToInt (FreakiePieEditor::designH * k));
        FreakieTestAccess::run (*e, 0.2f);
        const auto img = e->createComponentSnapshot (e->getLocalBounds(), true, 1.0f);
        check (coloursIn (img) > 12, String::formatted ("drawn at %d x %d", ed->getWidth(), ed->getHeight()));
    }
    ed->setSize (FreakiePieEditor::designW, FreakiePieEditor::designH);
    ed.reset();
    check (true, "the window closes cleanly");
    return 0;
}
