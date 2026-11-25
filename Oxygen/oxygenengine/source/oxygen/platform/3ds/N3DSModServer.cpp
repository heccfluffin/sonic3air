/*
*	Part of the Oxygen Engine / Sonic 3 A.I.R. software distribution.
*	Nintendo 3DS port: installing mods over Wi-Fi (see the header).
*
*	Published under the GNU GPLv3 open source software license, see license.txt
*	or https://www.gnu.org/licenses/gpl-3.0.en.html
*/

#include "oxygen/pch.h"

#if defined(PLATFORM_3DS)

#include "oxygen/platform/3ds/N3DSModServer.h"
#include "oxygen/platform/3ds/N3DSFileWriter.h"

#include <3ds.h>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <deque>
#include <set>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <poll.h>

#include "unzip.h"
#include <archive.h>
#include <archive_entry.h>
#include <memory>


namespace n3ds
{
	namespace modserver
	{
		namespace
		{
			// Like the PC mod patcher
			const int MAX_IMAGE_SIDE = 1024;
			const uint64_t MAX_TOTAL_AUDIO = 40ull * 1024 * 1024;
			const uint64_t MAX_TOTAL_SIZE = 80ull * 1024 * 1024;
			const uint64_t MAX_UPLOAD_SIZE = 1024ull * 1024 * 1024;

