#!/usr/bin/env python3
"""
Sonic 3 A.I.R. - 3DS Mod Patcher

Sets up the game files and installs, checks and orders mods for the Nintendo 3DS port
of Sonic 3 A.I.R., directly on the 3DS SD card (or any folder that mirrors its layout).

Game file setup ("Set up game files" / the "setup" command):
  - Copies the "data" folder from the official A.I.R. PC download (the .zip or the
    unpacked folder) and checks that it's the version the port needs
  - Finds your Sonic 3 & Knuckles ROM in your Steam library (or takes the file you pick),
    checks that it's the right one - exactly like the game does - and copies it under
    the file name the game requires

  * Double-click / run without arguments  -> simple window (Tkinter)
  * Run with a command                    -> command line, see --help

What it does when installing a mod:
  - Accepts a mod as a .zip file or a folder, and finds its mod.json (even if nested)
  - Unpacks zips into a plain folder: the game keeps every file it reads from a zip in
    RAM, which the 3DS can't afford, while folders are streamed from the SD card
  - Checks the mod for things the 3DS port can't handle (custom shaders, huge images,
    lots of music, very large total size) and reports them
  - Copies it to  <SD>/3ds/sonic3air/savedata/mods/  and enables it (highest priority)

Only uses the Python standard library (3.8+). Licensed under GPLv3 like Sonic 3 A.I.R.
"""

import argparse
import json
import os
import shutil
import string
import struct
import sys
import tempfile
import zipfile
from dataclasses import dataclass, field
from pathlib import Path, PurePosixPath
from typing import List, Optional

GAME_DIR = Path("3ds") / "sonic3air"
MODS_DIR = GAME_DIR / "savedata" / "mods"
ACTIVE_MODS_FILE = "active-mods.json"
ROM_NAME = "Sonic_Knuckles_wSonic3.bin"
REQUIRED_DATA_FILES = ["gamedata.bin", "enginedata.bin", "audiodata.bin", "scripts.bin", "metadata.json"]
REQUIRED_AIR_VERSION = "26.03.28.0"      # The A.I.R. version the port is built from: its data files must match exactly
AIR_DOWNLOAD_URL = "https://github.com/Eukaryot/sonic3air/releases/download/v26.03.28.0-stable/sonic3air_game.zip"
AIR_DATA_SIZES = {"gamedata.bin": 3705612, "enginedata.bin": 508007, "audiodata.bin": 2289447, "scripts.bin": 783815}   # Of that version
DSP_FIRMWARE = Path("3ds") / "dspfirm.cdc"

# Copies to the SD card go to a "<name>.s3air3ds-part" file first and get their real name only once they're complete, so a
# copy that's cut off (card full, card pulled out, window closed) never looks like a finished file.
# Mods are copied next to the mods folder first, and only a complete copy takes the place of the old version.
PART_SUFFIX = ".s3air3ds-part"
MODS_INCOMING_DIR = GAME_DIR / "savedata" / "mods_incoming"     # New mod versions while they're copied
MODS_REPLACED_DIR = GAME_DIR / "savedata" / "mods_replaced"     # Old versions for the moment of the swap
SPACE_MARGIN = 2 * 1024 * 1024

# ROM check, same as the game's (see ConfigurationImpl.cpp and ResourcesCache.cpp)
ROM_SIZE = 0x400000
ROM_CHECKSUM = 0x344983ffcfeff8cb
ROM_OVERWRITES = [(0x2001f0, 0x4a)]

# Compatibility thresholds for the 3DS (New 3DS: ~178 MB of app memory in total)
MAX_IMAGE_SIDE = 1024            # pixels
MAX_TOTAL_AUDIO_MB = 40
MAX_TOTAL_SIZE_MB = 80
SHADER_EXTENSIONS = {".glsl", ".shader", ".vert", ".frag", ".vs", ".fs"}


# ---------------------------------------------------------------------------
#  SD card layout
# ---------------------------------------------------------------------------

def find_sd_cards() -> List[Path]:
	"""Return likely 3DS SD card roots (drives with a 'Nintendo 3DS' or '3ds' folder)."""
	candidates: List[Path] = []
	if os.name == "nt":
		candidates = [Path(f"{letter}:/") for letter in string.ascii_uppercase]
	else:
		user = os.environ.get("USER", "")
		for base in [Path("/Volumes"), Path("/media") / user, Path("/run/media") / user, Path("/media"), Path("/mnt")]:
			try:
				candidates += [p for p in base.iterdir() if p.is_dir()]
			except OSError:
				pass
	found = []
	for root in candidates:
		try:
			if (root / "Nintendo 3DS").is_dir() or (root / "3ds").is_dir():
				found.append(root)
		except OSError:
			pass
	return found


class Progress:
	"""What a long copy is doing, for the progress bar (read by the window while the copy runs in a thread of its own)."""
	def __init__(self, callback=None):
		self.text = ""
		self.done = 0
		self.total = 0
		self.callback = callback

	def start(self, text: str, total: int):
		self.text, self.done, self.total = text, 0, total
		self._report()

	def add(self, amount: int):
		self.done += amount
		self._report()

	def _report(self):
		if self.callback:
			self.callback(self)


def _free_space(path: Path) -> Optional[int]:
	"""Free bytes on the drive of a path (the path doesn't need to exist yet)."""
	for candidate in [path] + list(path.parents):
		if candidate.exists():
			try:
				return shutil.disk_usage(candidate).free
			except OSError:
				return None
	return None


def _check_space(target: Path, needed: int):
	free = _free_space(target)
	if free is not None and free < needed + SPACE_MARGIN:
		mb = 1024 * 1024
		raise PatcherError(f"Not enough space on the SD card: this needs {needed / mb:.1f} MB, but only {free / mb:.1f} MB are free.\n"
						   "Nothing was changed. Make some room (old mods, screenshots, other games) and try again.")


def _copy_file(src: Path, dst: Path, progress: Optional[Progress] = None):
	"""Copy a file so that it only gets its real name once it's complete and has the right size."""
	part = dst.with_name(dst.name + PART_SUFFIX)
	size = src.stat().st_size
	dst.parent.mkdir(parents=True, exist_ok=True)
	try:
		with open(src, "rb") as fin, open(part, "wb") as fout:
			while True:
				chunk = fin.read(1024 * 1024)
				if not chunk:
					break
				fout.write(chunk)
				if progress:
					progress.add(len(chunk))
		written = part.stat().st_size
		if written != size:
			raise PatcherError(f"Copying {src.name} stopped after {written} of {size} bytes. Is the SD card full, or was it removed?")
		os.replace(part, dst)
	except BaseException:
		try:
			part.unlink()
		except OSError:
			pass
		raise


def _tree_files(folder: Path) -> List[Path]:
	return [p for p in folder.rglob("*") if p.is_file()]


def _copy_tree(src: Path, dst: Path, progress: Optional[Progress] = None, text: str = "Copying"):
	"""Copy a folder file by file (with the size check of _copy_file), then check that every file arrived."""
	files = _tree_files(src)
	if progress:
		progress.start(text, sum(p.stat().st_size for p in files))
	for file in files:
		_copy_file(file, dst / file.relative_to(src), progress)
	for file in files:
		copy = dst / file.relative_to(src)
		if not copy.is_file() or copy.stat().st_size != file.stat().st_size:
			raise PatcherError(f"{file.relative_to(src).as_posix()} didn't arrive complete on the SD card.")


