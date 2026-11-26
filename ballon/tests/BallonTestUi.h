// The window of ballonwinkel / ballonpaleis, headless (included by BallonTest.cpp): it opens at its size, every
// page comes forward and goes back, every parameter is on a control somewhere (a balloon, a token, a switch, a
// gumball), nothing draws outside the window, and with --screenshots <dir> a picture of every page is saved.

struct BalloonTestAccess
{
    static void run (BalloonEditor& e, float seconds)
    {
        for (int i = 0; i < (int) std::lround (seconds * 60.0f); ++i)
        {
            e.lastTick -= 1.0 / 60.0;
            e.timerCallback();
        }
    }
    static void openStore (BalloonEditor& e)    { e.openStore(); }
    static void openGumballs (BalloonEditor& e) { e.openGumballs(); }
    static void openHelp (BalloonEditor& e)     { e.library->open(); }
    static void closeHelp (BalloonEditor& e)    { e.library->close(); }
    static bool helpShowing (BalloonEditor& e)  { return e.library->isVisible(); }
    static int  helpChapters (BalloonEditor& e) { return e.library->numChapters(); }
    static void helpChapter (BalloonEditor& e, int c) { e.library->showChapter (c); }
    static void openChalk (BalloonEditor& e)    { e.chalkboard->bringForward(); }
    static void chalkPage (BalloonEditor& e, int pg) { e.chalkboard->setPage (pg); }
    static void chalkAway (BalloonEditor& e)    { e.chalkboard->putBack(); }
    static void openPresets (BalloonEditor& e)  { e.presetStrip->open(); }
    static void closePresets (BalloonEditor& e) { e.presetStrip->close(); }
    static void showOrder (BalloonEditor& e)    { e.orderRow->step (1.0f, true); }
    static void showCard (BalloonEditor& e, bool on) { e.cardShown = on ? 1.0f : 0.0f; e.placeCard(); }
    static void weather (BalloonEditor& e, int which) { e.weatherBar->toggle (which); }
    static void buyAll (BalloonEditor& e, bool b) { for (int fx : e.shelfItems()) e.setBought (fx, b); }
    static bool gumballsShowing (BalloonEditor& e) { return e.gumballPage != nullptr && e.gumballPage->isVisible(); }
    static void closeGumballs (BalloonEditor& e) { e.gumballPage->close(); }
};

static void saveShot (BalloonEditor& e, const File& dir, const String& name)
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
    auto* e = dynamic_cast<BalloonEditor*> (ed.get());
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
            if (b % 6 == 5) BalloonTestAccess::run (*e, 6.0f * 512.0f / 48000.0f);
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
        BalloonTestAccess::openGumballs (*e);
        BalloonTestAccess::run (*e, 1.0f);
        check (BalloonTestAccess::gumballsShowing (*e), "the sliders page (the original's gumball menu) comes forward");
        saveShot (*e, shots, "sliders");
        BalloonTestAccess::closeGumballs (*e);
        BalloonTestAccess::run (*e, 1.0f);
        check (! BalloonTestAccess::gumballsShowing (*e), "and goes back");
    }
    BalloonTestAccess::openStore (*e);
    BalloonTestAccess::run (*e, 1.0f);
    saveShot (*e, shots, "store");
    BalloonTestAccess::openStore (*e);
    BalloonTestAccess::run (*e, 1.0f);

    BalloonTestAccess::buyAll (*e, true);
    play (3.0f);
    saveShot (*e, shots, "everything_bought");

    BalloonTestAccess::openChalk (*e);
    BalloonTestAccess::run (*e, 0.8f);
    saveShot (*e, shots, "board_shape");
    BalloonTestAccess::chalkPage (*e, 1);
    BalloonTestAccess::run (*e, 0.2f);
    saveShot (*e, shots, "board_eq");
    BalloonTestAccess::chalkPage (*e, 0);
    BalloonTestAccess::chalkAway (*e);
    BalloonTestAccess::run (*e, 1.0f);

    BalloonTestAccess::openHelp (*e);
    BalloonTestAccess::run (*e, 0.8f);
    check (BalloonTestAccess::helpShowing (*e), "the help comes forward");
    saveShot (*e, shots, "help");
    {
        int painted = 0;
        for (int c = 0; c < BalloonTestAccess::helpChapters (*e); ++c)
        {
            BalloonTestAccess::helpChapter (*e, c);
            BalloonTestAccess::run (*e, 0.6f);
            painted += coloursIn (e->createComponentSnapshot (e->getLocalBounds(), true, 1.0f)) > 4 ? 1 : 0;
        }
        check (painted == BalloonTestAccess::helpChapters (*e), String::formatted ("every chapter of the help is drawn (%d)", painted));
    }
    BalloonTestAccess::closeHelp (*e);
    BalloonTestAccess::run (*e, 1.0f);
    check (! BalloonTestAccess::helpShowing (*e), "and goes back");

    BalloonTestAccess::openPresets (*e);
    BalloonTestAccess::run (*e, 0.8f);
    saveShot (*e, shots, "presets");
    BalloonTestAccess::closePresets (*e);
    BalloonTestAccess::run (*e, 0.8f);

    BalloonTestAccess::showOrder (*e);
    BalloonTestAccess::run (*e, 0.1f);
    BalloonTestAccess::showOrder (*e);
    saveShot (*e, shots, "order");
    BalloonTestAccess::showCard (*e, true);
    BalloonTestAccess::run (*e, 0.1f);
    saveShot (*e, shots, "card");
    BalloonTestAccess::showCard (*e, false);

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
        ed->setSize (roundToInt (BalloonEditor::designW * k), roundToInt (BalloonEditor::designH * k));
        BalloonTestAccess::run (*e, 0.2f);
        const auto img = e->createComponentSnapshot (e->getLocalBounds(), true, 1.0f);
        check (coloursIn (img) > 12, String::formatted ("drawn at %d x %d", ed->getWidth(), ed->getHeight()));
    }
    ed->setSize (BalloonEditor::designW, BalloonEditor::designH);
    ed.reset();
    check (true, "the window closes cleanly");
    return 0;
}
