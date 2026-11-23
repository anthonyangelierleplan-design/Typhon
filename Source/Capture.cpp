#include "Capture.h"
#include "PluginProcessor.h"
#include "Shared.h"

using namespace typhon;

namespace
{
    struct EzWindow : public juce::DocumentWindow
    {
        EzWindow() : DocumentWindow ("EZdrummer (dans Typhon)", juce::Colours::black, DocumentWindow::closeButton)
        {
            setUsingNativeTitleBar (true);
            setResizable (false, false);
        }
        void closeButtonPressed() override { setVisible (false); }
    };

    juce::String safeName (const juce::String& n)
    {
        auto s = juce::File::createLegalFileName (n.trim());
        return s.isEmpty() ? juce::String ("Kit") : s;
    }
}

CaptureEngine::CaptureEngine (TyphonProcessor& p) : juce::Thread ("Typhon capture"), proc (p)
{
    formats.addDefaultFormats();
    for (auto& e : laneEnabled) e.store (true);
}

CaptureEngine::~CaptureEngine()
{
    alive->store (false);
    signalThreadShouldExit();
    waitForThreadToExit (5000);
    window.reset();   // l'éditeur d'EZdrummer avant l'instance
    ezd.reset();
}

juce::String CaptureEngine::pluginName() const
{
    return ezd != nullptr ? ezd->getName() : juce::String ("EZdrummer");
}

juce::File CaptureEngine::findPluginFile()
{
    juce::Array<juce::File> roots;
   #if JUCE_WINDOWS
    roots.add (juce::File ("C:\\Program Files\\Common Files\\VST3"));
    roots.add (juce::File::getSpecialLocation (juce::File::windowsLocalAppData).getChildFile ("Programs").getChildFile ("Common").getChildFile ("VST3"));
   #elif JUCE_MAC
    roots.add (juce::File ("/Library/Audio/Plug-Ins/VST3"));
    roots.add (juce::File ("~/Library/Audio/Plug-Ins/VST3"));
   #endif
    for (auto& root : roots)
    {
        if (! root.isDirectory()) continue;
        for (const auto& entry : juce::RangedDirectoryIterator (root, true, "*.vst3", juce::File::findFilesAndDirectories))
        {
            const auto f = entry.getFile();
            if (f.getFileName().containsIgnoreCase ("EZdrummer") && ! f.getParentDirectory().getFileName().endsWithIgnoreCase (".vst3"))
                return f;
        }
    }
    return {};
}

bool CaptureEngine::load (juce::String& error)
{
    if (ezd != nullptr) return true;
    const auto file = findPluginFile();
    if (! file.exists()) { error = "EZdrummer introuvable dans le dossier VST3."; setMessage (error); return false; }

    juce::OwnedArray<juce::PluginDescription> types;
    for (auto* fmt : formats.getFormats())
        if (fmt->getName() == "VST3") fmt->findAllTypesForFile (types, file.getFullPathName());
    if (types.isEmpty()) { error = "Impossible de lire " + file.getFileName(); setMessage (error); return false; }

    juce::String err;
    auto inst = formats.createPluginInstance (*types[0], 48000.0, 512, err);
    if (inst == nullptr) { error = juce::String::fromUTF8 ("Chargement refus\xc3\xa9 : ") + err; setMessage (error); return false; }
    inst->disableNonMainBuses();
    inst->prepareToPlay (48000.0, 512);
    {
        const juce::ScopedLock sl (pluginLock);
        ezd = std::move (inst);
    }
    setMessage (pluginName() + juce::String::fromUTF8 (" charg\xc3\xa9. Choisis ton kit et ton mix, puis capture."));
    return true;
}

void CaptureEngine::showEditor()
{
    if (ezd == nullptr) return;
    if (window == nullptr)
    {
        auto* ed = ezd->createEditorIfNeeded();
        if (ed == nullptr) return;
        auto w = std::make_unique<EzWindow>();
        w->setContentOwned (ed, true);
        w->centreWithSize (w->getWidth(), w->getHeight());
        window = std::move (w);
    }
    window->setVisible (true);
    window->toFront (true);
}

