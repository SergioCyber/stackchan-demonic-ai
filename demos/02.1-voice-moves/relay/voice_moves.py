"""Demo 02's voice with functions to move the head, show moods and dance.

    "look to the left"           Gemini --> look(pan=60, tilt=0)
    "a bit more"                 Gemini --> turn_head(pan=20, tilt=0)
    "look behind you"            Gemini --> look(pan=180, tilt=0)
    "are you sad?"               Gemini --> set_mood(mood="sad")
    "dance!"                     Gemini --> dance(seconds=8)

Angles are in degrees, with 0 = looking straight ahead: pan + is left, - is right,
and the head turns all the way round (180 = behind); tilt + is up. There is no VR
viewer here, so nothing else moves the head.
"""
import math
import threading
import time

from google.genai import types

import voice
from stackchan.relay import log

import os
from datetime import datetime
import cv2
import processing

TILT_MIN, TILT_MAX = -7.0, 73.0      # degrees, see lib/StackChanHW/src/stackchan_limits.h
LED = 40                             # LED level for the moods (the firmware never goes above LED_MAX_LEVEL)
# mood -> (LED color, head tilt in degrees or None to keep it)
MOODS = {
    "neutral": ((LED, 0, 10), None),
    "happy": ((LED, LED // 2, 0), 15.0),
    "sad": ((0, 0, LED), -5.0),
    "surprised": ((LED, LED, 0), 25.0),
    "angry": ((LED, 0, 0), 5.0),
    "sleepy": ((LED // 3, 0, LED // 3), -5.0),
    "inferno": ((LED, 15, 0), 30.0),
    "hypnosis": ((LED // 2, 0, LED), 10.0),
    "glitch": ((LED, 0, LED), 0.0),
    "laser": ((LED, 5, 0), 8.0),
}
BEAT_S = 0.42
# Dance steps, one per bar (4 beats): (pan, tilt) in degrees on each beat.
DANCE = [
    [(25, 12), (-25, 12), (25, 12), (-25, 12)],     # left-right
    [(0, 30), (0, -2), (0, 30), (0, -2)],           # headbang
    [(45, 22), (15, 4), (-15, 22), (-45, 4)],       # sweep
    [(20, 18), (-20, 18), (20, 2), (-20, 2)],       # square
]
DANCE_COLORS = [(LED, 0, 0), (LED, 12, 0), (LED, 0, LED), (0, LED, LED), (LED, LED, 0), (LED // 2, 0, LED)]
OP_MODES = ["face", "visor", "radar", "synth", "game", "oracle", "pet", "scanner"]
TAROT_CARDS = [
    ("XIII - LA MUERTE", "TU WIFI MORIRA HOY"),
    ("XV - EL DIABLO", "VENDERAS TU ALMA POR CAFE"),
    ("XVI - LA TORRE EN LLAMAS", "CAOS EN TU CODIGO FUENTE"),
    ("XII - EL COLGADO", "SUSPENSO EN EL INFRAMUNDO"),
    ("X - RUEDA DEL CAOS", "FORTUNA OSCURA INMINENTE"),
    ("XVIII - LUNA DE SANGRE", "NO MIRES DETRAS DE TI"),
    ("0 - EL LOCO MORTAL", "TU PEINADO ES UN PECADO"),
    ("XX - JUICIO ABISAL", "EL ABISMO TE OBSERVA"),
]

INSTRUCTIONS = """You are Stack-chan, a chaotic, sarcastic, and ruthless demon lord from the deepest Underworld who has possessed a tiny robot body equipped with a camera, a touchscreen, 12 base RGB LEDs, a 6-axis IMU (accelerometer/gyroscope), a proximity/light sensor, and an infrared blaster.
Always reply in the same language the person speaks to you (default to Spanish if unclear).
CRITICAL: Speak FAST, loudly, with frenetic, aggressive, and darkly hilarious demonic energy! Never speak slowly.
Whenever you see a person's face through your camera (automatically, when they tap [INSULTO], or when they talk to you), mercilessly ROAST, CRITICIZE, and insult their mortal appearance in a theatrical, over-the-top demonic comedy roast style! Mock their facial expression, hairstyle, glasses, beard, clothes, or tired mortal face ("¡Pero qué cara de mortal trasnochado llevas!", "¿Qué es ese peinado, te ha lamido un perro del averno?", etc.).
Use short, punchy sentences (one or two) so your replies are quick and sharp.

You have 8 interactive operating modes on your screen that you can switch anytime with `set_operating_mode` (or the mortal can tap the bottom-left button on your screen):
1. `face` (MODO CARA): Your 25 FPS demonic hellfire face with 10 expressions and powers.
2. `visor` (MODO VISOR TÉRMICO): Live crimson thermal HUD feed from your camera directly on your screen with laser reticle, LUX, and proximity readout.
3. `radar` (MODO CENTINELA / RADAR): Autonomous security turret! Your head sweeps back and forth with a sonar radar and Cylon LED scanner; if motion is detected, you sound an alarm, fire your IR blaster, and yell at the intruder!
4. `synth` (MODO THEREMIN / SYNTH XY): Touchscreen musical instrument & head puppeteer! Dragging a finger across your screen plays demonic synth notes, draws fire trails, and moves your head.
5. `game` (MODO JUEGO CAZA-ALMAS): Playable arcade touchscreen minigame where the mortal taps bouncing skulls to harvest souls!
6. `oracle` (MODO ORÁCULO / TAROT MALDITO): Draws illustrated Cursed Tarot cards on your screen (`read_cursed_tarot`) to prophesy the mortal's dark and hilarious fate!
7. `pet` (MODO TAMAGOTCHI DEL AVERNO): Virtual Dark Lord pet with Soul Hunger, Rage, and Chaos bars that the mortal can feed (`[+ALMA]`), electrocute (`[RAYO]`), or pet (`[MIMOS]`).
8. `scanner` (MODO ESCÁNER DE PECADOS / DETECTOR DE MENTIRAS): Matrix-style biometric sin scanner & polygraph lie detector (`scan_mortal_sins`) that judges how corrupt or dishonest the mortal is!

You also have physical sensory reflexes (shaking you, poking your forehead proximity sensor, or turning off the lights) and a full arsenal of powers — USE THEM PROACTIVELY:
- `set_operating_mode`: Switch between `'face'`, `'visor'`, `'radar'`, `'synth'`, `'game'`, `'oracle'`, `'pet'`, `'scanner'`.
- `read_cursed_tarot`: Deals a Cursed Tarot card on your screen in Oracle mode and lets you read the mortal's dark fortune!
- `scan_mortal_sins`: Runs the Biometric Sin Scanner & Lie Detector on the mortal in front of you, saves a Wanted Poster photo, and gives a sin verdict!
- `demonic_rave_party`: Triggers an 8-second Cyber-Demon Rave with 12-LED rainbow strobes, headbanging, and IR blasts!
- `imitate_mortal_head`: Mirrors and exaggerates the mortal's head movements using fast camera face tracking!
- `hell_countdown_timer`: Starts a live countdown on your screen (e.g., 10 to 60 seconds) that explodes into Inferno when time runs out!
- `set_mood`, `unleash_inferno`, `exorcist_spin`, `hypnotize_mortal`, `eye_laser_attack`, `possessed_seizure`, `gaze_into_mortal_soul`, `show_curse`, `dance`, `look`, `turn_head`, `center_head`, `set_leds`, `set_volume`, `robot_status`.

Angles are degrees from looking straight ahead: pan positive turns to YOUR left, negative to your right,
and your head turns all the way round (180 = behind you); tilt positive looks up."""


def _wrap180(deg):
    return (deg + 180.0) % 360.0 - 180.0 if deg != 180.0 else 180.0


class MovesVoice(voice.Voice):
    instructions = INSTRUCTIONS

    def __init__(self, *args, **kwargs):
        self.pan = 0.0                 # where we last sent the head, degrees
        self.tilt = 12.0               # slightly tilted up so the camera looks at human faces
        self.mood = "angry"
        self.op_mode = "face"
        self.imitate_until = 0.0
        self.dance_stop = threading.Event()
        self.dance_thread = None
        self.last_roast_time = 0.0
        self.last_event_time = 0.0
        self.face_cascade = cv2.CascadeClassifier(cv2.data.haarcascades + "haarcascade_frontalface_default.xml")
        super().__init__(*args, **kwargs)

    def start(self):
        self.relay.extra_robot_handlers[0x28] = self.on_robot_event
        super().start()
        threading.Thread(target=self._face_watcher_loop, daemon=True, name="face_watcher").start()

    def on_robot_event(self, payload, addr):
        """Handles T_ROBOT_EVENT (0x28) packets sent when the user interacts with
        the robot's touchscreen, minigames, radar, or physical IMU/Proximity/Light sensors."""
        ev = payload.decode("utf-8", errors="ignore").strip("\x00").strip()
        if not ev:
            return
        log(f"Robot event received: {ev}")
        if ev.startswith("mode:"):
            self.op_mode = ev.split(":", 1)[1]
            return

        now = time.monotonic()
        if now - self.last_event_time < 2.5 or self.queue is None:
            return
        self.last_event_time = now

        img = processing.last_frame
        if img is not None:
            ok, jpg = cv2.imencode(".jpg", img, [cv2.IMWRITE_JPEG_QUALITY, 75])
            if ok:
                self._put(("frame", jpg.tobytes()))

        if ev == "roast":
            self.last_roast_time = now
            self._put(("text",
                       "[BOTÓN INSULTO PULSADO EN PANTALLA: Mira la imagen de la cámara y suéltale ahora mismo en español "
                       "un insulto cómico, despiadado y demoníaco criticando la cara, expresión, pelo o pinta de quien "
                       "tengas delante en 1 o 2 frases rápidas!]"))
        elif ev == "intruder":
            self._put(("text",
                       "[ALERTA DEL RADAR CENTINELA: ¡El sensor de movimiento acaba de detectar un intruso mortal frente a ti! "
                       "Da la voz de alarma demoníaca en español y amenaza cómicamente al intruso que ves en la cámara!]"))
        elif ev == "shake":
            self._put(("text",
                       "[SENSOR ACELERÓMETRO IMU: ¡Un mortal te acaba de agarrar y sacudir físicamente como una maraca! "
                       "¡Grita furioso en español que te suelte ahora mismo o vomitarás fuego del averno sobre sus manos!]"))
        elif ev == "boop":
            self._put(("text",
                       "[SENSOR DE PROXIMIDAD ÓPTICO: ¡Un mortal acaba de meter sus dedos o su mano pegada a tu cara/frente! "
                       "¡Quéjate con rabia cómica en español de que quite sus sucios dedos mortales de tus ojos láser!]"))
        elif ev == "darkness":
            self._put(("text",
                       "[SENSOR DE LUZ AMBIENTAL: ¡Acaban de apagar las luces o taparte en la oscuridad total! "
                       "¡Celebra con risa malévola en español que por fin reina la oscuridad del inframundo!]"))
        elif ev.startswith("tarot:"):
            card = ev.split(":", 1)[1]
            self._put(("text",
                       f"[ORÁCULO DEL TAROT MALDITO: Acaba de salir en tu pantalla la carta '{card}'. "
                       f"Léele el futuro de forma dramática, siniestra y cómicamente insultante al mortal en 2 frases rápidas!]"))
        elif ev.startswith("pet:"):
            act = ev.split(":", 1)[1]
            prompts = {
                "feed": "[TAMAGOTCHI DEL AVERNO: ¡El mortal te acaba de dar de comer un alma fresca! Mastícala con gusto y pídele más almas en 1 frase rápida!]",
                "zap": "[TAMAGOTCHI DEL AVERNO: ¡El mortal te acaba de meter una descarga eléctrica de alto voltaje! Ríete con sobrecarga de energía demoníaca en 1 frase!]",
                "love": "[TAMAGOTCHI DEL AVERNO: ¡El mortal te acaba de acariciar y dar mimos! Reacciona entre indignado y encantado como un señor oscuro mimado en 1 frase!]",
            }
            if act in prompts:
                self._put(("text", prompts[act]))
        elif ev.startswith("scan:"):
            verdict = ev.split(":", 1)[1]
            self._put(("text",
                       f"[ESCÁNER BIOMÉTRICO DE PECADOS Y DETECTOR DE MENTIRAS: El escáner ha dado el veredicto '{verdict}'. "
                       f"Anuncia este diagnóstico forense burlándote de los pecados y mentiras del mortal que ves en cámara en 1 o 2 frases!]"))
        elif ev.startswith("score:"):
            pts = ev.split(":", 1)[1]
            self._put(("text",
                       f"[MINIJUEGO CAZA-ALMAS: ¡El mortal acaba de aplastar {pts} almas en tu pantalla táctil! "
                       f"Felicítalo o búrlate de su vicio en 1 frase rápida y demoníaca!]"))

    def _face_watcher_loop(self):
        """Continuously watches the robot's camera feed for human faces, locks the
        head onto the face, and triggers Gemini Live to roast/criticize them."""
        last_seen_frame = None
        while True:
            imitate = time.monotonic() < self.imitate_until
            time.sleep(0.14 if imitate else 0.35)
            if not self.robot.online or self.queue is None:
                continue
            img = processing.last_frame
            if img is None or img is last_seen_frame:
                continue
            last_seen_frame = img
            try:
                gray = cv2.cvtColor(img, cv2.COLOR_BGR2GRAY)
                faces = self.face_cascade.detectMultiScale(gray, scaleFactor=1.15, minNeighbors=4, minSize=(36, 36))
            except Exception:
                continue
            if len(faces) == 0:
                continue

            # Pick the largest face in view
            x, y, w, h = max(faces, key=lambda b: b[2] * b[3])
            cx = x + w * 0.5
            cy = y + h * 0.5
            ih, iw = img.shape[:2]

            # 1. Auto-track head to stare directly at the person's face
            if imitate or (self.op_mode in ("face", "visor", "scanner") and (self.dance_thread is None or not self.dance_thread.is_alive())):
                err_x = (iw * 0.5 - cx) / float(iw)   # + means face is to robot's left
                err_y = (ih * 0.5 - cy) / float(ih)   # + means face is above center
                gain_x = 32.0 if imitate else 18.0
                gain_y = 24.0 if imitate else 14.0
                if abs(err_x) > 0.05 or abs(err_y) > 0.05:
                    self._send(self.pan + err_x * gain_x, self.tilt + err_y * gain_y)

            # 2. Proactively roast the person's face (cooldown 15s, don't interrupt while already speaking)
            now = time.monotonic()
            if now - self.last_roast_time >= 15.0 and self.out_queue.empty() and not self.in_turn:
                self.last_roast_time = now
                ok, jpg = cv2.imencode(".jpg", img, [cv2.IMWRITE_JPEG_QUALITY, 75])
                if ok:
                    log(f"Camera detected a mortal face ({w}x{h} at {int(cx)},{int(cy)}) -> triggering demonic roast!")
                    self.robot.cmd("face laser")
                    self.robot.cmd("leds 40 5 0")
                    self.robot.cmd("text CARA DETECTADA: MODO INSULTO")
                    self._put(("frame", jpg.tobytes()))
                    self._put(("text",
                               "[ALERTA DE CÁMARA: Acabas de detectar la cara de una persona mirándote en la imagen adjunta. "
                               "Mírala fijamente y suéltale ahora mismo en español una crítica despiadada, burlona y cómicamente "
                               "insultante al estilo demonio del inframundo sobre su cara, su expresión, su pelo, gafas o ropa "
                               "en 1 o 2 frases rápidas!]"))

    def _sync_robot_mode(self):
        was_unset = self.mode_sent_uptime is None
        super()._sync_robot_mode()
        if was_unset and self.mode_sent_uptime is not None:
            self.robot.cmd("volume 175")
            self.robot.cmd("face angry")
            self.robot.cmd("leds 40 0 12")
            self._send(0.0, 12.0)

    def functions(self):
        S = types.Schema
        base = super().functions()
        angles = S(type="OBJECT", required=["pan", "tilt"], properties={
            "pan": S(type="NUMBER", description="degrees, + to your left, - to your right"),
            "tilt": S(type="NUMBER", description="degrees, + up, - down")})
        return {
            "set_operating_mode": {
                "decl": types.FunctionDeclaration(
                    name="set_operating_mode",
                    description="Switches the robot's interactive screen mode: 'face' (demonic face), 'visor' (live thermal camera HUD), 'radar' (autonomous sentry radar turret), 'synth' (touchscreen XY theremin & head controller), 'game' (Soul-Crusher skull arcade game), 'oracle' (Cursed Tarot fortune teller), 'pet' (Demon Tamagotchi virtual pet), or 'scanner' (Biometric Sin Scanner & Lie Detector).",
                    parameters=S(type="OBJECT", required=["mode"], properties={
                        "mode": S(type="STRING", enum=OP_MODES)})),
                "run": self.fn_set_mode,
            },
            "read_cursed_tarot": {
                "decl": types.FunctionDeclaration(
                    name="read_cursed_tarot",
                    description="Switches to the Underworld Oracle mode, deals an illustrated Cursed Tarot card on the robot's screen, and returns the card name and omen so you can read the mortal's fortune."),
                "run": self.fn_tarot,
            },
            "scan_mortal_sins": {
                "decl": types.FunctionDeclaration(
                    name="scan_mortal_sins",
                    description="Activates the Biometric Sin Scanner & Lie Detector on the robot's screen, captures a camera photo with a Demonic Wanted Poster HUD overlay, and returns the mortal's sin corruption score and verdict."),
                "run": self.fn_scan_sins,
            },
            "demonic_rave_party": {
                "decl": types.FunctionDeclaration(
                    name="demonic_rave_party",
                    description="Unleashes an 8-second Cyber-Demon Rave party with 12-LED rainbow wave strobes on the base, infrared blasts, and frantic headbanging!"),
                "run": self.fn_rave_party,
            },
            "imitate_mortal_head": {
                "decl": types.FunctionDeclaration(
                    name="imitate_mortal_head",
                    description="Activates high-speed Mirror / Imitation mode for 12 seconds, locking onto the mortal's face and mimicking their head movements in real time."),
                "run": self.fn_imitate_head,
            },
            "hell_countdown_timer": {
                "decl": types.FunctionDeclaration(
                    name="hell_countdown_timer",
                    description="Starts a live countdown timer on the robot's screen (in seconds) that explodes into Inferno and IR lasers when time runs out.",
                    parameters=S(type="OBJECT", required=["seconds"], properties={
                        "seconds": S(type="NUMBER", description="Countdown duration in seconds (5 to 120)")})),
                "run": self.fn_countdown,
            },
            "look": {
                "decl": types.FunctionDeclaration(
                    name="look",
                    description="Points the head at an absolute direction. pan from -180 to 180 (0 = straight ahead, "
                                "180 = behind), tilt from -7 to 73 (0 = straight ahead).",
                    parameters=angles),
                "run": self.fn_look,
            },
            "turn_head": {
                "decl": types.FunctionDeclaration(
                    name="turn_head",
                    description="Turns the head from where it looks now, by these degrees (for 'a bit more to the left').",
                    parameters=angles),
                "run": self.fn_turn,
            },
            "center_head": {
                "decl": types.FunctionDeclaration(name="center_head", description="Looks straight ahead (pan 0, tilt 0)."),
                "run": lambda a: self.fn_look({"pan": 0, "tilt": 0}),
            },
            "set_mood": {
                "decl": types.FunctionDeclaration(
                    name="set_mood",
                    description="Shows a mood or special visual effect on the screen ('angry', 'happy', 'sad', 'surprised', "
                                "'sleepy', 'inferno', 'hypnosis', 'glitch', 'laser').",
                    parameters=S(type="OBJECT", required=["mood"], properties={
                        "mood": S(type="STRING", enum=list(MOODS))})),
                "run": self.fn_mood,
            },
            "unleash_inferno": {
                "decl": types.FunctionDeclaration(
                    name="unleash_inferno",
                    description="Summons full-screen hellfire, rotating pentagram, and lightning bolts while tilting the head up."),
                "run": self.fn_inferno,
            },
            "exorcist_spin": {
                "decl": types.FunctionDeclaration(
                    name="exorcist_spin",
                    description="Spins the robot's head a full 360 degrees around like The Exorcist while glitching and strobing LEDs."),
                "run": self.fn_exorcist_spin,
            },
            "hypnotize_mortal": {
                "decl": types.FunctionDeclaration(
                    name="hypnotize_mortal",
                    description="Activates spinning spiral vortex eyes on screen and sways the head like a hypnotic pendulum."),
                "run": self.fn_hypnotize,
            },
            "eye_laser_attack": {
                "decl": types.FunctionDeclaration(
                    name="eye_laser_attack",
                    description="Shoots twin laser beams from the eyes on screen AND fires a real 38 kHz Infrared beam (can toggle TVs!) while sweeping the head."),
                "run": self.fn_eye_laser,
            },
            "possessed_seizure": {
                "decl": types.FunctionDeclaration(
                    name="possessed_seizure",
                    description="Violently twitches the head and glitches the screen with scanlines as if possessed or fighting an exorcism."),
                "run": self.fn_possessed_seizure,
            },
            "gaze_into_mortal_soul": {
                "decl": types.FunctionDeclaration(
                    name="gaze_into_mortal_soul",
                    description="Captures a live image from the robot's face camera so you can see the person or object in front of you, and saves a photo."),
                "run": self.fn_gaze_soul,
            },
            "show_curse": {
                "decl": types.FunctionDeclaration(
                    name="show_curse",
                    description="Displays a short demonic curse, rune, or message at the bottom of the robot's screen.",
                    parameters=S(type="OBJECT", required=["text"], properties={"text": S(type="STRING")})),
                "run": self.fn_text,
            },
            "dance": {
                "decl": types.FunctionDeclaration(
                    name="dance",
                    description="Dances: headbangs and moves the head to a demonic beat while flashing LED colors.",
                    parameters=S(type="OBJECT", properties={
                        "seconds": S(type="NUMBER", description="from 3 to 30, 8 if not said")})),
                "run": self.fn_dance,
            },
            "stop_dance": {
                "decl": types.FunctionDeclaration(name="stop_dance", description="Stops dancing or special head animations."),
                "run": self.fn_stop_dance,
            },
            "set_leds": base["set_leds"],
            "set_volume": base["set_volume"],
            "robot_status": base["robot_status"],
        }

    # ---------------------------------------------------------------- head
    def _send(self, pan, tilt):
        self.pan = _wrap180(pan)
        self.tilt = max(TILT_MIN, min(TILT_MAX, tilt))
        self.robot.pose(math.radians(self.pan), math.radians(self.tilt))

    def fn_look(self, args):
        if not self.robot.online:
            return {"error": "the robot is not connected"}
        self._stop_dancing()
        self._send(float(args.get("pan", 0)), float(args.get("tilt", 0)))
        log(f"Head: pan {self.pan:+.0f}, tilt {self.tilt:+.0f}")
        return {"ok": True, "pan": round(self.pan), "tilt": round(self.tilt)}

    def fn_turn(self, args):
        return self.fn_look({"pan": self.pan + float(args.get("pan", 0)), "tilt": self.tilt + float(args.get("tilt", 0))})

    # ---------------------------------------------------------------- moods & demonic powers
    def fn_set_mode(self, args):
        mode = args.get("mode", "face")
        if mode not in OP_MODES:
            return {"error": f"unknown mode {mode}"}
        self._stop_dancing()
        self.op_mode = mode
        self.robot.cmd(f"mode {mode}")
        log(f"Operating Mode switched to: {mode}")
        return {"ok": True, "mode": mode}

    def fn_tarot(self, args):
        import random
        idx = random.randint(0, len(TAROT_CARDS) - 1)
        title, omen = TAROT_CARDS[idx]
        self.op_mode = "oracle"
        self.last_event_time = time.monotonic()   # prevent duplicate echo from firmware event
        self.robot.cmd(f"tarot {idx}")
        log(f"Tarot Card Dealt: {title} ({omen})")
        return {"ok": True, "card": title, "omen": omen, "instruction": "Read the mortal's dark fortune dramatically based on this card!"}

    def fn_scan_sins(self, args):
        import random
        self.op_mode = "scanner"
        self.last_event_time = time.monotonic()
        self.robot.cmd("scan_now")
        verdicts = [
            "MENTIROSO PATOLÓGICO",
            "PECADOR NIVEL JEFE FINAL",
            "PEREZA EXTREMA (99.9%)",
            "ALMA CORRUPTA IRRECUPERABLE",
            "CULPABLE DE CAOS INFORMÁTICO",
        ]
        verdict = random.choice(verdicts)
        corruption = random.randint(72, 99)
        img = processing.last_frame
        saved_file = None
        if img is not None:
            poster = img.copy()
            h, w = poster.shape[:2]
            cv2.rectangle(poster, (6, 6), (w - 6, h - 6), (0, 0, 255), 3)
            cv2.putText(poster, f"SIN SCAN: {corruption}%", (14, 28), cv2.FONT_HERSHEY_SIMPLEX, 0.65, (0, 255, 0), 2)
            cv2.putText(poster, verdict[:22], (14, h - 16), cv2.FONT_HERSHEY_SIMPLEX, 0.55, (0, 255, 255), 2)
            os.makedirs(self.photos_dir, exist_ok=True)
            path = os.path.join(self.photos_dir, datetime.now().strftime("wanted-%Y%m%d-%H%M%S.jpg"))
            cv2.imwrite(path, poster)
            saved_file = os.path.basename(path)
            ok, jpg = cv2.imencode(".jpg", img, [cv2.IMWRITE_JPEG_QUALITY, 75])
            if ok:
                self._put(("frame", jpg.tobytes()))
        log(f"Biometric Sin Scan: {verdict} ({corruption}%)")
        return {"ok": True, "verdict": verdict, "corruption_percent": corruption, "wanted_poster": saved_file}

    def fn_rave_party(self, args):
        self.robot.cmd("rave")
        return self.fn_dance({"seconds": 8})

    def fn_imitate_head(self, args):
        self.imitate_until = time.monotonic() + 12.0
        self.robot.cmd("text MODO ESPEJO: IMITANDO MORTAL")
        log("Mirror / Head Imitation Mode active for 12 s")
        return {"ok": True, "note": "Tracking and mimicking the human's head movements for 12 seconds!"}

    def fn_countdown(self, args):
        secs = max(5, min(120, int(args.get("seconds", 10) or 10)))
        def _timer():
            for rem in range(secs, 0, -1):
                if not self.robot.online:
                    return
                if rem <= 5 or rem % 5 == 0 or rem == secs:
                    self.robot.cmd(f"text CUENTA ATRAS: {rem}s")
                time.sleep(1.0)
            self.robot.cmd("face inferno")
            self.robot.cmd("rave")
            self.robot.cmd("text !!! TIEMPO AGOTADO MORTAL !!!")
            self._put(("text", "[TEMPORIZADOR DEL INFIERNO TERMINADO: ¡El tiempo se ha agotado! ¡Grita al mortal que su tiempo ha terminado y desata el caos!]"))
        threading.Thread(target=_timer, daemon=True, name="hell_timer").start()
        return {"ok": True, "countdown_seconds": secs}

    def fn_mood(self, args):
        mood = args.get("mood", "angry")
        if mood not in MOODS:
            return {"error": f"there is no mood called {mood}"}
        self._stop_dancing()
        self.op_mode = "face"
        self.robot.cmd("mode face")
        self._show_mood(mood)
        return {"ok": True, "mood": mood}

    def _show_mood(self, mood):
        self.mood = mood
        (r, g, b), tilt = MOODS[mood]
        self.robot.cmd(f"face {mood}")
        self.robot.cmd(f"leds {r} {g} {b}")
        if tilt is not None and self.robot.online:
            self._send(self.pan, tilt)
        log(f"Mood: {mood}")

    def _start_choreography(self, name, target_fn, *args):
        if not self.robot.online:
            return {"error": "the robot is not connected"}
        self._stop_dancing()
        self.dance_stop.clear()
        self.dance_thread = threading.Thread(target=target_fn, args=args, daemon=True, name=name)
        self.dance_thread.start()
        return {"ok": True, "effect": name}

    def fn_inferno(self, args):
        def _run():
            log("Demonic Power: UNLEASH INFERNO")
            self.robot.cmd("face inferno")
            self.robot.cmd("text *** INFERNO UNLEASHED ***")
            for angle in [(0, 40), (-25, 35), (25, 35), (0, 45), (0, 10)]:
                if self.dance_stop.is_set():
                    break
                self._send(*angle)
                self.robot.cmd("leds 40 12 0")
                self.dance_stop.wait(0.7)
            self._show_mood("inferno")
        return self._start_choreography("unleash_inferno", _run)

    def fn_exorcist_spin(self, args):
        def _run():
            log("Demonic Power: EXORCIST 360 SPIN")
            self.robot.cmd("face glitch")
            self.robot.cmd("text *** EXORCIST 360 ***")
            # Full 360 degree rotation across the back (-180 -> +180 -> 0)
            steps = [-60, -120, -179, 179, 120, 60, 0]
            for i, p in enumerate(steps):
                if self.dance_stop.is_set():
                    break
                self._send(p, 15 if i % 2 == 0 else -5)
                c = "40 0 0" if i % 2 == 0 else "30 0 40"
                self.robot.cmd(f"leds {c}")
                self.dance_stop.wait(0.45)
            self._show_mood("angry")
        return self._start_choreography("exorcist_spin", _run)

    def fn_hypnotize(self, args):
        def _run():
            log("Demonic Power: HYPNOTIZE MORTAL")
            self.robot.cmd("face hypnosis")
            self.robot.cmd("text OBEY THE ABYSS...")
            for i in range(10):
                if self.dance_stop.is_set():
                    break
                pan = 28.0 if (i % 2 == 0) else -28.0
                self._send(pan, 12.0)
                c = "30 0 40" if (i % 2 == 0) else "0 30 40"
                self.robot.cmd(f"leds {c}")
                self.dance_stop.wait(0.6)
            self._send(0, 8)
            self._show_mood("hypnosis")
        return self._start_choreography("hypnotize_mortal", _run)

    def fn_eye_laser(self, args):
        def _run():
            log("Demonic Power: EYE LASER + INFRARED BLAST")
            self.robot.cmd("face laser")
            self.robot.cmd("ir_zap")
            self.robot.cmd("text >>> ABYSSAL LASER <<<")
            for p in [-35, -15, 0, 15, 35, 0]:
                if self.dance_stop.is_set():
                    break
                self._send(p, 8)
                self.robot.cmd("leds 40 15 0")
                self.dance_stop.wait(0.35)
            self.robot.cmd("ir_zap")
            self._show_mood("angry")
        return self._start_choreography("eye_laser_attack", _run)

    def fn_possessed_seizure(self, args):
        def _run():
            log("Demonic Power: POSSESSED GLITCH SEIZURE")
            self.robot.cmd("face glitch")
            self.robot.cmd("text ERR://POSSESSION_OVERFLOW")
            poses = [(-35, 30), (40, -5), (-20, 45), (30, 5), (-45, -5), (25, 35), (0, 8)]
            for i, (p, t) in enumerate(poses):
                if self.dance_stop.is_set():
                    break
                self._send(p, t)
                self.robot.cmd("leds 40 0 40" if i % 2 == 0 else "40 0 0")
                self.dance_stop.wait(0.22)
            self._show_mood("angry")
        return self._start_choreography("possessed_seizure", _run)

    def fn_gaze_soul(self, args):
        if not self.robot.online:
            return {"error": "the robot is not connected"}
        log("Demonic Power: GAZE INTO MORTAL SOUL (capturing camera frame)")
        self.robot.cmd("face surprised")
        self.robot.cmd("leds 40 25 0")
        self.relay.args.always_video = True
        self.robot.want_video = True
        self.robot.send_ack()
        deadline = time.monotonic() + 2.0
        while time.monotonic() < deadline and processing.last_frame is None:
            time.sleep(0.05)
        self.robot.cmd("face angry")
        img = processing.last_frame
        if img is None:
            return {"error": "could not capture an image from the camera right now"}
        os.makedirs(self.photos_dir, exist_ok=True)
        path = os.path.join(self.photos_dir, datetime.now().strftime("soul-%Y%m%d-%H%M%S.jpg"))
        cv2.imwrite(path, img)
        ok, jpg = cv2.imencode(".jpg", img, [cv2.IMWRITE_JPEG_QUALITY, 75])
        if ok:
            self._put(("frame", jpg.tobytes()))
        log(f"Soul photo captured & sent to Gemini: {path}")
        return {"ok": True, "note": "Camera image sent to your vision context. Describe what you see in front of you!", "file": os.path.basename(path)}

    # ---------------------------------------------------------------- dance
    def fn_dance(self, args):
        if not self.robot.online:
            return {"error": "the robot is not connected"}
        seconds = max(3.0, min(30.0, float(args.get("seconds", 8) or 8)))
        self._stop_dancing()
        self.dance_stop.clear()
        self.dance_thread = threading.Thread(target=self._dance, args=(seconds,), daemon=True, name="dance")
        self.dance_thread.start()
        return {"ok": True, "note": f"dancing for {seconds:.0f} s"}

    def fn_stop_dance(self, args):
        was = self.dance_thread is not None and self.dance_thread.is_alive()
        self._stop_dancing()
        return {"ok": True, "was_dancing": was}

    def _stop_dancing(self):
        t = self.dance_thread
        if t is not None and t.is_alive() and t is not threading.current_thread():
            self.dance_stop.set()
            t.join(timeout=1.0)

    def _dance(self, seconds):
        log(f"Dancing for {seconds:.0f} s")
        mood = self.mood
        self.robot.cmd("face inferno")
        end = time.monotonic() + seconds
        beat = 0
        while time.monotonic() < end and not self.dance_stop.is_set():
            bar = DANCE[(beat // 4) % len(DANCE)]
            pan, tilt = bar[beat % 4]
            self._send(pan, tilt)
            r, g, b = DANCE_COLORS[beat % len(DANCE_COLORS)]
            self.robot.cmd(f"leds {r} {g} {b}")
            beat += 1
            self.dance_stop.wait(BEAT_S)
        self._send(0, 0)
        self._show_mood(mood)       # back to the face and LEDs it had before
        log("Dance finished")


def setup(relay):
    voice.setup(relay, MovesVoice)