def recover_interrupted(sd_root: Path) -> List[str]:
	"""Clean up after a copy that was cut off earlier: put back an old mod version that was in the middle of being replaced,
	delete half-copied files and folders. Returns what was done, for telling the user."""
	messages = []
	restored = set()
	replaced = sd_root / MODS_REPLACED_DIR
	mods_dir = sd_root / MODS_DIR
	if replaced.is_dir():
		for old in sorted(replaced.iterdir()):
			if not old.is_dir():
				continue
			if (mods_dir / old.name).exists():
				shutil.rmtree(old, ignore_errors=True)      # The swap had finished, only the clean-up was missing
			else:
				mods_dir.mkdir(parents=True, exist_ok=True)
				os.replace(old, mods_dir / old.name)
				restored.add(old.name)
				messages.append(f"Updating '{old.name}' was cut off earlier. Its previous version is back in place; install the update again.")
		shutil.rmtree(replaced, ignore_errors=True)
	incoming = sd_root / MODS_INCOMING_DIR
	if incoming.is_dir():
		for half in sorted(p for p in incoming.iterdir() if p.name not in restored):
			messages.append(f"Removed a half-copied mod '{half.name}' from an install that was cut off. Install it again.")
		shutil.rmtree(incoming, ignore_errors=True)
	game = sd_root / GAME_DIR
	for folder in [game, game / "data"]:
		try:
			parts = [p for p in folder.iterdir() if p.name.endswith(PART_SUFFIX)]
		except OSError:
			continue
		for part in parts:
			try:
				part.unlink()
			except OSError:
				continue
			messages.append(f"Removed a half-copied file '{part.name[:-len(PART_SUFFIX)]}' from a copy that was cut off.")
	return messages


@dataclass
class SetupReport:
	ok: bool
	lines: List[str]


def check_sd(sd_root: Path) -> SetupReport:
	"""Check that the game data, the ROM and the sound firmware are where the 3DS build expects them."""
	game = sd_root / GAME_DIR
	lines = []
	ok = True

	def item(good: bool, text: str, fix: str = ""):
		nonlocal ok
		ok = ok and good
		lines.append(("[OK]   " if good else "[MISS] ") + text + ("" if good or not fix else f"\n         -> {fix}"))

	version = read_air_version(game / "data")
	for name in REQUIRED_DATA_FILES:
		path = game / "data" / name
		if not path.is_file():
			item(False, f"data/{name}", f"Use 'Set up game files' with the official Sonic 3 A.I.R. {REQUIRED_AIR_VERSION} download:\n"
										f"         {AIR_DOWNLOAD_URL}")
			continue
		expected = AIR_DATA_SIZES.get(name) if version == REQUIRED_AIR_VERSION else None
		size = path.stat().st_size
		item(expected is None or size == expected, f"data/{name}" + ("" if expected is None or size == expected else
			 f" (incomplete or damaged: {size} bytes instead of {expected})"),
			 "Use 'Set up game files' again, it copies the data folder fresh")
	if version is not None:
		item(version == REQUIRED_AIR_VERSION, f"A.I.R. data version {version}",
			 f"The port needs the data of version {REQUIRED_AIR_VERSION} exactly, otherwise the scripts fail to load.\n"
			 f"         That version: {AIR_DOWNLOAD_URL}")
	leftovers = [p.name for folder in [game, game / "data"] if folder.is_dir() for p in folder.iterdir() if p.name.endswith(PART_SUFFIX)]
	if leftovers or (sd_root / MODS_INCOMING_DIR).is_dir() or (sd_root / MODS_REPLACED_DIR).is_dir():
		lines.append("[INFO] A copy was cut off earlier; the next install or 'Set up game files' cleans it up")

	rom_paths = [game / ROM_NAME, game / "savedata" / ROM_NAME]
	rom = next((p for p in rom_paths if p.is_file()), None)
	if rom is None:
		item(False, ROM_NAME, "Use 'Set up game files' to copy your own Steam ROM (it is not included, for legal reasons)")
	else:
		good, reason = check_rom(rom)
		item(good, f"{ROM_NAME}" + ("" if good else f" ({reason})"), "This is not the Sonic 3 & Knuckles ROM from Steam that the game needs")

	item((sd_root / DSP_FIRMWARE).is_file(), DSP_FIRMWARE.as_posix() + " (sound firmware)",
		 "Run the DSP1 homebrew app once on your 3DS, otherwise the game has no sound")

	has_remaster = (game / "data" / "audioremaster.bin").is_file()
	lines.append(("[OK]   " if has_remaster else "[INFO] ") + "data/audioremaster.bin (optional remastered music)")
	return SetupReport(ok, lines)


# ---------------------------------------------------------------------------
#  Game file setup: ROM and A.I.R. data
# ---------------------------------------------------------------------------

def murmur2_64(data: bytes) -> int:
	"""MurmurHash64A with seed 0, like rmx::getMurmur2_64 in the game."""
	mask = 0xFFFFFFFFFFFFFFFF
	m = 0xc6a4a7935bd1e995
	r = 47
	length = len(data)
	h = (length * m) & mask
	whole = length // 8
	for (k,) in struct.iter_unpack("<Q", data[:whole * 8]):
		k = (k * m) & mask
		k ^= k >> r
		k = (k * m) & mask
		h ^= k
		h = (h * m) & mask
	tail = data[whole * 8:]
	if tail:
		for i in reversed(range(len(tail))):
			h ^= tail[i] << (8 * i)
		h = (h * m) & mask
	h ^= h >> r
	h = (h * m) & mask
	h ^= h >> r
	return h


def check_rom(path: Path):
	"""Returns (ok, reason): whether this is the ROM the game accepts, checked the same way as the game does."""
	try:
		size = path.stat().st_size
		if size != ROM_SIZE:
			return False, f"wrong size: {size} bytes instead of {ROM_SIZE}"
		data = bytearray(path.read_bytes())
	except OSError as e:
		return False, str(e)
	for offset, value in ROM_OVERWRITES:
		data[offset] = value
	if murmur2_64(bytes(data)) != ROM_CHECKSUM:
		return False, "different content (not the Steam version of Sonic 3 & Knuckles)"
	return True, ""


def find_steam_roms() -> List[Path]:
	"""Look for the Steam ROM in the usual Steam library locations."""
	steam_roots = []
	if os.name == "nt":
		for env in ("ProgramFiles(x86)", "ProgramFiles"):
			if os.environ.get(env):
				steam_roots.append(Path(os.environ[env]) / "Steam")
	else:
		home = Path.home()
		steam_roots += [home / ".steam" / "steam", home / ".local" / "share" / "Steam",
						home / ".var" / "app" / "com.valvesoftware.Steam" / ".local" / "share" / "Steam",
						home / "Library" / "Application Support" / "Steam"]

	# Additional Steam libraries are listed in libraryfolders.vdf
	libraries = []
	for root in steam_roots:
		libraries.append(root)
		vdf = root / "steamapps" / "libraryfolders.vdf"
		try:
			for line in vdf.read_text(encoding="utf-8", errors="ignore").splitlines():
				parts = line.strip().split('"')
				if len(parts) >= 4 and parts[1] == "path":
					libraries.append(Path(parts[3].replace("\\\\", "\\")))
		except OSError:
			pass

	found = []
	for library in libraries:
		common = library / "steamapps" / "common"
		try:
			games = [p for p in common.iterdir() if p.is_dir()]
		except OSError:
			continue
		for game in games:
			for candidate in [game / ROM_NAME, game / "uncompressed ROMs" / ROM_NAME]:
				if candidate.is_file() and candidate not in found:
					found.append(candidate)
	return found


def read_air_version(data_dir: Path) -> Optional[str]:
	try:
		return str(json.loads((data_dir / "metadata.json").read_text(encoding="utf-8-sig")).get("Version"))
	except (OSError, ValueError, AttributeError):
		return None