void CaptureEngine::captureNow (const juce::String& kitName)
{
    if (ezd == nullptr || isThreadRunning()) return;
    singleName = kitName;
    batchMode = false;
    startThread();
}

void CaptureEngine::addCurrentToQueue (const juce::String& kitName)
{
    if (ezd == nullptr) return;
    Item it;
    it.name = kitName.trim().isEmpty() ? juce::String ("Kit ") + juce::String ((int) items.size() + 1) : kitName.trim();
    {
        const juce::ScopedLock sl (pluginLock);
        ezd->getStateInformation (it.state);
    }
    const juce::ScopedLock ql (queueLock);
    items.push_back (std::move (it));
}

void CaptureEngine::removeFromQueue (int index)
{
    const juce::ScopedLock ql (queueLock);
    if (index >= 0 && index < (int) items.size() && items[(size_t) index].status != 1)
        items.erase (items.begin() + index);
}

void CaptureEngine::clearDone()
{
    const juce::ScopedLock ql (queueLock);
    items.erase (std::remove_if (items.begin(), items.end(), [] (const Item& i) { return i.status == 2; }), items.end());
}

void CaptureEngine::startBatch()
{
    if (ezd == nullptr || isThreadRunning()) return;
    batchMode = true;
    startThread();
}

void CaptureEngine::stop()
{
    signalThreadShouldExit();
}

juce::Array<CaptureEngine::QueueView> CaptureEngine::queue() const
{
    juce::Array<QueueView> a;
    const juce::ScopedLock ql (queueLock);
    for (auto& i : items) a.add ({ i.name, i.status });
    return a;
}

double CaptureEngine::estimateSecondsPerKit() const
{
    int lanesOn = 0; for (auto& e : laneEnabled) if (e.load()) ++lanesOn;
    // le rendu hors temps réel va environ 3 fois plus vite que le temps réel
    return lanesOn * velLayers.load() * roundRobin.load() * (maxSeconds.load() + 0.3) / 3.0 + (batchMode ? loadWaitSeconds.load() : 0.0);
}

// ----------------------------------------------------------------------------
bool CaptureEngine::renderHit (int note, int vel, double sr, int maxLen, juce::AudioBuffer<float>& out)
{
    const juce::ScopedLock sl (pluginLock);
    if (ezd == nullptr) return false;
    const int block = 512;
    const int nch = std::max (2, std::max (ezd->getTotalNumInputChannels(), ezd->getTotalNumOutputChannels()));
    juce::AudioBuffer<float> buf (nch, block);
    juce::MidiBuffer mb;

    // silence pour laisser mourir la queue du coup précédent
    for (int i = 0; i < (int) (0.3 * sr) / block; ++i) { buf.clear(); mb.clear(); ezd->processBlock (buf, mb); }

    out.setSize (2, maxLen, false, true, false);
    out.clear();
    const int offAt = (int) (0.12 * sr);
    for (int pos = 0; pos < maxLen; pos += block)
    {
        if (threadShouldExit()) return false;
        buf.clear(); mb.clear();
        if (pos == 0) mb.addEvent (juce::MidiMessage::noteOn (1, note, (juce::uint8) juce::jlimit (1, 127, vel)), 0);
        if (offAt >= pos && offAt < pos + block) mb.addEvent (juce::MidiMessage::noteOff (1, note), offAt - pos);
        ezd->processBlock (buf, mb);
        const int n = std::min (block, maxLen - pos);
        out.copyFrom (0, pos, buf, 0, 0, n);
        out.copyFrom (1, pos, buf, std::min (1, buf.getNumChannels() - 1), 0, n);
    }

    // découpe : début juste avant l'attaque, fin quand le son tombe sous -70 dB
    const float onThr = 0.001f, offThr = 0.0003f;
    int first = -1, last = -1;
    for (int i = 0; i < maxLen; ++i)
    {
        const float a = std::max (std::fabs (out.getSample (0, i)), std::fabs (out.getSample (1, i)));
        if (first < 0 && a > onThr) first = i;
        if (a > offThr) last = i;
    }
    if (first < 0 || last <= first) return false;
    const int s0 = std::max (0, first - (int) (0.0005 * sr));
    const int s1 = std::min (maxLen, last + (int) (0.005 * sr));
    juce::AudioBuffer<float> t (2, s1 - s0);
    for (int c = 0; c < 2; ++c) t.copyFrom (c, 0, out, c, s0, s1 - s0);
    const int fade = std::min (t.getNumSamples(), (int) (0.005 * sr));
    t.applyGainRamp (t.getNumSamples() - fade, fade, 1.0f, 0.0f);
    out = std::move (t);
    return true;
}

