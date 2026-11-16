#!/usr/bin/env python3
"""
Sonic 3 A.I.R. - 3DS Mod Patcher

Installs, checks and orders mods for the Nintendo 3DS port of Sonic 3 A.I.R.,
directly on the 3DS SD card (or any folder that mirrors its layout).

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
REQUIRED_DATA_FILES = ["gamedata.bin", "enginedata.bin", "audiodata.bin"]

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


@dataclass
class SetupReport:
	ok: bool
	lines: List[str]


def check_sd(sd_root: Path) -> SetupReport:
	"""Check that the port, the game data and the ROM are where the 3DS build expects them."""
	game = sd_root / GAME_DIR
	lines = []
	ok = True

	def item(good: bool, text: str, fix: str = ""):
		nonlocal ok
		ok = ok and good
		lines.append(("[OK]   " if good else "[MISS] ") + text + ("" if good or not fix else f"\n         -> {fix}"))

	item((game / "sonic3air.3dsx").is_file(), "sonic3air.3dsx",
		 f"Copy sonic3air.3dsx into {GAME_DIR.as_posix()}/")
	for name in REQUIRED_DATA_FILES:
		item((game / "data" / name).is_file(), f"data/{name}",
			 "Copy the 'data' folder from the official Sonic 3 A.I.R. PC release (sonic3air.org)")
	item((game / "config.json").is_file(), "config.json",
		 "Copy config.json from the official PC release (next to the 'data' folder)")

	rom_paths = [game / ROM_NAME, game / "savedata" / ROM_NAME]
	rom = next((p for p in rom_paths if p.is_file()), None)
	if rom is None:
		item(False, ROM_NAME, f"Put your own copy of the Steam ROM in {GAME_DIR.as_posix()}/ (it is not included, for legal reasons)")
	else:
		size_mb = rom.stat().st_size / (1024 * 1024)
		item(abs(size_mb - 4.0) < 0.01, f"{ROM_NAME} ({size_mb:.1f} MB)",
			 "This doesn't look like the 4 MB Sonic 3 & Knuckles ROM from Steam")

	has_remaster = (game / "data" / "audioremaster.bin").is_file()
	lines.append(("[OK]   " if has_remaster else "[INFO] ") + "data/audioremaster.bin (optional remastered music)")
	return SetupReport(ok, lines)


# ---------------------------------------------------------------------------
#  Mod discovery & active mods list
# ---------------------------------------------------------------------------

@dataclass
class ModInfo:
	folder: str              # Path relative to the mods directory, with forward slashes (as stored by the game)
	name: str
	author: str = ""
	version: str = ""
	description: str = ""


def load_mod_json(path: Path) -> dict:
	# Mod authors sometimes save with a UTF-8 BOM or trailing commas; be lenient with the BOM at least
	text = path.read_text(encoding="utf-8-sig", errors="replace")
	return json.loads(text)


def mod_info_from_json(folder: str, data: dict) -> ModInfo:
	meta = data.get("Metadata", {}) if isinstance(data, dict) else {}
	name = meta.get("DisplayName") or meta.get("Name") or PurePosixPath(folder).name
	return ModInfo(folder, str(name), str(meta.get("Author", "")), str(meta.get("ModVersion", "")), str(meta.get("Description", "")))


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


def install_mod(source: Path, sd_root: Path, enable: bool = True, allow_errors: bool = False) -> InstallResult:
	mods_dir = sd_root / MODS_DIR
	source = Path(source)
	with tempfile.TemporaryDirectory(prefix="s3air3ds_") as tmp:
		if source.is_file() and zipfile.is_zipfile(source):
			_safe_extract(source, Path(tmp))
			root = _find_mod_root(Path(tmp))
			fallback_name = source.stem
		elif source.is_dir():
			root = _find_mod_root(source)
			fallback_name = source.name
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
		if replaced:
			shutil.rmtree(dest)
		mods_dir.mkdir(parents=True, exist_ok=True)
		shutil.copytree(root, dest)

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


def main_cli(argv: List[str]) -> int:
	ap = argparse.ArgumentParser(description="Install and manage Sonic 3 A.I.R. mods on a 3DS SD card.")
	ap.add_argument("--sd", help="SD card root (auto-detected if omitted)")
	sub = ap.add_subparsers(dest="cmd", required=True)
	sub.add_parser("check-sd", help="Check that the game, data files and ROM are in place")
	sub.add_parser("list", help="List installed mods")
	p = sub.add_parser("install", help="Install mods from .zip files or folders")
	p.add_argument("paths", nargs="+")
	p.add_argument("--disabled", action="store_true", help="Install without enabling")
	p = sub.add_parser("check", help="Check a mod for 3DS compatibility without installing")
	p.add_argument("path")
	for name, hlp in [("enable", "Enable a mod (as highest priority)"), ("disable", "Disable a mod"), ("remove", "Delete a mod from the SD card"),
					  ("up", "Raise a mod's priority"), ("down", "Lower a mod's priority")]:
		p = sub.add_parser(name, help=hlp)
		p.add_argument("folder", help="Mod folder name, as shown by 'list'")
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
		elif args.cmd == "install":
			for path in args.paths:
				r = install_mod(Path(path), sd, enable=not args.disabled)
				print(f"{'Updated' if r.replaced else 'Installed'}: {r.mod.name}  ->  {(MODS_DIR / r.mod.folder).as_posix()}")
				_print_check(r.check)
		elif args.cmd == "enable":
			set_enabled(sd, args.folder, True)
		elif args.cmd == "disable":
			set_enabled(sd, args.folder, False)
		elif args.cmd == "remove":
			remove_mod(sd, args.folder)
		elif args.cmd == "up":
			move_priority(sd, args.folder, +1)
		elif args.cmd == "down":
			move_priority(sd, args.folder, -1)
	except (PatcherError, OSError, zipfile.BadZipFile) as e:
		print(f"Error: {e}", file=sys.stderr)
		return 1
	return 0


# ---------------------------------------------------------------------------
#  Window (Tkinter)
# ---------------------------------------------------------------------------

def main_gui() -> int:
	import tkinter as tk
	from tkinter import filedialog, messagebox, ttk

	root = tk.Tk()
	root.title("Sonic 3 A.I.R. - 3DS Mod Patcher")
	root.geometry("720x480")
	root.minsize(600, 380)

	sd_var = tk.StringVar()
	cards = find_sd_cards()
	if cards:
		sd_var.set(str(cards[0]))
	status_var = tk.StringVar(value="Pick your 3DS SD card, then add mods.")

	# Top: SD card selection
	top = ttk.Frame(root, padding=8)
	top.pack(fill="x")
	ttk.Label(top, text="3DS SD card:").pack(side="left")
	ttk.Entry(top, textvariable=sd_var).pack(side="left", fill="x", expand=True, padx=6)

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
		for i, m in enumerate(enabled):
			label = "ON  (top)" if i == 0 else "ON"
			tree.insert("", "end", iid=m.folder, values=(label, m.name, m.author, m.version))
		for m in disabled:
			tree.insert("", "end", iid=m.folder, values=("off", m.name, m.author, m.version))
		if select and tree.exists(select):
			tree.selection_set(select)
		status_var.set(f"{len(enabled)} enabled, {len(disabled)} disabled. Enabled mods higher in the list win when they change the same thing.")

	def guarded(fn):
		def wrapper():
			try:
				fn()
			except (PatcherError, OSError, zipfile.BadZipFile) as e:
				messagebox.showerror("Error", str(e))
		return wrapper

	def add_mods(paths):
		card = sd()
		if not card:
			return
		messages = []
		last = None
		for p in paths:
			r = install_mod(Path(p), card)
			last = r.mod.folder
			msg = f"{'Updated' if r.replaced else 'Installed'}: {r.mod.name}"
			if r.check.warnings:
				msg += "\n  - " + "\n  - ".join(r.check.warnings)
			messages.append(msg)
		refresh(last)
		if messages:
			messagebox.showinfo("Mods installed", "\n\n".join(messages))

	@guarded
	def add_zip():
		paths = filedialog.askopenfilenames(title="Select mod .zip files", filetypes=[("Mod archives", "*.zip"), ("All files", "*.*")])
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
					  ("Remove", remove), ("Refresh", refresh)]:
		if text is None:
			ttk.Separator(side).pack(fill="x", pady=6)
		else:
			ttk.Button(side, text=text, command=cmd).pack(fill="x", pady=2)

	tree.bind("<Double-1>", lambda e: toggle())
	sd_var.trace_add("write", lambda *a: refresh())

	ttk.Label(root, textvariable=status_var, padding=8, wraplength=680).pack(fill="x")
	refresh()
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
