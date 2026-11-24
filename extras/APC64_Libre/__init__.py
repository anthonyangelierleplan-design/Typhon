# APC64 Libre — Foudre Audio
# Le script officiel de l'APC64 (Ableton Live 12), avec les touch strips libres pour le MIDI Map
# quand le bouton OFF des strips est allumé — comme le mode User de l'APC40.
# Tout le reste (Session, Note, Drum, Device, Volume, Pan, Sends...) est celui d'Ableton, inchangé.
from __future__ import absolute_import, print_function, unicode_literals
from ableton.v3.base import listens, task
from ableton.v3.control_surface.capabilities import CONTROLLER_ID_KEY, NOTES_CC, PORTS_KEY, REMOTE, SCRIPT, SYNC, controller_id, inport, outport
from APC64 import APC64 as _OfficialAPC64

STRIP_COUNT = 8
LED_STYLE_BAR = 1      # barre depuis le bas
LED_COLOUR_WHITE = 3   # palette Ableton


def get_capabilities():
    # Comme le script officiel, mais les ports ne sont plus cachés : on peut y activer « Télécommande »
    return {CONTROLLER_ID_KEY: controller_id(vendor_id=2536, product_ids=[83], model_name=["APC64"]),
            PORTS_KEY: [inport(props=[NOTES_CC, SCRIPT, REMOTE]),
                        outport(props=[NOTES_CC, SCRIPT, REMOTE]),
                        outport(props=[SYNC])]}


def create_instance(c_instance):
    return APC64Libre(c_instance=c_instance)


class APC64Libre(_OfficialAPC64):

    def setup(self):
        super().setup()
        modes = self.component_map["Touch_Strip_Modes"]
        self._on_strip_mode_changed.subject = modes
        self._on_strip_mode_changed(modes.selected_mode)

    @listens("selected_mode")
    def _on_strip_mode_changed(self, selected_mode):
        if selected_mode == "off":
            # le script n'utilise plus les strips : Live peut les assigner (Ctrl+M).
            # On rallume juste leurs LED pour voir la position renvoyée par Live.
            self._tasks.add(task.sequence(task.wait(0.15), task.run(self._light_free_strips)))
            self.show_message("APC64 Libre : touch strips libres, assigne-les avec Ctrl+M")

    def _light_free_strips(self):
        if self.component_map["Touch_Strip_Modes"].selected_mode != "off":
            return
        for i in range(STRIP_COUNT):
            self._send_midi((0xB0, 104 + i, LED_STYLE_BAR))
            self._send_midi((0xB0, 112 + i, LED_COLOUR_WHITE))