def _find_air_data_dir(directory: Path) -> Optional[Path]:
	"""Find the A.I.R. 'data' folder (with gamedata.bin and metadata.json) somewhere below a folder."""
	for meta in sorted(directory.rglob("metadata.json"), key=lambda p: len(p.parts)):
		if (meta.parent / "gamedata.bin").is_file():
			return meta.parent
	return None


def install_rom(rom: Path, sd_root: Path, progress: Optional[Progress] = None) -> Path:
	good, reason = check_rom(rom)
	if not good:
		raise PatcherError(f"{rom.name}: {reason}.\nSonic 3 A.I.R. only accepts the ROM from the Steam release of Sonic 3 & Knuckles.")
	target = sd_root / GAME_DIR / ROM_NAME
	_check_space(target.parent, ROM_SIZE)      # The new copy sits next to an old one until it's complete
	if progress:
		progress.start("Copying the ROM", ROM_SIZE)
	_copy_file(rom, target, progress)
	good, reason = check_rom(target)
	if not good:
		raise PatcherError(f"The ROM copy on the SD card doesn't check out ({reason}). Try again, or try another SD card reader.")
	return target


def version_hint(version: Optional[str]) -> str:
	return (f"This is Sonic 3 A.I.R. version {version}, but the 3DS port needs version {REQUIRED_AIR_VERSION}.\n"
			"The data files must match the port exactly, otherwise the game can't load its scripts.\n\n"
			f"That exact version is still available here:\n{AIR_DOWNLOAD_URL}")


def install_air_data(source: Path, sd_root: Path, progress: Optional[Progress] = None) -> str:
	"""Copy the 'data' folder of the official A.I.R. download (zip or folder) to the SD card. Returns the version."""
	recover_interrupted(sd_root)
	with tempfile.TemporaryDirectory() as tmp:
		if source.is_file() and zipfile.is_zipfile(source):
			_safe_extract(source, Path(tmp))
			source = Path(tmp)
		if not source.is_dir():
			raise PatcherError(f"{source} is neither a .zip file nor a folder.")
		data_dir = _find_air_data_dir(source)
		if data_dir is None:
			raise PatcherError("No A.I.R. 'data' folder found in there (with gamedata.bin and metadata.json).\n"
							   "Use the Windows download of Sonic 3 A.I.R. from sonic3air.org.")
		version = read_air_version(data_dir)
		if version != REQUIRED_AIR_VERSION:
			raise PatcherError(version_hint(version))
		missing = [name for name in REQUIRED_DATA_FILES if not (data_dir / name).is_file()]
		if missing:
			raise PatcherError("The A.I.R. data folder is incomplete, missing: " + ", ".join(missing))
		damaged = [name for name, size in AIR_DATA_SIZES.items() if (data_dir / name).stat().st_size != size]
		if damaged:
			raise PatcherError("The A.I.R. download looks damaged (" + ", ".join(damaged) + " has the wrong size). Download it again:\n" + AIR_DOWNLOAD_URL)

		target = sd_root / GAME_DIR / "data"
		files = [f for f in data_dir.iterdir() if f.is_file()]
		# One file at a time sits next to its old version until it's complete
		sizes = [f.stat().st_size for f in files]
		existing = sum((target / f.name).stat().st_size for f in files if (target / f.name).is_file())
		_check_space(target, max(0, sum(sizes) - existing) + max(sizes, default=0))
		if progress:
			progress.start("Copying the game data", sum(f.stat().st_size for f in files))
		# metadata.json (with the version) goes last: as long as it's missing, the data counts as not set up
		for file in sorted(files, key=lambda f: f.name == "metadata.json"):
			_copy_file(file, target / file.name, progress)
	(sd_root / MODS_DIR).mkdir(parents=True, exist_ok=True)
	return version


# ---------------------------------------------------------------------------
#  Mod discovery & active mods list
# ---------------------------------------------------------------------------

@dataclass
class OtherModRef:
	"""A mod's declared relation to another mod ("OtherMods" in mod.json), same meaning as in the game's Mods menu."""
	mod_id: str
	display_name: str
	min_version: str = ""
	required: bool = False
	conflict: bool = False
	priority: int = 0            # +1: the other mod must be higher in the list (higher priority), -1: lower, 0: don't care


@dataclass
class ModInfo:
	folder: str              # Path relative to the mods directory, with forward slashes (as stored by the game)
	name: str
	author: str = ""
	version: str = ""
	description: str = ""
	unique_id: str = ""      # "UniqueID" from mod.json, or the folder name (like the game)
	others: List[OtherModRef] = field(default_factory=list)


def _relaxed_json(text: str) -> str:
	"""Turn JSON as the game reads it into strict JSON: the game's reader (jsoncpp with its default settings) accepts // and
	/* */ comments and trailing commas, and lots of mods have them."""
	out = []
	i, n = 0, len(text)
	in_string = False
	while i < n:
		c = text[i]
		if in_string:
			out.append(c)
			if c == "\\" and i + 1 < n:
				out.append(text[i + 1])
				i += 1
			elif c == '"':
				in_string = False
		elif c == '"':
			in_string = True
			out.append(c)
		elif text.startswith("//", i):
			while i < n and text[i] not in "\r\n":
				i += 1
			continue
		elif text.startswith("/*", i):
			end = text.find("*/", i + 2)
			i = n if end < 0 else end + 2
			out.append(" ")
			continue
		elif c in "}]":
			# Drop a comma right before the closing bracket (with only whitespace between them)
			j = len(out) - 1
			while j >= 0 and out[j] in " \t\r\n":
				j -= 1
			if j >= 0 and out[j] == ",":
				del out[j]
			out.append(c)
		else:
			out.append(c)
		i += 1
	return "".join(out)


def load_mod_json(path: Path) -> dict:
	"""Read a mod.json as leniently as the game does: UTF-8 BOM, comments, trailing commas, and anything after the end."""
	text = path.read_text(encoding="utf-8-sig", errors="replace")
	try:
		return json.loads(text)
	except ValueError:
		pass
	value, _ = json.JSONDecoder().raw_decode(_relaxed_json(text).strip())
	return value


def mod_info_from_json(folder: str, data: dict) -> ModInfo:
	meta = data.get("Metadata", {}) if isinstance(data, dict) else {}
	if not isinstance(meta, dict):
		meta = {}
	name = meta.get("DisplayName") or meta.get("Name") or PurePosixPath(folder).name
	unique_id = str(meta.get("UniqueID") or PurePosixPath(folder).name)
	others = []
	other_mods = data.get("OtherMods") if isinstance(data, dict) else None
	if not isinstance(other_mods, dict):
		other_mods = meta.get("OtherMods")
	if isinstance(other_mods, dict):
		for mod_id, info in other_mods.items():
			if not isinstance(info, dict):
				continue
			priority = str(info.get("Priority", "")).lower()
			others.append(OtherModRef(str(mod_id), str(info.get("DisplayName") or mod_id), str(info.get("MinimumVersion", "")),
									  bool(info.get("IsRequired", False)), bool(info.get("IsConflict", False)),
									  +1 if priority == "higher" else -1 if priority == "lower" else 0))
	return ModInfo(folder, str(name), str(meta.get("Author", "")), str(meta.get("ModVersion", "")), str(meta.get("Description", "")),
				   unique_id, others)


def scan_installed_mods(mods_dir: Path, max_depth: int = 3) -> List[ModInfo]:
	"""Find installed mod folders the same way the game does (mod.json, nested up to a few levels)."""
	result: List[ModInfo] = []

	def walk(directory: Path, rel: PurePosixPath, depth: int):
		try:
			children = sorted(p for p in directory.iterdir() if p.is_dir())
		except OSError:
			return
		for child in children:
			child_rel = rel / child.name
			mod_json = child / "mod.json"
			if mod_json.is_file():
				try:
					result.append(mod_info_from_json(child_rel.as_posix(), load_mod_json(mod_json)))
				except (OSError, ValueError):
					result.append(ModInfo(child_rel.as_posix(), child.name + "  (broken mod.json)"))
			elif depth > 0:
				walk(child, child_rel, depth - 1)

	if mods_dir.is_dir():
		walk(mods_dir, PurePosixPath(), max_depth)
	return result


