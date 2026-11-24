#pragma once
// ============================================================================
//  Liaison directe Typhon ↔ APC64 : ouvre le port « APC64 » (DAW), passe la machine
//  en mode DAW, lit les pads / boutons / strips et dessine la surface (LED, écran).
//  Une seule instance de Typhon pilote l'APC64 à la fois.
// ============================================================================
#include <JuceHeader.h>
#include "Apc.h"

namespace typhon { namespace apc {

    // Actions demandées par la surface (fil MIDI) et exécutées sur le fil principal
    enum class Act : uint8_t { setStrip, toggleMute, toggleSeqLen, undo, redo, commit };
    struct Action { Act act = Act::commit; int8_t index = 0, lane = 0; float value = 0.0f; };
    struct ActionQueue
    {
        static constexpr int size = 512;
        Action buf[size];
        std::atomic<int> w { 0 }, r { 0 };
        bool push (const Action& a)
        {
            const int wi = w.load (std::memory_order_relaxed), nx = (wi + 1) % size;
            if (nx == r.load (std::memory_order_acquire)) return false;
            buf[wi] = a; w.store (nx, std::memory_order_release); return true;
        }
        bool pop (Action& a)
        {
            const int ri = r.load (std::memory_order_relaxed);
            if (ri == w.load (std::memory_order_acquire)) return false;
            a = buf[ri]; r.store ((ri + 1) % size, std::memory_order_release); return true;
        }
    };

    class Link : private juce::MidiInputCallback
    {
    public:
        Link (Pattern& p, Host& h) : pat (p), host (h) {}
        ~Link() override { close(); }

        Surface surface;

        // appelé ~25 fois par seconde sur le fil principal
        void tick (bool wanted, int playStep)
        {
            if (! wanted) { if (out != nullptr || in != nullptr) close(); state = "Off"; return; }
            const double now = juce::Time::getMillisecondCounterHiRes();
            if (out == nullptr)
            {
                if (now - lastTry < 2000.0) return;
                lastTry = now;
                open();
                return;
            }
            if (! dawOn && (identityOk.load() || now - openedAt > 500.0))
            {
                send (dawMode (true));
                dawOn = true; force = true;
            }
            if (! dawOn) return;
            if (surface.sessionView.load() && ! wasSession) force = true;   // retour en vue Session : tout redessiner
            wasSession = surface.sessionView.load();
            Frame f;
            surface.render (pat, host, playStep, f);
            draw (f);
        }

        juce::String status() const { return state; }
        bool active() const { return dawOn; }

    private:
        static inline std::atomic<Link*> owner { nullptr };

        static int score (const juce::String& n)
        {
            if (! n.containsIgnoreCase ("APC64")) return 0;
            if (n.trim().equalsIgnoreCase ("APC64")) return 5;
            if (n.containsIgnoreCase ("DAW")) return 4;
            if (n.containsIgnoreCase ("Notes") || n.containsIgnoreCase ("Custom") || n.startsWithIgnoreCase ("MIDIIN") || n.startsWithIgnoreCase ("MIDIOUT")) return 0;
            if (n.containsIgnoreCase ("MIDI")) return 1;
            return 2;
        }
        template <typename Arr> static juce::String pick (const Arr& a)
        {
            juce::String best; int bs = 0;
            for (auto& d : a) { const int s = score (d.name); if (s > bs) { bs = s; best = d.identifier; } }
            return best;
        }

        void open()
        {
            Link* expected = nullptr;
            if (owner.load() != this && ! owner.compare_exchange_strong (expected, this))
            { state = juce::String::fromUTF8 ("Une autre instance de Typhon pilote d\xc3\xa9j\xc3\xa0 l'APC64."); return; }
            const auto inId = pick (juce::MidiInput::getAvailableDevices()), outId = pick (juce::MidiOutput::getAvailableDevices());
            if (inId.isEmpty() || outId.isEmpty())
            { state = juce::String::fromUTF8 ("APC64 introuvable : branche-le en USB."); owner.store (nullptr); return; }
            in = juce::MidiInput::openDevice (inId, this);
            out = juce::MidiOutput::openDevice (outId);
            if (in == nullptr || out == nullptr)
            {
                in.reset(); out.reset(); owner.store (nullptr);
                state = juce::String::fromUTF8 ("APC64 occup\xc3\xa9 : dans Ableton, Pr\xc3\xa9" "f\xc3\xa9rences > Link/Tempo/MIDI, mets la surface de contr\xc3\xb4le APC64 sur \xc2\xab Aucun \xc2\xbb.");
                return;
            }
            in->start();
            identityOk.store (false); dawOn = false; force = true; wasSession = true;
            surface.sessionView.store (true);
            openedAt = juce::Time::getMillisecondCounterHiRes();
            for (auto& m : handshake()) send (m);
            state = juce::String::fromUTF8 ("APC64 pilot\xc3\xa9 par Typhon.");
        }

