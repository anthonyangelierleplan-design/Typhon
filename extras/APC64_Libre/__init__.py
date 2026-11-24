# APC64 Libre — Foudre Audio
# Le script officiel de l'APC64 (Ableton Live 12) + un mode USER sur le bouton OFF des touch strips,
# comme le mode User de l'APC40 : en OFF, chaque strip pilote le réglage que tu lui as assigné ;
# en Device / Volume / Pan / Sends / Channel, les strips retrouvent leur rôle normal.
#
# Assigner : clique un réglage dans Ableton (il s'encadre), puis maintiens SHIFT et touche le strip.
# Désassigner : maintiens CLEAR et touche le strip.
# Les assignations sont enregistrées avec le projet Live.
from __future__ import absolute_import, print_function, unicode_literals
import Live
from ableton.v3.base import listens, listens_group, task
from ableton.v3.live import liveobj_valid
from APC64 import APC64 as _OfficialAPC64, get_capabilities as _official_capabilities

STRIPS = 8
DATA_KEY = "apc64_libre_strips"


def get_capabilities():
    return _official_capabilities()


def create_instance(c_instance):
    return APC64Libre(c_instance=c_instance)


# --- retrouver un réglage d'un projet à l'autre : chemin depuis la piste ---------------------
def _track_ref(song, track):
    if track == song.master_track:
        return ["master", 0]
    for i, t in enumerate(song.tracks):
        if t == track:
            return ["track", i]
    for i, t in enumerate(song.return_tracks):
        if t == track:
            return ["return", i]
    return None


def _track_from_ref(song, ref):
    kind, i = ref
    if kind == "master":
        return song.master_track
    tracks = song.tracks if kind == "track" else song.return_tracks
    return tracks[i] if 0 <= i < len(tracks) else None


def _index_of(seq, obj):
    for i, x in enumerate(seq):
        if x == obj:
            return i
    return -1


def parameter_path(song, parameter):
    """['track', 3, 'dev', 0, 'chain', 1, 'dev', 2, 'p', 5] ou ['track', 3, 'mix', 'volume']"""
    owner = parameter.canonical_parent
    if isinstance(owner, Live.MixerDevice.MixerDevice):
        track = owner.canonical_parent
        ref = _track_ref(song, track)
        if ref is None:
            return None
        if parameter == owner.volume:
            return ref + ["mix", "volume"]
        if parameter == owner.panning:
            return ref + ["mix", "pan"]
        k = _index_of(owner.sends, parameter)
        return ref + ["mix", "send", k] if k >= 0 else None
    if not isinstance(owner, Live.Device.Device):
        return None
    tail = ["p", _index_of(owner.parameters, parameter)]
    node = owner
    while True:
        parent = node.canonical_parent
        if isinstance(parent, Live.Track.Track):
            ref = _track_ref(song, parent)
            if ref is None:
                return None
            return ref + ["dev", _index_of(parent.devices, node)] + tail
        if isinstance(parent, Live.Chain.Chain):
            rack = parent.canonical_parent
            tail = ["chain", _index_of(rack.chains, parent), "dev", _index_of(parent.devices, node)] + tail
            node = rack
            continue
        return None


def parameter_from_path(song, path):
    try:
        track = _track_from_ref(song, path[:2])
        if track is None:
            return None
        rest = path[2:]
        if rest[0] == "mix":
            mixer = track.mixer_device
            if rest[1] == "volume":
                return mixer.volume
            if rest[1] == "pan":
                return mixer.panning
            sends = mixer.sends
            return sends[rest[2]] if rest[2] < len(sends) else None
        container = track
        device = None
        while rest:
            if rest[0] == "dev":
                device = container.devices[rest[1]]
                rest = rest[2:]
            elif rest[0] == "chain":
                container = device.chains[rest[1]]
                rest = rest[2:]
            elif rest[0] == "p":
                return device.parameters[rest[1]]
            else:
                return None
    except Exception:
        return None
    return None


class APC64Libre(_OfficialAPC64):

    def setup(self):
        super().setup()
        self._user_paths = [None] * STRIPS
        self._shift_held = False
        self._clear_held = False
        self._load_assignments()
        modes = self.component_map["Touch_Strip_Modes"]
        self._on_strip_mode_changed.subject = modes
        self._on_shift.subject = self.elements.shift_button
        self._on_clear.subject = self.elements.clear_button
        self._on_strip_touched.replace_subjects(self.elements.touch_elements_raw)
        self._on_strip_mode_changed(modes.selected_mode)

    # --- état des modes ---------------------------------------------------------------------
    def _user_mode(self):
        return self.component_map["Touch_Strip_Modes"].selected_mode == "off"

    @listens("selected_mode")
    def _on_strip_mode_changed(self, selected_mode):
        if selected_mode == "off":
            # après que le mode précédent a rendu les strips
            self._tasks.add(task.sequence(task.wait(0.05), task.run(self._connect_user_strips)))

    def _connect_user_strips(self):
        if not self._user_mode():
            return
        song = self.song
        names = []
        for i, strip in enumerate(self.elements.touch_strips_raw):
            path = self._user_paths[i]
            parameter = parameter_from_path(song, path) if path else None
            if liveobj_valid(parameter):
                strip.connect_to(parameter)
                names.append(parameter.name)
            else:
                strip.release_parameter()
                strip.send_midi((0xB0, 104 + i, 0))   # LED éteinte : strip libre
        assigned = sum(1 for p in self._user_paths if p)
        self.show_message("APC64 Libre : mode USER ({} strip(s) assigné(s)) — SHIFT + strip = assigner le réglage sélectionné".format(assigned))

    # --- assigner / désassigner -------------------------------------------------------------------
    @listens("value")
    def _on_shift(self, value):
        self._shift_held = value != 0

    @listens("value")
    def _on_clear(self, value):
        self._clear_held = value != 0

    @listens_group("value")
    def _on_strip_touched(self, value, element):
        if value == 0 or not self._user_mode():
            return
        if not (self._shift_held or self._clear_held):
            return
        i = _index_of(self.elements.touch_elements_raw, element)
        if i < 0:
            return
        if self._clear_held:
            self._user_paths[i] = None
            self.show_message("APC64 Libre : strip {} libéré".format(i + 1))
        else:
            parameter = self.song.view.selected_parameter
            path = parameter_path(self.song, parameter) if liveobj_valid(parameter) else None
            if path is None:
                self.show_message("APC64 Libre : clique d'abord un réglage dans Ableton, puis SHIFT + strip")
                return
            self._user_paths[i] = path
            self.show_message("APC64 Libre : strip {} = {}".format(i + 1, parameter.name))
        self._save_assignments()
        self._connect_user_strips()

    # --- sauvegarde dans le projet Live -----------------------------------------------------------
    def _save_assignments(self):
        try:
            self.song.set_data(DATA_KEY, list(self._user_paths))
        except Exception:
            pass

    def _load_assignments(self):
        try:
            data = self.song.get_data(DATA_KEY, None)
            if data and len(data) == STRIPS:
                self._user_paths = [list(p) if p else None for p in data]
        except Exception:
            pass