def read_active_mods(mods_dir: Path) -> List[str]:
	"""Active mods as stored by the game: LOWEST priority first, highest priority last."""
	path = mods_dir / ACTIVE_MODS_FILE
	if not path.is_file():
		return []
	try:
		data = json.loads(path.read_text(encoding="utf-8-sig"))
		return [m for m in data.get("ActiveMods", []) if isinstance(m, str)]
	except (OSError, ValueError):
		return []


def write_active_mods(mods_dir: Path, active: List[str]):
	mods_dir.mkdir(parents=True, exist_ok=True)
	data = {"ActiveMods": active, "UseLegacyLoading": False}
	tmp = mods_dir / (ACTIVE_MODS_FILE + ".tmp")
	tmp.write_text(json.dumps(data, indent=4), encoding="utf-8")
	os.replace(tmp, mods_dir / ACTIVE_MODS_FILE)


# ---------------------------------------------------------------------------
#  Compatibility check
# ---------------------------------------------------------------------------

@dataclass
class CheckResult:
	errors: List[str] = field(default_factory=list)
	warnings: List[str] = field(default_factory=list)
	notes: List[str] = field(default_factory=list)


def png_size(path: Path):
	try:
		with open(path, "rb") as f:
			head = f.read(24)
		if head[:8] == b"\x89PNG\r\n\x1a\n" and head[12:16] == b"IHDR":
			return struct.unpack(">II", head[16:24])
	except OSError:
		pass
	return None


def check_mod_folder(mod_root: Path) -> CheckResult:
	res = CheckResult()
	if not (mod_root / "mod.json").is_file():
		res.errors.append("No mod.json found - this doesn't look like a Sonic 3 A.I.R. mod.")
		return res
	try:
		load_mod_json(mod_root / "mod.json")
	except ValueError as e:
		res.errors.append(f"mod.json is not valid JSON ({e}). The game would ignore this mod.")
		return res

	total = audio = 0
	shaders, big_images = [], []
	for p in mod_root.rglob("*"):
		if not p.is_file():
			continue
		size = p.stat().st_size
		total += size
		ext = p.suffix.lower()
		rel = p.relative_to(mod_root).as_posix()
		if ext in (".ogg", ".wav"):
			audio += size
		elif ext in SHADER_EXTENSIONS or "/shader/" in f"/{rel.lower()}":
			shaders.append(rel)
		elif ext == ".png":
			dims = png_size(p)
			if dims and max(dims) > MAX_IMAGE_SIDE:
				big_images.append(f"{rel} ({dims[0]}x{dims[1]})")

	mb = 1024 * 1024
	if shaders:
		res.warnings.append(f"Uses {len(shaders)} custom shader file(s). The 3DS has no OpenGL, so these effects won't show "
							f"(e.g. {shaders[0]}).")
	if big_images:
		res.warnings.append(f"{len(big_images)} very large image(s), may run out of memory on 3DS: " + ", ".join(big_images[:3]))
	if audio > MAX_TOTAL_AUDIO_MB * mb:
		res.warnings.append(f"Contains {audio / mb:.0f} MB of audio. Music mods this big may stutter or run out of memory.")
	if total > MAX_TOTAL_SIZE_MB * mb:
		res.warnings.append(f"Mod is {total / mb:.0f} MB in total, which is a lot for the 3DS.")
	res.notes.append(f"Size: {total / mb:.1f} MB")
	return res


# ---------------------------------------------------------------------------
#  Installing
# ---------------------------------------------------------------------------

class PatcherError(Exception):
	pass


def _find_mod_root(directory: Path) -> Optional[Path]:
	"""The folder containing mod.json, searching a few levels deep (zips often have a wrapper folder)."""
	if (directory / "mod.json").is_file():
		return directory
	matches = sorted((p.parent for p in directory.rglob("mod.json")), key=lambda p: len(p.parts))
	matches = [m for m in matches if len(m.relative_to(directory).parts) <= 3]
	if len(matches) > 1 and len(matches[0].parts) == len(matches[1].parts):
		raise PatcherError("This archive contains several mods. Please unpack it and install them one by one.")
	return matches[0] if matches else None


def _safe_extract(zip_path: Path, target: Path):
	with zipfile.ZipFile(zip_path) as zf:
		for member in zf.infolist():
			name = member.filename.replace("\\", "/")
			parts = PurePosixPath(name).parts
			if name.startswith("/") or ".." in parts or (parts and ":" in parts[0]):
				raise PatcherError(f"Refusing unsafe path inside zip: {member.filename}")
			if member.is_dir():
				continue
			dest = target.joinpath(*parts)
			dest.parent.mkdir(parents=True, exist_ok=True)
			with zf.open(member) as src, open(dest, "wb") as out:
				shutil.copyfileobj(src, out)


def _clean_folder_name(name: str) -> str:
	keep = "".join(c if (c.isalnum() or c in " -_.()[]") else "_" for c in name).strip(" .")
	return keep or "mod"


@dataclass
class InstallResult:
	mod: ModInfo
	check: CheckResult
	replaced: bool


def _archive_kind(path: Path) -> Optional[str]:
	try:
		with open(path, "rb") as f:
			head = f.read(8)
	except OSError:
		return None
	if head.startswith(b"Rar!\x1a\x07"):
		return "RAR"
	if head.startswith(b"7z\xbc\xaf\x27\x1c"):
		return "7z"
	return None


def install_mod(source: Path, sd_root: Path, enable: bool = True, allow_errors: bool = False, progress: Optional[Progress] = None) -> InstallResult:
	mods_dir = sd_root / MODS_DIR
	source = Path(source)
	recover_interrupted(sd_root)
	with tempfile.TemporaryDirectory(prefix="s3air3ds_") as tmp:
		if source.is_file() and zipfile.is_zipfile(source):
			if progress:
				progress.start(f"Unpacking {source.name}", 0)
			_safe_extract(source, Path(tmp))
			root = _find_mod_root(Path(tmp))
			fallback_name = source.stem
		elif source.is_dir():
			root = _find_mod_root(source)
			fallback_name = source.name
		elif source.is_file() and _archive_kind(source):
			raise PatcherError(f"{source.name} is a {_archive_kind(source)} archive, which this tool can't unpack.\n"
							   "Unpack it on your computer first (with 7-Zip, for example) and use 'Add mod folder...', "
							   "or install it on the 3DS itself: the game's Mods menu can unpack .rar and .7z (GameBanana or Wi-Fi).")
		else:
			raise PatcherError(f"Not a mod .zip or folder: {source}")
		if root is None:
			raise PatcherError("No mod.json found - this doesn't look like a Sonic 3 A.I.R. mod.")

		check = check_mod_folder(root)
		if check.errors and not allow_errors:
			raise PatcherError("\n".join(check.errors))

		# Folder name: the mod's own folder name if it has one, else the zip's name
		name = root.name if root != Path(tmp) else fallback_name
		folder = _clean_folder_name(name)
		dest = mods_dir / folder
		replaced = dest.exists()

		# Copy the whole mod next to the mods folder first (the old version stays as it is), and only swap once it's complete
		_check_space(mods_dir, sum(p.stat().st_size for p in _tree_files(root)))
		incoming = sd_root / MODS_INCOMING_DIR / folder
		if incoming.exists():
			shutil.rmtree(incoming)
		try:
			_copy_tree(root, incoming, progress, f"Copying {folder}")
		except BaseException:
			shutil.rmtree(sd_root / MODS_INCOMING_DIR, ignore_errors=True)
			raise
		mods_dir.mkdir(parents=True, exist_ok=True)
		if replaced:
			old = sd_root / MODS_REPLACED_DIR / folder
			if old.exists():
				shutil.rmtree(old)
			old.parent.mkdir(parents=True, exist_ok=True)
			try:
				os.replace(dest, old)
			except OSError as e:
				shutil.rmtree(sd_root / MODS_INCOMING_DIR, ignore_errors=True)
				raise PatcherError(f"Couldn't replace the old version of '{folder}' ({e.strerror}). Is one of its files open in another program?\n"
								   "The old version is still there, unchanged.")
			try:
				os.replace(incoming, dest)
			except OSError:
				os.replace(old, dest)       # Put the old version back
				raise
			shutil.rmtree(sd_root / MODS_REPLACED_DIR, ignore_errors=True)
		else:
			os.replace(incoming, dest)
		shutil.rmtree(sd_root / MODS_INCOMING_DIR, ignore_errors=True)

	info = mod_info_from_json(folder, load_mod_json(dest / "mod.json"))
	if enable:
		active = [m for m in read_active_mods(mods_dir) if m != folder]
		active.append(folder)                # Last = highest priority
		write_active_mods(mods_dir, active)
	return InstallResult(info, check, replaced)