        void close()
        {
            if (out != nullptr && dawOn)
            {
                for (int i = 0; i < 64; ++i) out->sendMessageNow (juce::MidiMessage::noteOn (1, i, (juce::uint8) 0));
                send (dawMode (false));
            }
            if (in != nullptr) in->stop();
            if (logStream != nullptr) logStream->flush();
            in.reset(); out.reset();
            dawOn = false;
            Link* me = this; owner.compare_exchange_strong (me, nullptr);
        }

        void handleIncomingMidiMessage (juce::MidiInput*, const juce::MidiMessage& m) override
        {
            const auto* d = m.getRawData(); const int n = m.getRawDataSize();
            logMessage (d, n);
            if (isIdentityReply (d, n)) { identityOk.store (true); return; }
            surface.handle (d, n, pat, host);
        }

        // journal des messages reçus de l'APC64 (diagnostic) : Documents/Foudre Audio/Typhon/apc64_log.txt
        void logMessage (const uint8_t* d, int n)
        {
            if (logged >= 4000) return;
            if (logStream == nullptr)
            {
                auto f = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("Foudre Audio").getChildFile ("Typhon").getChildFile ("apc64_log.txt");
                f.getParentDirectory().createDirectory();
                f.deleteFile();
                logStream = std::make_unique<juce::FileOutputStream> (f);
                if (! logStream->openedOk()) { logStream.reset(); logged = 4000; return; }
            }
            juce::String line = juce::String (juce::Time::getMillisecondCounter()) + " :";
            for (int i = 0; i < std::min (n, 24); ++i) line << " " << juce::String::toHexString ((int) d[i]).paddedLeft ('0', 2);
            line << "  vue=" << surface.view.load() << " piste=" << surface.selLane.load() << " strips=" << surface.touchedStrips.load() << "\n";
            logStream->writeText (line, false, false, nullptr);
            if (++logged % 20 == 0) logStream->flush();
        }
        std::unique_ptr<juce::FileOutputStream> logStream;
        int logged = 0;

        void send (const std::vector<uint8_t>& b) { if (out != nullptr) out->sendMessageNow (juce::MidiMessage (b.data(), (int) b.size())); }
        void raw (uint8_t a, uint8_t b, uint8_t c) { if (out != nullptr) out->sendMessageNow (juce::MidiMessage (a, b, c)); }

        void draw (const Frame& f)
        {
            const uint64_t dirty = surface.dirtyPads.exchange (0);
            for (int i = 0; i < 64; ++i)
                if (force || ((dirty >> i) & 1) || f.padB[i] != sent.padB[i] || f.padC[i] != sent.padC[i])
                    raw ((uint8_t) (0x90 | (f.padC[i] == 0 ? 0 : f.padB[i])), (uint8_t) i, f.padC[i]);
            for (int l = 0; l < 8; ++l)
                if (force || f.trackC[l] != sent.trackC[l]) raw ((uint8_t) (0x90 | bFull), (uint8_t) (nTrack0 + l), f.trackC[l]);
            for (int nb = 64; nb < 128; ++nb)
            {
                if (nb >= nTrack0 && nb < nTrack0 + 8) continue;
                if (! force && f.btn[nb] == sent.btn[nb]) continue;
                if (f.btn[nb] == 0) raw (0x90, (uint8_t) nb, 0);
                else raw ((uint8_t) (0x90 | (f.btn[nb] - 1)), (uint8_t) nb, 1);
            }
            for (int i = 0; i < 8; ++i)
            {
                if (force) raw (0xB0, (uint8_t) (0x68 + i), 1);   // barre blanche
                if (force || f.stripC[i] != sent.stripC[i]) raw (0xB0, (uint8_t) (0x70 + i), f.stripC[i]);
                if (force || std::abs ((int) f.stripPos[i] - (int) sent.stripPos[i]) > 96)
                    raw ((uint8_t) (0xE0 | i), (uint8_t) (f.stripPos[i] & 0x7F), (uint8_t) (f.stripPos[i] >> 7));
                else const_cast<Frame&> (f).stripPos[i] = sent.stripPos[i];
            }
            if (force || f.oled0 != sent.oled0) send (oled (0, f.oled0));
            if (force || f.oled1 != sent.oled1) send (oled (1, f.oled1));
            sent = f; force = false;
        }

        Pattern& pat;
        Host& host;
        std::unique_ptr<juce::MidiInput> in;
        std::unique_ptr<juce::MidiOutput> out;
        std::atomic<bool> identityOk { false };
        bool dawOn = false, force = true, wasSession = true;
        double lastTry = -1.0e9, openedAt = 0.0;
        Frame sent;
        juce::String state { "Off" };
    };
}}