bool CaptureEngine::captureKit (const juce::String& name, juce::File& outFolder)
{
    const bool hi = proc.settings.captureRate.load() == 1;
    const double sr = hi ? 96000.0 : 48000.0;
    const int bits = hi ? 32 : 24;
    const int maxLen = (int) (sr * juce::jlimit (0.2f, 8.0f, maxSeconds.load()));
    const int L = juce::jlimit (1, 8, velLayers.load());
    const int RR = juce::jlimit (1, 4, roundRobin.load());

    {
        const juce::ScopedLock sl (pluginLock);
        if (ezd == nullptr) return false;
        ezd->setNonRealtime (true);
        ezd->releaseResources();
        ezd->setRateAndBufferSizeDetails (sr, 512);
        ezd->prepareToPlay (sr, 512);
    }

    outFolder = TyphonProcessor::libraryFolder().getChildFile (safeName (name));
    outFolder.deleteRecursively();
    outFolder.createDirectory();

    juce::WavAudioFormat wav;
    juce::XmlElement kit ("TYPHON_KIT");
    kit.setAttribute ("name", name);
    kit.setAttribute ("source", pluginName());
    kit.setAttribute ("rate", sr);

    bool any = false;
    for (int lane = 0; lane < numLanes && ! threadShouldExit(); ++lane)
    {
        if (! laneEnabled[lane].load()) continue;
        auto* laneX = kit.createNewChildElement ("LANE");
        laneX->setAttribute ("index", lane);
        laneX->setAttribute ("note", proc.captureNote (lane));
        const float velScale = lane == 2 ? 0.5f : 1.0f;   // ghost : coups doux
        for (int k = 0; k < L && ! threadShouldExit(); ++k)
        {
            const int top = (int) std::round (127.0 * (k + 1) / L);
            auto* layX = laneX->createNewChildElement ("LAYER");
            layX->setAttribute ("top", top);
            for (int r = 0; r < RR && ! threadShouldExit(); ++r)
            {
                setMessage ("Capture " + juce::String (done.load() + 1) + "/" + juce::String (total.load()) + juce::String::fromUTF8 (" \xc2\xb7 ")
                            + laneName (lane) + juce::String::fromUTF8 (" \xc2\xb7 v\xc3\xa9l ") + juce::String (top));
                juce::AudioBuffer<float> hit;
                const int vel = std::max (1, (int) std::round (top * velScale));
                if (renderHit (proc.captureNote (lane), vel, sr, maxLen, hit))
                {
                    const auto fname = "lane" + juce::String (lane) + "_v" + juce::String (k) + "_rr" + juce::String (r) + ".wav";
                    auto f = outFolder.getChildFile (fname);
                    std::unique_ptr<juce::OutputStream> os (f.createOutputStream().release());
                    if (os != nullptr)
                    {
                        std::unique_ptr<juce::AudioFormatWriter> w (wav.createWriterFor (os.get(), sr, 2, bits, {}, 0));
                        if (w != nullptr)
                        {
                            os.release();   // le writer possède maintenant le flux
                            w->writeFromAudioSampleBuffer (hit, 0, hit.getNumSamples());
                            layX->createNewChildElement ("FILE")->setAttribute ("name", fname);
                            any = true;
                        }
                    }
                }
                done.fetch_add (1);
            }
        }
    }
    {
        const juce::ScopedLock sl (pluginLock);
        if (ezd != nullptr) { ezd->setNonRealtime (false); }
    }
    if (! any) { outFolder.deleteRecursively(); return false; }
    kit.writeTo (outFolder.getChildFile ("kit.xml"));
    return ! threadShouldExit();
}