def set_enabled(sd_root: Path, folder: str, enabled: bool):
	mods_dir = sd_root / MODS_DIR
	if enabled and folder not in {m.folder for m in scan_installed_mods(mods_dir)}:
		raise PatcherError(f"No installed mod named '{folder}' (see 'list').")
	active = [m for m in read_active_mods(mods_dir) if m != folder]
	if enabled:
		active.append(folder)
	write_active_mods(mods_dir, active)


def move_priority(sd_root: Path, folder: str, step: int):
	"""step > 0: higher priority, step < 0: lower priority."""
	mods_dir = sd_root / MODS_DIR
	active = read_active_mods(mods_dir)
	if folder not in active:
		raise PatcherError("Only enabled mods have a priority.")
	i = active.index(folder)
	j = max(0, min(len(active) - 1, i + step))
	active.insert(j, active.pop(i))
	write_active_mods(mods_dir, active)


def remove_mod(sd_root: Path, folder: str):
	mods_dir = sd_root / MODS_DIR
	target = (mods_dir / folder).resolve()
	if mods_dir.resolve() not in target.parents:
		raise PatcherError("Invalid mod folder.")
	set_enabled(sd_root, folder, False)
	shutil.rmtree(target)


def list_mods(sd_root: Path):
	"""(enabled mods from highest to lowest priority, disabled mods)"""
	mods_dir = sd_root / MODS_DIR
	installed = {m.folder: m for m in scan_installed_mods(mods_dir)}
	active = [f for f in read_active_mods(mods_dir) if f in installed]
	enabled = [installed[f] for f in reversed(active)]
	disabled = [m for f, m in sorted(installed.items()) if f not in active]
	return enabled, disabled


# ---------------------------------------------------------------------------
#  Conflicts between mods
# ---------------------------------------------------------------------------

@dataclass
class ModProblem:
	severity: str                    # "error" (doesn't work like this) or "warning" (may not work as expected)
	text: str
	mod: str                         # Folder of the mod the problem is about
	other: Optional[str] = None      # Folder of the other installed mod involved, if any
	can_disable_other: bool = False  # Disabling "other" solves it (offered by the window / --disable-conflicting)


def _version_tuple(version: str):
	parts = []
	for piece in version.replace("-", ".").split("."):
		digits = "".join(ch for ch in piece if ch.isdigit())
		parts.append(int(digits) if digits else 0)
	return tuple(parts)


def _content_files(mod_root: Path) -> set:
	"""Files a mod replaces or adds, relative to its folder - without mod.json, files directly in the mod folder (readme,
	preview images) and scripts (scripts of different mods are all loaded, they don't replace each other by file name)."""
	files = set()
	for path in mod_root.rglob("*"):
		if not path.is_file():
			continue
		rel = path.relative_to(mod_root)
		if len(rel.parts) < 2 or rel.parts[0].lower() == "scripts":
			continue
		files.add(PurePosixPath(*rel.parts).as_posix().lower())
	return files


def find_mod_problems(sd_root: Path) -> List[ModProblem]:
	"""Checks the enabled mods against each other: declared conflicts, missing required mods, required order, minimum
	versions (all like the game's Mods menu does), and enabled mods replacing the same files (only the higher one wins)."""
	mods_dir = sd_root / MODS_DIR
	installed = {m.folder: m for m in scan_installed_mods(mods_dir)}
	active = [f for f in read_active_mods(mods_dir) if f in installed]     # Lowest priority first
	priority = {folder: index for index, folder in enumerate(active)}
	by_id = {}
	for m in installed.values():
		by_id.setdefault(m.unique_id, m)
		by_id.setdefault(PurePosixPath(m.folder).name, m)

	problems: List[ModProblem] = []
	reported_pairs = set()
	for folder in reversed(active):
		mod = installed[folder]
		for ref in mod.others:
			other = by_id.get(ref.mod_id)
			other_name = other.name if other else ref.display_name
			other_enabled = other is not None and other.folder in priority
			if other is not None and other.folder == folder:
				continue
			if ref.conflict:
				pair = frozenset((folder, other.folder)) if other else None
				if other_enabled and pair not in reported_pairs:
					reported_pairs.add(pair)
					problems.append(ModProblem("error", f'"{mod.name}" does not work together with "{other_name}" (says {mod.name}).',
											   folder, other.folder, True))
				continue
			if not other_enabled:
				if ref.required:
					if other is not None:
						problems.append(ModProblem("error", f'"{mod.name}" requires "{other_name}" - it is installed, but not enabled.', folder, other.folder))
					else:
						problems.append(ModProblem("error", f'"{mod.name}" requires "{other_name}", which is not installed.', folder))
				continue
			if ref.priority != 0:
				other_is_higher = priority[other.folder] > priority[folder]
				if other_is_higher != (ref.priority > 0):
					where = "below" if ref.priority > 0 else "above"
					problems.append(ModProblem("error" if ref.required else "warning",
											   f'"{mod.name}" needs to be placed {where} "{other_name}" in the list.', folder, other.folder))
			if ref.min_version and other.version and _version_tuple(other.version) < _version_tuple(ref.min_version):
				problems.append(ModProblem("warning", f'"{mod.name}" needs "{other_name}" version {ref.min_version} or newer (installed: {other.version}).',
										   folder, other.folder))

	# Enabled mods replacing the same files: only the one higher in the list has an effect for those files
	files = {folder: _content_files(mods_dir / folder) for folder in active}
	for i, high in enumerate(reversed(active)):
		for low in list(reversed(active))[i + 1:]:
			if frozenset((high, low)) in reported_pairs:
				continue
			shared = sorted(files[high] & files[low])
			if shared:
				example = ", ".join(shared[:3]) + (f" and {len(shared) - 3} more" if len(shared) > 3 else "")
				problems.append(ModProblem("warning", f'"{installed[high].name}" and "{installed[low].name}" both change the same files '
											f'({example}); only "{installed[high].name}" (higher in the list) is used for those.',
											high, low, True))
	return problems


def problems_for(problems: List[ModProblem], folder: str) -> List[ModProblem]:
	return [p for p in problems if p.mod == folder or p.other == folder]