			const char* const PAGE_HTML = R"HTML(<!doctype html>
<html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width, initial-scale=1">
<title>Sonic 3 A.I.R. 3DS - Mods</title>
<style>
:root { color-scheme: dark; }
body { margin: 0; font: 16px/1.45 system-ui, sans-serif; background: #101830; color: #e8ecf8; }
main { max-width: 640px; margin: 0 auto; padding: 20px 16px 40px; }
h1 { font-size: 22px; margin: 4px 0 2px; } h2 { font-size: 17px; margin: 28px 0 8px; }
p { margin: 6px 0; color: #b8c2dc; }
#drop { margin-top: 16px; border: 2px dashed #5878c8; border-radius: 12px; padding: 28px 16px; text-align: center; background: #18244a; }
#drop.over { background: #22356a; border-color: #8fb0ff; }
button { font: inherit; font-weight: 600; padding: 10px 22px; border-radius: 8px; border: 0; background: #ffcc33; color: #1a1a1a; cursor: pointer; }
button:disabled { opacity: .5; cursor: default; }
#bar { height: 10px; border-radius: 5px; background: #24305a; margin-top: 14px; overflow: hidden; display: none; }
#fill { height: 100%; width: 0; background: #ffcc33; transition: width .2s; }
#msg { margin-top: 12px; min-height: 1.4em; }
.ok { color: #8fe08f; } .bad { color: #ff8f8f; } .warn { color: #ffd479; }
ul { list-style: none; padding: 0; margin: 0; }
li { padding: 8px 12px; background: #18244a; border-radius: 8px; margin-bottom: 6px; display: flex; justify-content: space-between; gap: 12px; }
.tag { font-size: 13px; color: #8fe08f; white-space: nowrap; } .tag.off { color: #8090b0; }
</style></head><body><main>
<h1>Sonic 3 A.I.R. for New 3DS</h1>
<p>Install a mod: pick its <b>.zip</b>, <b>.rar</b> or <b>.7z</b> (the download from GameBanana or wherever it's from). It goes straight to the 3DS's SD card, gets unpacked and turned on.</p>
<div id="drop"><p>Drop a mod's .zip, .rar or .7z here, or</p><p><button id="pick">Choose a file...</button></p>
<input id="file" type="file" accept=".zip,.rar,.7z,application/zip,application/x-rar-compressed,application/vnd.rar,application/x-7z-compressed" hidden>
<div id="bar"><div id="fill"></div></div><div id="msg"></div></div>
<h2>Mods on the 3DS</h2><ul id="mods"><li>...</li></ul>
<p>Turn mods on and off and change their order in the game's Mods menu. The highest one wins when two change the same thing.</p>
</main><script>
const $ = id => document.getElementById(id);
const msg = (text, cls) => { $('msg').className = cls || ''; $('msg').innerHTML = text; };
const esc = s => String(s).replace(/[&<>"]/g, c => ({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;'}[c]));
let busy = false;
async function loadMods() {
  try {
    const mods = await (await fetch('/mods', {cache: 'no-store'})).json();
    $('mods').innerHTML = mods.length ? mods.map(m => '<li><span>' + esc(m.name) + '</span><span class="tag' + (m.active ? '' : ' off') + '">' + (m.active ? 'on' : 'off') + '</span></li>').join('') : '<li>No mods yet</li>';
  } catch (e) { $('mods').innerHTML = '<li>Couldn\'t reach the 3DS - is the Wi-Fi screen still open in the game?</li>'; }
}
function upload(file) {
  if (busy || !file) return;
  if (!/\.(zip|rar|7z)$/i.test(file.name)) { msg('That\'s not a .zip, .rar or .7z file, which is what mods come as.', 'bad'); return; }
  busy = true; $('pick').disabled = true; $('bar').style.display = 'block'; $('fill').style.width = '0';
  msg('Sending ' + esc(file.name) + '...');
  const xhr = new XMLHttpRequest();
  xhr.open('POST', '/upload');
  xhr.setRequestHeader('X-File-Name', encodeURIComponent(file.name));
  xhr.upload.onprogress = e => { if (e.lengthComputable) { $('fill').style.width = (e.loaded / e.total * 100) + '%'; msg('Sending ' + esc(file.name) + '... ' + Math.floor(e.loaded / e.total * 100) + '%'); } };
  xhr.onload = () => { let r = {}; try { r = JSON.parse(xhr.responseText); } catch (e) {} if (xhr.status != 200) { done(false, r.message || 'The 3DS said no (' + xhr.status + ').', []); } else { poll(r.installs); } };
  xhr.onerror = () => done(false, 'The connection to the 3DS broke off. Is it still on the Wi-Fi screen?', []);
  xhr.send(file);
}
async function poll(installsBefore) {
  try {
    const s = await (await fetch('/status', {cache: 'no-store'})).json();
    if (s.state == 'unpacking') { $('fill').style.width = (s.filesTotal ? s.filesDone / s.filesTotal * 100 : 100) + '%'; msg('Unpacking on the 3DS... ' + s.filesDone + ' of ' + s.filesTotal + ' files (the SD card can be slow)'); }
    else if (s.state == 'ready' && s.message) { done(s.success, s.message, s.warnings || []); return; }
  } catch (e) {}
  setTimeout(() => poll(installsBefore), 600);
}
function done(ok, text, warnings) {
  busy = false; $('pick').disabled = false; $('bar').style.display = 'none'; $('file').value = '';
  msg(esc(text) + warnings.map(w => '<br><span class="warn">' + esc(w) + '</span>').join(''), ok ? 'ok' : 'bad');
  loadMods();
  setTimeout(loadMods, 1500);   // The game turns the new mod on a moment later
}
$('pick').onclick = () => $('file').click();
$('file').onchange = () => upload($('file').files[0]);
const drop = $('drop');
drop.ondragover = e => { e.preventDefault(); drop.classList.add('over'); };
drop.ondragleave = () => drop.classList.remove('over');
drop.ondrop = e => { e.preventDefault(); drop.classList.remove('over'); upload(e.dataTransfer.files[0]); };
loadMods();
setInterval(() => { if (!busy) loadMods(); }, 5000);
</script></body></html>
)HTML";

			struct Shared
			{
				LightLock mLock;
				Status mStatus;
				std::deque<std::string> mInstalled;
				std::string mModsJson = "[]";
			};
			Shared gShared;
			bool gLockReady = false;

			std::string gModsDirectory;
			std::string gWorkDirectory;
			volatile bool gStopRequested = false;
			volatile bool gThreadRunning = false;
			int gListenSocket = -1;

			struct Locked
			{
				Locked()  { LightLock_Lock(&gShared.mLock); }
				~Locked() { LightLock_Unlock(&gShared.mLock); }
			};

			// --- Files ---

			void createDirectories(const std::string& path)
			{
				// Each level after the device name ("sdmc:/"); errors are ignored, mostly they exist already
				const size_t start = path.find(":/");
				for (size_t pos = path.find('/', (start == std::string::npos) ? 1 : start + 2); pos != std::string::npos; pos = path.find('/', pos + 1))
					mkdir(path.substr(0, pos).c_str(), 0777);
			}

			// Like "createDirectories", but each folder only once per unpacking: on a slow SD card every call takes a while,
			// and a mod with hundreds of files would otherwise ask for the same few folders over and over
			void createDirectoriesOnce(const std::string& path, std::set<std::string>& created)
			{
				const size_t start = path.find(":/");
				for (size_t pos = path.find('/', (start == std::string::npos) ? 1 : start + 2); pos != std::string::npos; pos = path.find('/', pos + 1))
				{
					std::string folder = path.substr(0, pos);
					if (created.insert(folder).second)
						mkdir(folder.c_str(), 0777);
				}
			}

			// The zip is read with a big buffer: minizip asks for a few bytes at a time, each a request to the SD card otherwise
			voidpf ZCALLBACK openZipBuffered(voidpf, const void* filename, int)
			{
				FILE* file = fopen((const char*)filename, "rb");
				if (nullptr != file)
					setvbuf(file, nullptr, _IOFBF, 32 * 1024);
				return file;
			}

			uint32_t msSince(uint64_t startTicks)
			{
				return (uint32_t)((svcGetSystemTick() - startTicks) * 1000 / SYSCLOCK_ARM11);
			}

			void removeRecursively(const std::string& path)
			{
				struct stat info;
				if (stat(path.c_str(), &info) != 0)
					return;
				if (S_ISDIR(info.st_mode))
				{
					std::vector<std::string> names;
					if (DIR* dir = opendir(path.c_str()))
					{
						while (const dirent* entry = readdir(dir))
						{
							if (strcmp(entry->d_name, ".") != 0 && strcmp(entry->d_name, "..") != 0)
								names.emplace_back(entry->d_name);
						}
						closedir(dir);
					}
					for (const std::string& name : names)
						removeRecursively(path + "/" + name);
					rmdir(path.c_str());
				}
				else
				{
					unlink(path.c_str());
				}
			}

			std::string lowercase(std::string text)
			{
				for (char& c : text)
					c = (char)tolower((unsigned char)c);
				return text;
			}

			bool endsWith(const std::string& text, const char* suffix)
			{
				const size_t length = strlen(suffix);
				return text.length() >= length && text.compare(text.length() - length, length, suffix) == 0;
			}

			// The mod's name from its mod.json ("Metadata" -> "Name"), without a JSON library: good enough for a list
			std::string readModName(const std::string& modJsonPath, const std::string& fallback)
			{
				FILE* file = fopen(modJsonPath.c_str(), "rb");
				if (nullptr == file)
					return fallback;
				std::string content;
				char buffer[1024];
				size_t count;
				while ((count = fread(buffer, 1, sizeof(buffer), file)) > 0 && content.size() < 64 * 1024)
					content.append(buffer, count);
				fclose(file);

				size_t pos = content.find("\"Name\"");
				if (pos == std::string::npos)
					return fallback;
				pos = content.find(':', pos);
				pos = (pos == std::string::npos) ? pos : content.find('"', pos);
				if (pos == std::string::npos)
					return fallback;
				std::string name;
				for (size_t i = pos + 1; i < content.size() && content[i] != '"' && name.size() < 80; ++i)
				{
					if (content[i] == '\\' && i + 1 < content.size())
						++i;
					name += content[i];
				}
				return name.empty() ? fallback : name;
			}

			// --- Unpacking (in the background file writer's thread, with the SD card to itself) ---

			void setUnpackProgress(int done, int total)
			{
				Locked lock;
				gShared.mStatus.mFilesDone = done;
				gShared.mStatus.mFilesTotal = total;
			}

			// Reads an archive's entries one after another: .zip with minizip, .rar and .7z with libarchive
			class ArchiveReader
			{
			public:
				virtual ~ArchiveReader() {}
				virtual bool open(const std::string& path) = 0;
				virtual bool nextEntry(std::string& outPath, bool& outIsFile, bool& outIsOther) = 0;		// false at the end, or on an error
				virtual int read(uint8_t* buffer, size_t size) = 0;		// From the current entry: 0 at its end, < 0 on an error
				virtual bool closeEntry() = 0;			// false if the data was damaged
				std::string mError;						// What went wrong, if anything
			};

			class ZipReader : public ArchiveReader
			{
			public:
				~ZipReader() override  { if (nullptr != mZip) unzClose(mZip); }

				bool open(const std::string& path) override
				{
					zlib_filefunc64_def fileFunctions;
					fill_fopen64_filefunc(&fileFunctions);
					fileFunctions.zopen64_file = &openZipBuffered;
					mZip = unzOpen2_64(path.c_str(), &fileFunctions);
					return (nullptr != mZip);
				}

				bool nextEntry(std::string& outPath, bool& outIsFile, bool& outIsOther) override
				{
					closeEntry();
					const int status = mStarted ? unzGoToNextFile(mZip) : unzGoToFirstFile(mZip);
					mStarted = true;
					if (status != UNZ_OK)
						return false;
					char name[512];
					unz_file_info64 info;
					if (unzGetCurrentFileInfo64(mZip, &info, name, sizeof(name), nullptr, 0, nullptr, 0) != UNZ_OK)
					{
						mError = "Couldn't read the list of files in it.";
						return false;
					}
					outPath = name;
					outIsFile = !outPath.empty() && outPath.back() != '/' && outPath.back() != '\\';
					outIsOther = false;
					return true;
				}

				int read(uint8_t* buffer, size_t size) override
				{
					if (!mEntryOpen)
					{
						if (unzOpenCurrentFile(mZip) != UNZ_OK)
							return -1;
						mEntryOpen = true;
					}
					return unzReadCurrentFile(mZip, buffer, (unsigned)size);
				}

				bool closeEntry() override
				{
					if (!mEntryOpen)
						return true;
					mEntryOpen = false;
					return (unzCloseCurrentFile(mZip) == UNZ_OK);		// Checks the CRC
				}

			private:
				unzFile mZip = nullptr;
				bool mStarted = false;
				bool mEntryOpen = false;
			};

			class LibarchiveReader : public ArchiveReader
			{
			public:
				~LibarchiveReader() override  { if (nullptr != mArchive) archive_read_free(mArchive); }

				bool open(const std::string& path) override
				{
					mArchive = archive_read_new();
					if (nullptr == mArchive)
						return false;
					archive_read_support_format_7zip(mArchive);
					archive_read_support_format_rar(mArchive);
					archive_read_support_format_rar5(mArchive);
					archive_read_support_filter_all(mArchive);
					if (archive_read_open_filename(mArchive, path.c_str(), 64 * 1024) != ARCHIVE_OK)
					{
						takeError();
						return false;
					}
					return true;
				}

				bool nextEntry(std::string& outPath, bool& outIsFile, bool& outIsOther) override
				{
					archive_entry* entry = nullptr;
					const int status = archive_read_next_header(mArchive, &entry);
					if (status == ARCHIVE_EOF)
						return false;
					if (status != ARCHIVE_OK && status != ARCHIVE_WARN)
					{
						takeError();
						return false;
					}
					const char* name = archive_entry_pathname_utf8(entry);
					if (nullptr == name)
						name = archive_entry_pathname(entry);
					outPath = (nullptr != name) ? name : "";
					const mode_t type = archive_entry_filetype(entry);
					outIsFile = (type == AE_IFREG);
					outIsOther = (type != AE_IFREG && type != AE_IFDIR);		// Links and such: not in a mod
					if (type == AE_IFDIR && !outPath.empty() && outPath.back() != '/')
						outPath += '/';
					return true;
				}

				int read(uint8_t* buffer, size_t size) override
				{
					const la_ssize_t bytes = archive_read_data(mArchive, buffer, size);
					if (bytes < 0)
						takeError();
					return (int)bytes;
				}

				bool closeEntry() override  { return true; }		// libarchive checks the data while reading

			private:
				void takeError()
				{
					const char* text = (nullptr != mArchive) ? archive_error_string(mArchive) : nullptr;
					mError = (nullptr != text) ? text : "unknown error";
				}

				archive* mArchive = nullptr;
			};

			enum class ArchiveType { UNKNOWN, ZIP, RAR, SEVENZIP };

			// By the first bytes of the file, not its name
			ArchiveType getArchiveType(const std::string& path)
			{
				uint8_t magic[8] = { 0 };
				FILE* file = fopen(path.c_str(), "rb");
				if (nullptr == file)
					return ArchiveType::UNKNOWN;
				const size_t bytes = fread(magic, 1, sizeof(magic), file);
				fclose(file);
				if (bytes >= 4 && magic[0] == 'P' && magic[1] == 'K' && (magic[2] == 3 || magic[2] == 5) && (magic[3] == 4 || magic[3] == 6))
					return ArchiveType::ZIP;
				if (bytes >= 6 && memcmp(magic, "Rar!\x1a\x07", 6) == 0)
					return ArchiveType::RAR;
				if (bytes >= 6 && memcmp(magic, "7z\xbc\xaf\x27\x1c", 6) == 0)
					return ArchiveType::SEVENZIP;
				return ArchiveType::UNKNOWN;
			}

			std::unique_ptr<ArchiveReader> createReader(ArchiveType type)
			{
				if (type == ArchiveType::ZIP)
					return std::make_unique<ZipReader>();
				return std::make_unique<LibarchiveReader>();
			}

			InstallResult unpack(const std::string& zipPath, const std::string& uploadName, const std::string& modsDirectory, const std::string& installDir, const std::function<void(int, int)>& progress)
			{
				InstallResult result;
				const uint64_t startTicks = svcGetSystemTick();
				const ArchiveType type = getArchiveType(zipPath);
				if (type == ArchiveType::UNKNOWN)
				{
					result.mMessage = "This isn't a .zip, .rar or .7z file the 3DS can read (or it got cut off on the way).";
					return result;
				}
				const char* kind = (type == ArchiveType::ZIP) ? "zip" : (type == ArchiveType::RAR) ? "rar" : "7z";
				const std::string memoryHint = (type == ArchiveType::ZIP) ? std::string() : std::string(" If it's big, the 3DS may not have the memory for unpacking it: unpack it on a PC and send it as .zip instead.");

				// All entries first: where's mod.json?
				std::vector<std::string> entries;
				{
					std::unique_ptr<ArchiveReader> reader = createReader(type);
					if (!reader->open(zipPath))
					{
						result.mMessage = std::string("Couldn't open the ") + kind + " file" + (reader->mError.empty() ? "" : " (" + reader->mError + ")") + "." + memoryHint;
						return result;
					}
					std::string path;
					bool isFile = false;
					bool isOther = false;
					while (reader->nextEntry(path, isFile, isOther))
					{
						std::replace(path.begin(), path.end(), '\\', '/');
						if (isOther)
						{
							result.mMessage = "Refused: the " + std::string(kind) + " file has a link or something else that isn't a file in it (" + path + ").";
							return result;
						}
						entries.push_back(path);
					}
					if (!reader->mError.empty())
					{
						result.mMessage = "Couldn't read the list of files in the " + std::string(kind) + " file (" + reader->mError + ")." + memoryHint;
						return result;
					}
				}

				std::string root;
				std::string error;
				if (!findModRoot(entries, root, error))
				{
					result.mMessage = error;
					return result;
				}
				for (const std::string& entry : entries)
				{
					if (!isSafeZipPath(entry))
					{
						result.mMessage = "Refused: the " + std::string(kind) + " file has an unsafe path in it (" + entry + ").";
						return result;
					}
				}

				// The folder name: the mod's own folder if it has one, else the archive's name
				std::string folderName;
				if (!root.empty())
				{
					const std::string withoutSlash = root.substr(0, root.size() - 1);
					const size_t slash = withoutSlash.rfind('/');
					folderName = (slash == std::string::npos) ? withoutSlash : withoutSlash.substr(slash + 1);
				}
				else
				{
					folderName = uploadName;
					const std::string lower = lowercase(folderName);
					if (endsWith(lower, ".zip") || endsWith(lower, ".rar"))
						folderName.resize(folderName.size() - 4);
					else if (endsWith(lower, ".7z"))
						folderName.resize(folderName.size() - 3);
				}
				folderName = cleanFolderName(folderName);

				// Unpack into a folder of its own first ("installDir", ending with '/'); only a complete mod takes the place of an older version
				removeRecursively(installDir.substr(0, installDir.size() - 1));
				std::set<std::string> createdFolders;
				const uint32_t readMs = msSince(startTicks);
				const uint64_t writeTicks = svcGetSystemTick();
				createDirectoriesOnce(installDir, createdFolders);

				int total = 0;
				for (const std::string& entry : entries)
				{
					if (entry.compare(0, root.size(), root) == 0 && entry.size() > root.size() && entry.back() != '/')
						++total;
				}
				if (progress)
					progress(0, total);

				std::vector<uint8_t> buffer(32 * 1024);
				uint64_t totalSize = 0;
				uint64_t audioSize = 0;
				int shaders = 0;
				std::string firstShader;
				std::vector<std::string> bigImages;
				int done = 0;
				bool success = true;
				std::unique_ptr<ArchiveReader> reader = createReader(type);
				if (!reader->open(zipPath))
				{
					result.mMessage = std::string("Couldn't open the ") + kind + " file again.";
					success = false;
				}
				std::string path;
				bool isFile = false;
				bool isOther = false;
				while (success && reader->nextEntry(path, isFile, isOther))
				{
					std::replace(path.begin(), path.end(), '\\', '/');
					if (!isFile || path.compare(0, root.size(), root) != 0 || path.size() <= root.size())
						continue;
					const std::string relative = path.substr(root.size());
					const std::string target = installDir + relative;
					createDirectoriesOnce(target, createdFolders);

					FILE* out = fopen(target.c_str(), "wb");
					if (nullptr == out)
					{
						result.mMessage = "Couldn't write " + relative + " to the SD card.";
						success = false;
						break;
					}
					setvbuf(out, nullptr, _IOFBF, 32 * 1024);
					uint8_t header[24] = { 0 };
					size_t headerBytes = 0;
					uint64_t size = 0;
					int bytes;
					bool writeFailed = false;
					while ((bytes = reader->read(buffer.data(), buffer.size())) > 0)
					{
						if (headerBytes < sizeof(header))
						{
							const size_t copy = std::min(sizeof(header) - headerBytes, (size_t)bytes);
							memcpy(header + headerBytes, buffer.data(), copy);
							headerBytes += copy;
						}
						if (fwrite(buffer.data(), 1, (size_t)bytes, out) != (size_t)bytes)
						{
							writeFailed = true;
							break;
						}
						size += (uint64_t)bytes;
					}
					const bool closedOut = (fclose(out) == 0);
					const bool dataOk = reader->closeEntry() && (bytes >= 0);
					if (writeFailed || !closedOut || !dataOk)
					{
						result.mMessage = (writeFailed || !closedOut) ? "Writing " + relative + " failed - is the SD card full?" :
										  relative + " in the " + kind + " file is damaged" + (reader->mError.empty() ? "" : " (" + reader->mError + ")") + "." + memoryHint;
						success = false;
						break;
					}

					// What the 3DS might struggle with
					totalSize += size;
					const std::string lower = lowercase(relative);
					if (endsWith(lower, ".ogg") || endsWith(lower, ".wav"))
					{
						audioSize += size;
					}
					else if (endsWith(lower, ".glsl") || endsWith(lower, ".shader") || endsWith(lower, ".vert") || endsWith(lower, ".frag") ||
							 endsWith(lower, ".vs") || endsWith(lower, ".fs") || ("/" + lower).find("/shader/") != std::string::npos)
					{
						if (shaders++ == 0)
							firstShader = relative;
					}
					else if (endsWith(lower, ".png") && headerBytes >= 24 && memcmp(header, "\x89PNG\r\n\x1a\n", 8) == 0 && memcmp(header + 12, "IHDR", 4) == 0)
					{
						const uint32_t width = ((uint32_t)header[16] << 24) | ((uint32_t)header[17] << 16) | ((uint32_t)header[18] << 8) | header[19];
						const uint32_t height = ((uint32_t)header[20] << 24) | ((uint32_t)header[21] << 16) | ((uint32_t)header[22] << 8) | header[23];
						if ((int)std::max(width, height) > MAX_IMAGE_SIDE && bigImages.size() < 3)
							bigImages.push_back(relative + " (" + std::to_string(width) + "x" + std::to_string(height) + ")");
					}
					++done;
					if (progress)
						progress(done, total);
				}
				if (success && !reader->mError.empty())
				{
					result.mMessage = "Unpacking the " + std::string(kind) + " file failed (" + reader->mError + ")." + memoryHint;
					success = false;
				}
				reader.reset();

				if (!success)
				{
					removeRecursively(installDir.substr(0, installDir.size() - 1));
					return result;
				}

				const uint32_t writeMs = msSince(writeTicks);
				const uint64_t moveTicks = svcGetSystemTick();

				// In place: an older version of the same mod goes
				const std::string destination = modsDirectory + folderName;
				struct stat existing;
				const bool replaced = (stat(destination.c_str(), &existing) == 0);
				if (replaced)
					removeRecursively(destination);
				createDirectories(modsDirectory);
				if (rename(installDir.substr(0, installDir.size() - 1).c_str(), destination.c_str()) != 0)
				{
					removeRecursively(installDir.substr(0, installDir.size() - 1));
					result.mMessage = "Couldn't move the mod into the mods folder.";
					return result;
				}

				const std::string modName = readModName(destination + "/mod.json", folderName);
				const uint32_t totalMs = msSince(startTicks);
				if (totalMs >= 1000)
				{
					// To see where a slow unpacking spends its time (the SD card, or the system core the work runs on)
					RMX_LOG_INFO("Unpacking took " << totalMs << " ms: reading the " << kind << " file " << readMs << " ms, " << done << " files and " << createdFolders.size() <<
								 " folders " << writeMs << " ms, moving into place " << msSince(moveTicks) << " ms");
				}
				result.mSuccess = true;
				result.mFolder = folderName;
				result.mMessage = std::string(replaced ? "Updated " : "Installed ") + "\"" + modName + "\" and turned it on. (" +
								  std::to_string(done) + " files, " + std::to_string((int)(totalSize / (1024 * 1024))) + "." + std::to_string((int)(totalSize % (1024 * 1024) * 10 / (1024 * 1024))) + " MB)";
				if (shaders > 0)
					result.mWarnings.push_back("Uses " + std::to_string(shaders) + " custom shader file(s). The 3DS has no OpenGL, so these effects won't show (e.g. " + firstShader + ").");
				if (!bigImages.empty())
				{
					std::string list;
					for (const std::string& image : bigImages)
						list += (list.empty() ? "" : ", ") + image;
					result.mWarnings.push_back("Very large image(s), may run out of memory on the 3DS: " + list);
				}
				if (audioSize > MAX_TOTAL_AUDIO)
					result.mWarnings.push_back("Contains " + std::to_string((int)(audioSize / (1024 * 1024))) + " MB of audio. Music mods this big may stutter or run out of memory.");
				if (totalSize > MAX_TOTAL_SIZE)
					result.mWarnings.push_back("The mod is " + std::to_string((int)(totalSize / (1024 * 1024))) + " MB in total, which is a lot for the 3DS.");
				return result;
			}

			// --- HTTP ---

			bool sendAll(int socket, const char* data, size_t size)
			{
				while (size > 0)
				{
					const ssize_t sent = send(socket, data, size, 0);
					if (sent <= 0)
					{
						if (sent < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
						{
							svcSleepThread(2 * 1000 * 1000);
							continue;
						}
						return false;
					}
					data += sent;
					size -= (size_t)sent;
				}
				return true;
			}

			void sendResponse(int socket, int code, const char* contentType, const std::string& body)
			{
				const char* reason = (code == 200) ? "OK" : (code == 400) ? "Bad Request" : (code == 404) ? "Not Found" : (code == 409) ? "Conflict" : (code == 413) ? "Payload Too Large" : "Error";
				char header[256];
				snprintf(header, sizeof(header), "HTTP/1.1 %d %s\r\nContent-Type: %s\r\nContent-Length: %u\r\nCache-Control: no-store\r\nConnection: close\r\n\r\n",
						 code, reason, contentType, (unsigned)body.size());
				sendAll(socket, header, strlen(header));
				sendAll(socket, body.data(), body.size());
			}

			void sendJson(int socket, int code, const std::string& json)
			{
				sendResponse(socket, code, "application/json; charset=utf-8", json);
			}

			std::string statusJson()
			{
				Locked lock;
				const Status& status = gShared.mStatus;
				std::string warnings;
				for (const std::string& warning : status.mLastWarnings)
					warnings += (warnings.empty() ? "\"" : ",\"") + escapeJson(warning) + "\"";
				const char* state = (status.mState == Status::State::RECEIVING) ? "receiving" : (status.mState == Status::State::UNPACKING) ? "unpacking" : "ready";
				return std::string("{\"state\":\"") + state + "\",\"filesDone\":" + std::to_string(status.mFilesDone) + ",\"filesTotal\":" + std::to_string(status.mFilesTotal) +
					   ",\"installs\":" + std::to_string(status.mInstalls) + ",\"success\":" + (status.mLastSuccess ? "true" : "false") +
					   ",\"message\":\"" + escapeJson(status.mLastMessage) + "\",\"warnings\":[" + warnings + "]}";
			}

			std::string urlDecode(const std::string& text)
			{
				std::string result;
				for (size_t i = 0; i < text.size(); ++i)
				{
					if (text[i] == '%' && i + 2 < text.size() && isxdigit((unsigned char)text[i + 1]) && isxdigit((unsigned char)text[i + 2]))
					{
						result += (char)strtol(text.substr(i + 1, 2).c_str(), nullptr, 16);
						i += 2;
					}
					else
					{
						result += (text[i] == '+') ? ' ' : text[i];
					}
				}
				return result;
			}

			// Waits until the socket has something to read (or the time is up, or the server is stopping)
			bool waitReadable(int socket, int timeoutMs)
			{
				for (int waited = 0; waited < timeoutMs && !gStopRequested; waited += 100)
				{
					pollfd fd = { socket, POLLIN, 0 };
					const int result = poll(&fd, 1, 100);
					if (result > 0)
						return true;
					if (result < 0 && errno != EINTR)
						return false;
				}
				return false;
			}

			void handleUpload(int socket, const std::string& fileName, uint64_t contentLength, std::string alreadyReceived)
			{
				{
					Locked lock;
					if (gShared.mStatus.mState != Status::State::READY)
					{
						sendJson(socket, 409, "{\"message\":\"The 3DS is still busy with the last mod, try again in a moment.\"}");
						return;
					}
					gShared.mStatus.mState = Status::State::RECEIVING;
					gShared.mStatus.mFileName = fileName;
					gShared.mStatus.mBytes = 0;
					gShared.mStatus.mTotalBytes = contentLength;
					gShared.mStatus.mFilesDone = gShared.mStatus.mFilesTotal = 0;
				}
				const auto backToReady = [](bool success, const std::string& message)
				{
					Locked lock;
					gShared.mStatus.mState = Status::State::READY;
					gShared.mStatus.mLastSuccess = success;
					gShared.mStatus.mLastMessage = message;
					gShared.mStatus.mLastWarnings.clear();
				};

				// Straight to the SD card, so even big mods don't need much memory
				const std::string zipPath = gWorkDirectory + "wifi_upload.bin";		// The kind of archive is told by its first bytes
				createDirectories(zipPath);
				FILE* file = fopen(zipPath.c_str(), "wb");
				if (nullptr == file)
				{
					backToReady(false, "Couldn't write to the SD card.");
					sendJson(socket, 500, "{\"message\":\"Couldn't write to the SD card.\"}");
					return;
				}
				uint64_t received = 0;
				bool ok = true;
				if (!alreadyReceived.empty())
				{
					const size_t take = (size_t)std::min<uint64_t>(alreadyReceived.size(), contentLength);
					ok = (fwrite(alreadyReceived.data(), 1, take, file) == take);
					received += take;
				}
				std::vector<char> buffer(32 * 1024);
				while (ok && received < contentLength)
				{
					if (!waitReadable(socket, 20000))
					{
						ok = false;
						break;
					}
					const ssize_t bytes = recv(socket, buffer.data(), (size_t)std::min<uint64_t>(buffer.size(), contentLength - received), 0);
					if (bytes <= 0)
					{
						if (bytes < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
							continue;
						ok = false;
						break;
					}
					if (fwrite(buffer.data(), 1, (size_t)bytes, file) != (size_t)bytes)
					{
						ok = false;
						break;
					}
					received += (uint64_t)bytes;
					Locked lock;
					gShared.mStatus.mBytes = received;
				}
				const bool closed = (fclose(file) == 0);
				if (!ok || !closed)
				{
					unlink(zipPath.c_str());
					const std::string message = gStopRequested ? "Stopped: the Wi-Fi screen was closed on the 3DS." : "The upload broke off (or the SD card is full).";
					backToReady(false, message);
					sendJson(socket, 500, "{\"message\":\"" + escapeJson(message) + "\"}");
					return;
				}

				int installsBefore;
				{
					Locked lock;
					gShared.mStatus.mState = Status::State::UNPACKING;
					installsBefore = gShared.mStatus.mInstalls;
				}
				sendJson(socket, 200, "{\"installs\":" + std::to_string(installsBefore) + "}");
				RMX_LOG_INFO("Mods over Wi-Fi: received " << fileName << " (" << received << " bytes), unpacking");

				// Unpacking in the background file writer (the SD card's busy enough as it is), reported back on the main thread
				auto result = std::make_shared<InstallResult>();
				const std::string uploadName = fileName;
				n3ds::filewriter::runInBackground("unpacking a mod", [result, zipPath, uploadName]()
				{
					*result = installZip(zipPath, uploadName, gModsDirectory, gWorkDirectory + "wifi_install/", &setUnpackProgress);
					unlink(zipPath.c_str());
				},
				[result]()
				{
					RMX_LOG_INFO("Mods over Wi-Fi: " << result->mMessage);
					Locked lock;
					gShared.mStatus.mState = Status::State::READY;
					gShared.mStatus.mLastSuccess = result->mSuccess;
					gShared.mStatus.mLastMessage = result->mMessage;
					gShared.mStatus.mLastWarnings = result->mWarnings;
					if (result->mSuccess)
					{
						++gShared.mStatus.mInstalls;
						gShared.mInstalled.push_back(result->mFolder);
					}
				});
			}

			void handleConnection(int socket)
			{
				// The request line and headers
				std::string request;
				char buffer[2048];
				size_t headerEnd = std::string::npos;
				while (headerEnd == std::string::npos && request.size() < 16 * 1024)
				{
					if (!waitReadable(socket, 10000))
						return;
					const ssize_t bytes = recv(socket, buffer, sizeof(buffer), 0);
					if (bytes <= 0)
					{
						if (bytes < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
							continue;
						return;
					}
					request.append(buffer, (size_t)bytes);
					headerEnd = request.find("\r\n\r\n");
				}
				if (headerEnd == std::string::npos)
					return;

				const size_t lineEnd = request.find("\r\n");
				const std::string requestLine = request.substr(0, lineEnd);
				const size_t space1 = requestLine.find(' ');
				const size_t space2 = requestLine.find(' ', space1 + 1);
				if (space1 == std::string::npos || space2 == std::string::npos)
					return;
				const std::string method = requestLine.substr(0, space1);
				std::string path = requestLine.substr(space1 + 1, space2 - space1 - 1);
				path = path.substr(0, path.find('?'));

				uint64_t contentLength = 0;
				bool hasContentLength = false;
				std::string fileName;
				bool expectContinue = false;
				for (size_t pos = lineEnd + 2; pos < headerEnd; )
				{
					const size_t end = request.find("\r\n", pos);
					const std::string line = request.substr(pos, end - pos);
					pos = end + 2;
					const size_t colon = line.find(':');
					if (colon == std::string::npos)
						continue;
					const std::string key = lowercase(line.substr(0, colon));
					std::string value = line.substr(colon + 1);
					value.erase(0, value.find_first_not_of(' '));
					if (key == "content-length")
					{
						contentLength = strtoull(value.c_str(), nullptr, 10);
						hasContentLength = true;
					}
					else if (key == "x-file-name")
						fileName = urlDecode(value);
					else if (key == "expect")
						expectContinue = (lowercase(value) == "100-continue");
				}

				if (method == "GET" && (path == "/" || path == "/index.html"))
				{
					sendResponse(socket, 200, "text/html; charset=utf-8", PAGE_HTML);
				}
				else if (method == "GET" && path == "/status")
				{
					sendJson(socket, 200, statusJson());
				}
				else if (method == "GET" && path == "/mods")
				{
					std::string json;
					{
						Locked lock;
						json = gShared.mModsJson;
					}
					sendJson(socket, 200, json);
				}
				else if (method == "POST" && path == "/upload")
				{
					// Just the name, no folders
					const size_t slash = fileName.find_last_of("/\\");
					if (slash != std::string::npos)
						fileName = fileName.substr(slash + 1);
					if (!hasContentLength || contentLength == 0)
						sendJson(socket, 400, "{\"message\":\"Nothing was sent.\"}");
					else if (contentLength > MAX_UPLOAD_SIZE)
						sendJson(socket, 413, "{\"message\":\"That's bigger than 1 GB - too big for a 3DS mod.\"}");
					else if (!endsWith(lowercase(fileName), ".zip") && !endsWith(lowercase(fileName), ".rar") && !endsWith(lowercase(fileName), ".7z"))
						sendJson(socket, 400, "{\"message\":\"Only .zip, .rar or .7z files, please.\"}");
					else
					{
						if (expectContinue)
							sendAll(socket, "HTTP/1.1 100 Continue\r\n\r\n", 25);
						handleUpload(socket, fileName, contentLength, request.substr(headerEnd + 4));
					}
				}
				else
				{
					sendResponse(socket, 404, "text/plain", "Not found");
				}
			}

			void threadFunc(void*)
			{
				while (!gStopRequested)
				{
					pollfd fd = { gListenSocket, POLLIN, 0 };
					if (poll(&fd, 1, 200) <= 0)
						continue;
					sockaddr_in clientAddress;
					socklen_t addressLength = sizeof(clientAddress);
					const int client = accept(gListenSocket, (sockaddr*)&clientAddress, &addressLength);
					if (client < 0)
						continue;
					// Blocking for the connection itself (with "poll" for time limits)
					const int flags = fcntl(client, F_GETFL, 0);
					if (flags >= 0)
						fcntl(client, F_SETFL, flags & ~O_NONBLOCK);
					handleConnection(client);
					shutdown(client, SHUT_RDWR);
					close(client);
				}
				gThreadRunning = false;
			}

			int openListenSocket(int port)
			{
				const int listenSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
				if (listenSocket < 0)
					return -1;
				const int yes = 1;
				setsockopt(listenSocket, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
				sockaddr_in address;
				memset(&address, 0, sizeof(address));
				address.sin_family = AF_INET;
				address.sin_port = htons((uint16_t)port);
				address.sin_addr.s_addr = INADDR_ANY;
				if (bind(listenSocket, (sockaddr*)&address, sizeof(address)) != 0 || listen(listenSocket, 4) != 0)
				{
					close(listenSocket);
					return -1;
				}
				const int flags = fcntl(listenSocket, F_GETFL, 0);
				if (flags >= 0)
					fcntl(listenSocket, F_SETFL, flags | O_NONBLOCK);
				return listenSocket;
			}
		}

		InstallResult installZip(const std::string& zipPath, const std::string& nameHint, const std::string& modsDirectory, const std::string& tempDirectory, const std::function<void(int, int)>& progress)
		{
			return unpack(zipPath, nameHint, modsDirectory, tempDirectory, progress);
		}

		bool start(const std::string& modsDirectory, const std::string& workDirectory, uint32_t ipAddress)
		{
			if (!gLockReady)
			{
				LightLock_Init(&gShared.mLock);
				gLockReady = true;
			}
			if (gThreadRunning)
				return true;

			{
				Locked lock;
				gShared.mStatus = Status();
				gShared.mInstalled.clear();
			}
			if (ipAddress == 0)
			{
				Locked lock;
				gShared.mStatus.mState = Status::State::NO_WIFI;
				return false;
			}

			gModsDirectory = modsDirectory;
			gWorkDirectory = workDirectory;
			int port = 80;
			gListenSocket = openListenSocket(port);
			if (gListenSocket < 0)
			{
				port = 8080;
				gListenSocket = openListenSocket(port);
			}
			if (gListenSocket < 0)
			{
				RMX_LOG_INFO("Mods over Wi-Fi: couldn't open a port (80 or 8080)");
				Locked lock;
				gShared.mStatus.mState = Status::State::NO_WIFI;
				return false;
			}

			in_addr address;
			address.s_addr = ipAddress;
			const std::string addressText = std::string("http://") + inet_ntoa(address) + ((port == 80) ? std::string("/") : ":" + std::to_string(port) + "/");

			gStopRequested = false;
			gThreadRunning = true;
			s32 mainPriority = 0x30;
			svcGetThreadPriority(&mainPriority, CUR_THREAD_HANDLE);
			const s32 priority = std::min<s32>(mainPriority + 1, 0x3f);
			Thread thread = threadCreate(threadFunc, nullptr, 0x8000, priority, 1, true);
			if (nullptr == thread)
				thread = threadCreate(threadFunc, nullptr, 0x8000, priority, -2, true);
			if (nullptr == thread)
			{
				gThreadRunning = false;
				close(gListenSocket);
				gListenSocket = -1;
				Locked lock;
				gShared.mStatus.mState = Status::State::NO_WIFI;
				return false;
			}

			{
				Locked lock;
				gShared.mStatus.mState = Status::State::READY;
				gShared.mStatus.mAddress = addressText;
			}
			RMX_LOG_INFO("Mods over Wi-Fi: open at " << addressText);
			return true;
		}

		void stop()
		{
			if (!gThreadRunning)
			{
				Locked lock;
				if (gShared.mStatus.mState != Status::State::UNPACKING)
					gShared.mStatus.mState = Status::State::STOPPED;
				return;
			}
			gStopRequested = true;
			for (int k = 0; k < 400 && gThreadRunning; ++k)		// Up to 2 seconds for the connection to finish
				svcSleepThread(5 * 1000 * 1000);
			if (gListenSocket >= 0)
			{
				close(gListenSocket);
				gListenSocket = -1;
			}
			Locked lock;
			if (gShared.mStatus.mState != Status::State::UNPACKING)
				gShared.mStatus.mState = Status::State::STOPPED;
			RMX_LOG_INFO("Mods over Wi-Fi: closed");
		}

		bool isRunning()
		{
			return gThreadRunning;
		}

		Status getStatus()
		{
			if (!gLockReady)
				return Status();
			Locked lock;
			return gShared.mStatus;
		}

		bool takeInstalledMod(std::string& outFolder)
		{
			if (!gLockReady)
				return false;
			Locked lock;
			if (gShared.mInstalled.empty())
				return false;
			outFolder = gShared.mInstalled.front();
			gShared.mInstalled.pop_front();
			return true;
		}

		void setInstalledModsJson(const std::string& json)
		{
			if (!gLockReady)
			{
				LightLock_Init(&gShared.mLock);
				gLockReady = true;
			}
			Locked lock;
			gShared.mModsJson = json;
		}

		std::string escapeJson(const std::string& text)
		{
			std::string result;
			for (const char c : text)
			{
				switch (c)
				{
					case '"':  result += "\\\"";  break;
					case '\\': result += "\\\\";  break;
					case '\n': result += "\\n";  break;
					case '\r': break;
					case '\t': result += ' ';  break;
					default:
						if ((unsigned char)c < 0x20)
							result += ' ';
						else
							result += c;
				}
			}
			return result;
		}

		bool findModRoot(const std::vector<std::string>& entries, std::string& outRoot, std::string& outError)
		{
			// The shallowest mod.json, no more than 3 folders deep (zips often have an extra folder around the mod)
			int bestDepth = 99;
			int countAtBest = 0;
			for (const std::string& entry : entries)
			{
				const size_t slash = entry.rfind('/');
				const std::string fileName = (slash == std::string::npos) ? entry : entry.substr(slash + 1);
				if (lowercase(fileName) != "mod.json")
					continue;
				const std::string folder = (slash == std::string::npos) ? std::string() : entry.substr(0, slash + 1);
				if (folder.compare(0, 9, "__MACOSX/") == 0)
					continue;
				const int depth = (int)std::count(folder.begin(), folder.end(), '/');
				if (depth > 3)
					continue;
				if (depth < bestDepth)
				{
					bestDepth = depth;
					countAtBest = 1;
					outRoot = folder;
				}
				else if (depth == bestDepth)
				{
					++countAtBest;
				}
			}
			if (countAtBest == 0)
			{
				outError = "No mod.json found - this doesn't look like a Sonic 3 A.I.R. mod.";
				return false;
			}
			if (countAtBest > 1)
			{
				outError = "This zip has several mods in it. Please unpack it on your PC and zip and upload them one by one.";
				return false;
			}
			return true;
		}

		std::string cleanFolderName(const std::string& name)
		{
			std::string result;
			for (const char c : name)
				result += (isalnum((unsigned char)c) || strchr(" -_.()[]", c) != nullptr) ? c : '_';
			const size_t first = result.find_first_not_of(" .");
			const size_t last = result.find_last_not_of(" .");
			result = (first == std::string::npos) ? std::string() : result.substr(first, last - first + 1);
			return result.empty() ? "mod" : result;
		}

		bool isSafeZipPath(const std::string& path)
		{
			if (path.empty() || path[0] == '/' || path.find(':') != std::string::npos)
				return false;
			size_t start = 0;
			while (start <= path.size())
			{
				const size_t end = path.find('/', start);
				const std::string part = path.substr(start, (end == std::string::npos) ? std::string::npos : end - start);
				if (part == "..")
					return false;
				if (end == std::string::npos)
					break;
				start = end + 1;
			}
			return true;
		}
	}
}

#endif