void CaptureEngine::run()
{
    int lanesOn = 0; for (auto& e : laneEnabled) if (e.load()) ++lanesOn;
    const int perKit = lanesOn * juce::jlimit (1, 8, velLayers.load()) * juce::jlimit (1, 4, roundRobin.load());
    done.store (0);

    if (! batchMode)
    {
        total.store (perKit);
        juce::File folder;
        const auto name = singleName.trim().isEmpty() ? juce::String ("Mon kit") : singleName.trim();
        if (captureKit (name, folder))
        {
            setMessage (juce::String::fromUTF8 ("Kit \xc2\xab ") + name + juce::String::fromUTF8 (" \xc2\xbb captur\xc3\xa9 et charg\xc3\xa9."));
            juce::MessageManager::callAsync ([this, folder, a = alive] { if (a->load()) proc.loadKitFolder (folder); });
        }
        else setMessage (threadShouldExit() ? juce::String (juce::String::fromUTF8 ("Capture arr\xc3\xaat\xc3\xa9" "e."))
                                            : juce::String (juce::String::fromUTF8 ("Rien captur\xc3\xa9 : v\xc3\xa9rifie le kit et les notes dans EZdrummer.")));
        return;
    }

    int pending = 0;
    { const juce::ScopedLock ql (queueLock); for (auto& i : items) if (i.status == 0 || i.status == 3) ++pending; }
    total.store (pending * perKit);

    for (;;)
    {
        if (threadShouldExit()) break;
        int idx = -1; juce::String name; juce::MemoryBlock state;
        {
            const juce::ScopedLock ql (queueLock);
            for (size_t i = 0; i < items.size(); ++i)
                if (items[i].status == 0 || items[i].status == 3) { idx = (int) i; items[i].status = 1; name = items[i].name; state = items[i].state; break; }
        }
        if (idx < 0) break;

        // recharge l'état d'EZdrummer sur le fil principal, puis attend que le kit soit chargé
        setMessage (juce::String::fromUTF8 ("Chargement de \xc2\xab ") + name + juce::String::fromUTF8 (" \xc2\xbb dans EZdrummer..."));
        auto ev = std::make_shared<juce::WaitableEvent>();
        juce::MessageManager::callAsync ([this, ev, state, a = alive] {
            if (a->load()) { const juce::ScopedLock sl (pluginLock); if (ezd != nullptr) ezd->setStateInformation (state.getData(), (int) state.getSize()); }
            ev->signal();
        });
        for (int t = 0; t < 200 && ! threadShouldExit(); ++t) if (ev->wait (100)) break;
        for (int t = 0; t < (int) (loadWaitSeconds.load() * 10.0f) && ! threadShouldExit(); ++t) wait (100);
        // vérifie qu'EZdrummer joue (kit chargé), sinon attend encore un peu
        for (int tries = 0; tries < 6 && ! threadShouldExit(); ++tries)
        {
            juce::AudioBuffer<float> probe;
            if (renderHit (36, 100, 48000.0, 24000, probe) || renderHit (38, 100, 48000.0, 24000, probe)) break;
            setMessage (juce::String::fromUTF8 ("EZdrummer charge encore \xc2\xab ") + name + juce::String::fromUTF8 (" \xc2\xbb..."));
            for (int t = 0; t < 50 && ! threadShouldExit(); ++t) wait (100);
        }

        juce::File folder;
        const bool ok = captureKit (name, folder);
        {
            const juce::ScopedLock ql (queueLock);
            for (auto& i : items) if (i.name == name && i.status == 1) { i.status = ok ? 2 : (threadShouldExit() ? 0 : 3); break; }
        }
    }
    setMessage (threadShouldExit() ? juce::String (juce::String::fromUTF8 ("Capture par lot arr\xc3\xaat\xc3\xa9" "e (reprise possible)."))
                                   : juce::String (juce::String::fromUTF8 ("Capture par lot termin\xc3\xa9" "e.")));
}