def disable_conflicting(sd_root: Path, folder: str) -> List[str]:
	"""Disables the other mods that conflict with this one (declared conflicts and file overlaps). Returns their folders."""
	disabled = []
	for problem in problems_for(find_mod_problems(sd_root), folder):
		if not problem.can_disable_other:
			continue
		other = problem.other if problem.mod == folder else problem.mod
		if other and other != folder and other not in disabled:
			set_enabled(sd_root, other, False)
			disabled.append(other)
	return disabled


# ---------------------------------------------------------------------------
#  Command line
# ---------------------------------------------------------------------------

def _resolve_sd(arg: Optional[str]) -> Path:
	if arg:
		return Path(arg)
	cards = find_sd_cards()
	if len(cards) == 1:
		return cards[0]
	if not cards:
		raise PatcherError("No 3DS SD card found. Pass it with --sd (e.g. --sd E:/ or --sd /Volumes/3DS).")
	raise PatcherError("Several possible SD cards found, pick one with --sd: " + ", ".join(map(str, cards)))


def _print_check(check: CheckResult):
	for e in check.errors:
		print("  ERROR:   " + e)
	for w in check.warnings:
		print("  WARNING: " + w)
	for n in check.notes:
		print("  " + n)


def _print_problems(problems: List[ModProblem]):
	for problem in problems:
		print(("  CONFLICT: " if problem.severity == "error" else "  WARNING:  ") + problem.text)


def _handle_conflicts_cli(sd: Path, folder: str, disable_others: bool):
	problems = problems_for(find_mod_problems(sd), folder)
	if not problems:
		return
	_print_problems(problems)
	if disable_others:
		for other in disable_conflicting(sd, folder):
			print(f"  Disabled: {other}")
	elif any(p.can_disable_other for p in problems):
		print("  -> Disable the other mod with 'disable <folder>', or install/enable again with --disable-conflicting")


def _cli_progress() -> Optional[Progress]:
	"""A percentage on one line while copying, when running in a terminal."""
	if not sys.stderr.isatty():
		return None
	shown = [None]

	def show(p: Progress):
		state = (p.text, int(p.done * 100 / p.total) if p.total else None)
		if state != shown[0]:
			shown[0] = state
			print(f"\r{p.text}..." + (f" {state[1]}%" if state[1] is not None else "") + " " * 10, end="", file=sys.stderr, flush=True)
	return Progress(show)


def _cli_progress_done(progress: Optional[Progress]):
	if progress and progress.text:
		print("\r" + " " * (len(progress.text) + 20) + "\r", end="", file=sys.stderr, flush=True)
		progress.text = ""


def main_cli(argv: List[str]) -> int:
	ap = argparse.ArgumentParser(description="Install and manage Sonic 3 A.I.R. mods on a 3DS SD card.")
	ap.add_argument("--sd", help="SD card root (auto-detected if omitted)")
	sub = ap.add_subparsers(dest="cmd", required=True)
	sub.add_parser("check-sd", help="Check that the game data, ROM and sound firmware are in place")
	p = sub.add_parser("setup", help="Copy the A.I.R. data and your ROM to the SD card")
	p.add_argument("--air", help=f"The official Sonic 3 A.I.R. {REQUIRED_AIR_VERSION} download (.zip or unpacked folder)")
	p.add_argument("--rom", help="Your Sonic 3 & Knuckles ROM from Steam (any file name; searched in your Steam library if omitted)")
	sub.add_parser("list", help="List installed mods")
	sub.add_parser("conflicts", help="Check the enabled mods for conflicts with each other")
	p = sub.add_parser("install", help="Install mods from .zip files or folders")
	p.add_argument("paths", nargs="+")
	p.add_argument("--disabled", action="store_true", help="Install without enabling")
	p.add_argument("--disable-conflicting", action="store_true", help="Disable other mods that conflict with the installed one")
	p = sub.add_parser("check", help="Check a mod for 3DS compatibility without installing")
	p.add_argument("path")
	for name, hlp in [("enable", "Enable a mod (as highest priority)"), ("disable", "Disable a mod"), ("remove", "Delete a mod from the SD card"),
					  ("up", "Raise a mod's priority"), ("down", "Lower a mod's priority")]:
		p = sub.add_parser(name, help=hlp)
		p.add_argument("folder", help="Mod folder name, as shown by 'list'")
		if name == "enable":
			p.add_argument("--disable-conflicting", action="store_true", help="Disable other mods that conflict with this one")
	args = ap.parse_args(argv)

	try:
		if args.cmd == "check":
			with tempfile.TemporaryDirectory() as tmp:
				src = Path(args.path)
				if src.is_file() and zipfile.is_zipfile(src):
					_safe_extract(src, Path(tmp))
					src = Path(tmp)
				root = _find_mod_root(src)
				if root is None:
					raise PatcherError("No mod.json found - this doesn't look like a Sonic 3 A.I.R. mod.")
				check = check_mod_folder(root)
				_print_check(check)
				if not check.errors and not check.warnings:
					print("  Looks fine for the 3DS.")
				return 1 if check.errors else 0

		sd = _resolve_sd(args.sd)
		for message in recover_interrupted(sd):
			print(message)
		progress = _cli_progress()
		if args.cmd == "setup":
			if args.air:
				version = install_air_data(Path(args.air), sd, progress)
				_cli_progress_done(progress)
				print(f"Copied the A.I.R. {version} data folder to {(GAME_DIR / 'data').as_posix()}/")
			if args.rom:
				rom = Path(args.rom)
			else:
				roms = [r for r in find_steam_roms() if check_rom(r)[0]]
				rom = roms[0] if roms else None
				if rom is None and not (sd / GAME_DIR / ROM_NAME).is_file():
					print("No Steam ROM found automatically, pass it with --rom")
			if rom is not None:
				target = install_rom(rom, sd, progress)
				_cli_progress_done(progress)
				print(f"Copied ROM {rom}  ->  {(GAME_DIR / ROM_NAME).as_posix()}  ({target})")
			report = check_sd(sd)
			print("\n".join(report.lines))
			return 0 if report.ok else 1
		if args.cmd == "check-sd":
			report = check_sd(sd)
			print(f"SD card: {sd}")
			print("\n".join(report.lines))
			return 0 if report.ok else 1
		if args.cmd == "list":
			enabled, disabled = list_mods(sd)
			print("Enabled (highest priority first):")
			for m in enabled:
				print(f"  {m.folder:30}  {m.name}" + (f"  v{m.version}" if m.version else ""))
			if not enabled:
				print("  (none)")
			print("Disabled:")
			for m in disabled:
				print(f"  {m.folder:30}  {m.name}" + (f"  v{m.version}" if m.version else ""))
			if not disabled:
				print("  (none)")
		elif args.cmd == "conflicts":
			problems = find_mod_problems(sd)
			_print_problems(problems)
			if not problems:
				print("No conflicts between the enabled mods.")
			return 1 if any(p.severity == "error" for p in problems) else 0
		elif args.cmd == "install":
			for path in args.paths:
				r = install_mod(Path(path), sd, enable=not args.disabled, progress=progress)
				_cli_progress_done(progress)
				print(f"{'Updated' if r.replaced else 'Installed'}: {r.mod.name}  ->  {(MODS_DIR / r.mod.folder).as_posix()}")
				_print_check(r.check)
				if not args.disabled:
					_handle_conflicts_cli(sd, r.mod.folder, args.disable_conflicting)
		elif args.cmd == "enable":
			set_enabled(sd, args.folder, True)
			_handle_conflicts_cli(sd, args.folder, args.disable_conflicting)
		elif args.cmd == "disable":
			set_enabled(sd, args.folder, False)
		elif args.cmd == "remove":
			remove_mod(sd, args.folder)
		elif args.cmd == "up":
			move_priority(sd, args.folder, +1)
		elif args.cmd == "down":
			move_priority(sd, args.folder, -1)
	except KeyboardInterrupt:
		print("\nStopped. Whatever was being copied wasn't finished, so the old files are still in place.", file=sys.stderr)
		return 1
	except (PatcherError, OSError, zipfile.BadZipFile) as e:
		print(f"\nError: {e}", file=sys.stderr)
		return 1
	return 0


# ---------------------------------------------------------------------------
#  Window (Tkinter)
# ---------------------------------------------------------------------------

def main_gui() -> int:
	import threading
	import tkinter as tk
	import webbrowser
	from tkinter import filedialog, messagebox, ttk

	root = tk.Tk()
	root.title("Sonic 3 A.I.R. - 3DS Mod Patcher")
	root.geometry("820x480")
	root.minsize(600, 380)

	sd_var = tk.StringVar()
	cards = find_sd_cards()
	if cards:
		sd_var.set(str(cards[0]))
	status_var = tk.StringVar(value="Pick your 3DS SD card. First time? Click 'Set up game files...'. Then add mods.")
	MB = 1024 * 1024

	# Top: SD card selection (a drop-down when more than one card is plugged in)
	top = ttk.Frame(root, padding=8)
	top.pack(fill="x")
	ttk.Label(top, text="3DS SD card:").pack(side="left")
	sd_box = ttk.Combobox(top, textvariable=sd_var, values=[str(c) for c in cards])
	sd_box.pack(side="left", fill="x", expand=True, padx=6)
	sd_box.bind("<<ComboboxSelected>>", lambda e: refresh())

	# Long copies run in a thread of their own, with a progress window, so the window never shows "Not Responding"
	busy = {"running": False}

	def run_job(title: str, work, finished):
		"""work(progress) runs in the background; finished(result, error) then runs in the window's thread."""
		dialog = tk.Toplevel(root)
		dialog.title(title)
		dialog.transient(root)
		dialog.resizable(False, False)
		text_var = tk.StringVar(value="Starting...")
		ttk.Label(dialog, textvariable=text_var, padding=(14, 14, 14, 6), width=64).pack(fill="x")
		bar = ttk.Progressbar(dialog, length=440, mode="determinate", maximum=1000)
		bar.pack(padx=14, pady=(0, 8))
		ttk.Label(dialog, text="Please don't remove the SD card or close the patcher until this is done.",
				  padding=(14, 0, 14, 14)).pack()
		dialog.protocol("WM_DELETE_WINDOW", lambda: None)
		dialog.grab_set()

		lock = threading.Lock()
		state = {"text": "", "done": 0, "total": 0, "finished": False, "result": None, "error": None}

		def report(p: Progress):
			with lock:
				state.update(text=p.text, done=p.done, total=p.total)

		def worker():
			try:
				result = work(Progress(report))
				with lock:
					state["result"] = result
			except BaseException as e:      # Shown in the window's thread
				with lock:
					state["error"] = e
			with lock:
				state["finished"] = True

		def poll():
			with lock:
				text, done, total, over = state["text"], state["done"], state["total"], state["finished"]
			if total:
				bar.configure(mode="determinate")
				bar["value"] = 1000 * done / total
				text_var.set(f"{text}...  {done * 100 // total}%  ({done / MB:.1f} of {total / MB:.1f} MB)")
			else:
				bar.configure(mode="indeterminate")
				bar.step(20)
				text_var.set(f"{text}..." if text else "Working...")
			if not over:
				root.after(100, poll)
				return
			busy["running"] = False
			dialog.grab_release()
			dialog.destroy()
			finished(state["result"], state["error"])

		busy["running"] = True
		threading.Thread(target=worker, daemon=True).start()
		poll()

	def show_error(title: str, error: BaseException):
		text = str(error)
		if AIR_DOWNLOAD_URL in text:
			if messagebox.askyesno(title, text + "\n\nOpen that download in your browser?", icon="warning"):
				webbrowser.open(AIR_DOWNLOAD_URL)
		elif isinstance(error, (PatcherError, OSError, zipfile.BadZipFile)):
			messagebox.showerror(title, text)
		else:
			messagebox.showerror(title, f"Something unexpected went wrong: {type(error).__name__}: {text}")

	def on_close():
		if busy["running"] and not messagebox.askyesno("Still copying",
				"A copy to the SD card is still running.\n\nIf you close now, it won't be finished: the old files stay as they were, "
				"but the new ones won't be there. Close anyway?", icon="warning"):
			return
		root.destroy()
	root.protocol("WM_DELETE_WINDOW", on_close)

	def show_recovery(card: Path):
		try:
			messages = recover_interrupted(card)
		except OSError:
			return
		if messages:
			messagebox.showinfo("Cleaned up", "\n\n".join(messages))
			refresh()

	def browse_sd():
		path = filedialog.askdirectory(title="Select the root of your 3DS SD card")
		if path:
			sd_var.set(path)
			refresh()
	ttk.Button(top, text="Browse...", command=browse_sd).pack(side="left")

	def do_check_sd():
		if not sd_var.get():
			return
		report = check_sd(Path(sd_var.get()))
		(messagebox.showinfo if report.ok else messagebox.showwarning)("SD card check", "\n".join(report.lines))
	ttk.Button(top, text="Check SD card", command=do_check_sd).pack(side="left", padx=(6, 0))

	def do_setup():
		if not sd_var.get():
			messagebox.showwarning("No SD card", "Select your 3DS SD card first.")
			return
		card = Path(sd_var.get())
		show_recovery(card)

		# Ask for everything first, then copy it all in the background
		air_path = None
		if messagebox.askyesno("Set up game files (1/2)",
							   f"Copy the game data from the official Sonic 3 A.I.R. {REQUIRED_AIR_VERSION} download?\n\n"
							   "Download the Windows version first (sonic3air.org, or the link below). Then pick the downloaded .zip "
							   "(or choose 'No' here if the data is already on the SD card).\n\n"
							   f"{AIR_DOWNLOAD_URL}"):
			path = filedialog.askopenfilename(title=f"Select the Sonic 3 A.I.R. {REQUIRED_AIR_VERSION} download (.zip)",
											  filetypes=[("Zip archives", "*.zip"), ("All files", "*.*")])
			if not path:
				path = filedialog.askdirectory(title="Or select the unpacked Sonic 3 A.I.R. folder")
			if path:
				air_path = Path(path)

		root.config(cursor="watch")
		root.update()
		roms = [r for r in find_steam_roms() if check_rom(r)[0]]
		root.config(cursor="")
		rom = None
		if roms and messagebox.askyesno("Set up game files (2/2)", f"Found your Sonic 3 & Knuckles ROM in Steam:\n{roms[0]}\n\nCopy it to the SD card?"):
			rom = roms[0]
		elif not roms:
			messagebox.showinfo("Set up game files (2/2)",
								"Now pick your Sonic 3 & Knuckles ROM from Steam.\nIts file name doesn't matter, it gets renamed automatically.")
		if rom is None:
			path = filedialog.askopenfilename(title="Select your Sonic 3 & Knuckles ROM (Steam)", filetypes=[("ROM files", "*.bin *.md *.gen"), ("All files", "*.*")])
			if path:
				rom = Path(path)

		def work(progress: Progress):
			done = []
			if air_path is not None:
				version = install_air_data(air_path, card, progress)
				done.append(f"Copied the A.I.R. {version} data.")
			if rom is not None:
				install_rom(rom, card, progress)
				done.append(f"Copied the ROM as {ROM_NAME}.")
			return done

		def finished(done, error):
			if error is not None:
				show_error("Set up game files", error)
			report = check_sd(card)
			(messagebox.showinfo if report.ok else messagebox.showwarning)("Set up game files", "\n".join((done or []) + ([""] if done else []) + report.lines))
			refresh()

		if air_path is None and rom is None:
			finished([], None)
		else:
			run_job("Setting up the game files", work, finished)
	ttk.Button(top, text="Set up game files...", command=do_setup).pack(side="left", padx=(6, 0))

	# Middle: mod list
	mid = ttk.Frame(root, padding=(8, 0))
	mid.pack(fill="both", expand=True)
	# Pack the button column first, so the list can't squeeze it out of the window
	side = ttk.Frame(mid, padding=(8, 0, 0, 0))
	side.pack(side="right", fill="y")

	tree = ttk.Treeview(mid, columns=("state", "name", "author", "version"), show="headings", selectmode="browse")
	for col, title, width, stretch in [("state", "State", 80, False), ("name", "Mod", 220, True), ("author", "Author", 120, True), ("version", "Version", 70, False)]:
		tree.heading(col, text=title)
		tree.column(col, width=width, minwidth=50, anchor="w", stretch=stretch)
	scroll = ttk.Scrollbar(mid, orient="vertical", command=tree.yview)
	scroll.pack(side="right", fill="y")
	tree.pack(side="left", fill="both", expand=True)
	tree.configure(yscrollcommand=scroll.set)

	def sd() -> Optional[Path]:
		if not sd_var.get():
			messagebox.showwarning("No SD card", "Select your 3DS SD card first.")
			return None
		return Path(sd_var.get())

	def selected() -> Optional[str]:
		sel = tree.selection()
		return sel[0] if sel else None

	def refresh(select: Optional[str] = None):
		tree.delete(*tree.get_children())
		if not sd_var.get():
			return
		enabled, disabled = list_mods(Path(sd_var.get()))
		problem_folders = set()
		for problem in find_mod_problems(Path(sd_var.get())):
			problem_folders.add(problem.mod)
			if problem.other:
				problem_folders.add(problem.other)
		for i, m in enumerate(enabled):
			label = "ON  (top)" if i == 0 else "ON"
			if m.folder in problem_folders:
				label += " \u26a0"
			tree.insert("", "end", iid=m.folder, values=(label, m.name, m.author, m.version))
		for m in disabled:
			tree.insert("", "end", iid=m.folder, values=("off", m.name, m.author, m.version))
		if select and tree.exists(select):
			tree.selection_set(select)
		conflict_text = f" \u26a0 {len(problem_folders)} mods with conflicts - see 'Check conflicts'." if problem_folders else ""
		status_var.set(f"{len(enabled)} enabled, {len(disabled)} disabled. Enabled mods higher in the list win when they change the same thing.{conflict_text}")

	def guarded(fn):
		def wrapper():
			try:
				fn()
			except (PatcherError, OSError, zipfile.BadZipFile) as e:
				messagebox.showerror("Error", str(e))
		return wrapper

	def resolve_conflicts(folder: str):
		"""After enabling a mod: report its conflicts and offer to disable the other mod for each one."""
		card = sd()
		if not card:
			return
		handled = set()
		names = {m.folder: m.name for m in scan_installed_mods(card / MODS_DIR)}
		for problem in problems_for(find_mod_problems(card), folder):
			other = problem.other if problem.mod == folder else problem.mod
			if problem.can_disable_other and other and other != folder:
				if other in handled:
					continue
				handled.add(other)
				title = "Mod conflict" if problem.severity == "error" else "Possible mod conflict"
				if messagebox.askyesno(title, f"{problem.text}\n\nDisable \"{names.get(other, other)}\"?"):
					set_enabled(card, other, False)
			else:
				messagebox.showwarning("Mod problem", problem.text)
		refresh(folder)

	@guarded
	def check_conflicts():
		card = sd()
		if not card:
			return
		problems = find_mod_problems(card)
		if not problems:
			messagebox.showinfo("Check conflicts", "No conflicts between the enabled mods.")
			return
		lines = [("CONFLICT: " if p.severity == "error" else "WARNING: ") + p.text for p in problems]
		if any(p.can_disable_other for p in problems):
			lines.append("\nSelect a mod and click 'Enable / Disable' to turn one of them off.")
		messagebox.showwarning("Check conflicts", "\n\n".join(lines))

	def add_mods(paths):
		card = sd()
		if not card or not paths:
			return
		show_recovery(card)

		def work(progress: Progress):
			results, errors = [], []
			for p in paths:
				try:
					results.append(install_mod(Path(p), card, progress=progress))
				except (PatcherError, OSError, zipfile.BadZipFile) as e:
					errors.append(f"{Path(p).name}: {e}")      # One bad mod doesn't stop the others
			return results, errors

		def finished(outcome, error):
			if error is not None:
				show_error("Installing mods", error)
				refresh()
				return
			results, errors = outcome
			messages = []
			for r in results:
				msg = f"{'Updated' if r.replaced else 'Installed'}: {r.mod.name}"
				if r.check.warnings:
					msg += "\n  - " + "\n  - ".join(r.check.warnings)
				messages.append(msg)
			refresh(results[-1].mod.folder if results else None)
			if messages:
				messagebox.showinfo("Mods installed", "\n\n".join(messages))
			if errors:
				messagebox.showerror("Not installed", "\n\n".join(errors))
			for r in results:
				resolve_conflicts(r.mod.folder)

		run_job("Installing mods", work, finished)

	@guarded
	def add_zip():
		paths = filedialog.askopenfilenames(title="Select mod .zip files", filetypes=[("Mod archives", "*.zip"), ("All files", "*.*")])
		paths = list(paths)
		add_mods(paths)

	@guarded
	def add_folder():
		path = filedialog.askdirectory(title="Select an unpacked mod folder (containing mod.json)")
		if path:
			add_mods([path])

	@guarded
	def toggle():
		folder, card = selected(), sd()
		if folder and card:
			enabled_now = folder in read_active_mods(card / MODS_DIR)
			set_enabled(card, folder, not enabled_now)
			refresh(folder)
			if not enabled_now:
				resolve_conflicts(folder)

	def mover(step):
		@guarded
		def move():
			folder, card = selected(), sd()
			if folder and card:
				move_priority(card, folder, step)
				refresh(folder)
		return move

	@guarded
	def remove():
		folder, card = selected(), sd()
		if folder and card and messagebox.askyesno("Remove mod", f"Delete '{folder}' from the SD card?"):
			remove_mod(card, folder)
			refresh()

	for text, cmd in [("Add mod (.zip)...", add_zip), ("Add mod folder...", add_folder), (None, None),
					  ("Enable / Disable", toggle), ("Move up", mover(+1)), ("Move down", mover(-1)), (None, None),
					  ("Remove", remove), ("Check conflicts", check_conflicts), ("Refresh", refresh)]:
		if text is None:
			ttk.Separator(side).pack(fill="x", pady=6)
		else:
			ttk.Button(side, text=text, command=cmd).pack(fill="x", pady=2)

	tree.bind("<Double-1>", lambda e: toggle())

	sd_var.trace_add("write", lambda *a: refresh())

	ttk.Label(root, textvariable=status_var, padding=8, wraplength=680).pack(fill="x")
	if sd_var.get():
		root.after(200, lambda: show_recovery(Path(sd_var.get())))
	refresh()
	if len(cards) > 1:
		status_var.set(f"Found {len(cards)} possible 3DS SD cards - check that the right one is picked at the top.")
	root.mainloop()
	return 0


if __name__ == "__main__":
	if len(sys.argv) > 1:
		sys.exit(main_cli(sys.argv[1:]))
	try:
		sys.exit(main_gui())
	except ImportError:
		print("Tkinter is not available, use the command line instead (see --help).")
		sys.exit(main_cli(["--help"]))
