#!/usr/bin/env python3
"""
Generates the Universal-Updater store file (sonic3air-3ds.unistore) for a GitHub release of the 3DS port.

The release workflow (.github/workflows/3ds.yml) runs this for every version tag and attaches the result to the release.
Users add the store once, from the URL of the latest release's copy:
    https://github.com/<repo>/releases/latest/download/sonic3air-3ds.unistore
Universal-Updater then checks that URL for store updates (it reloads the store when "revision" increased)
and downloads the CIA / 3DSX from the latest release.

Test channel (--channel test): the store for the rolling "test-build" pre-release, which the workflow replaces with
every build of the development branch. Its URL never changes:
    https://github.com/<repo>/releases/download/test-build/sonic3air-3ds-test.unistore

Format: UniStore version 3, see https://github.com/Universal-Team/Universal-Updater/wiki
Usage:  make_unistore.py --repo owner/name --version v0.1.0 --revision 12 [--channel stable|test] [--sheet] [--output FILE]
"""

import argparse
import datetime
import json

SHEET_ASSET = 'sonic3air-3ds.t3x'		# Name of the icon sheet in the release
CHANNELS = {
	'stable': {'store_file': 'sonic3air-3ds.unistore', 'sheet_file': 'sonic3air-3ds.t3x', 'prereleases': False,
			   'store_title': 'Sonic 3 A.I.R. for New 3DS by heccfluffin', 'app_title': 'Sonic 3 A.I.R. (New 3DS)'},
	'test':   {'store_file': 'sonic3air-3ds-test.unistore', 'sheet_file': 'sonic3air-3ds-test.t3x', 'prereleases': True,
			   'store_title': 'Sonic 3 A.I.R. for New 3DS by heccfluffin (test builds)', 'app_title': 'Sonic 3 A.I.R. (New 3DS, test build)'},
}

PROMPT = ('Sonic 3 A.I.R. needs files that can\'t be downloaded here:\n'
		  '- the "data" folder of A.I.R. 26.03.28.0 (sonic3air.org)\n'
		  '- your own ROM, named exactly Sonic_Knuckles_wSonic3.bin\n'
		  'Both go to sdmc:/3ds/sonic3air/ - see INSTALL.md in the release.\n'
		  'New 3DS / New 2DS XL only. Continue?')


def main():
	parser = argparse.ArgumentParser()
	parser.add_argument('--repo', required=True, help='GitHub repository, e.g. owner/sonic3air')
	parser.add_argument('--version', required=True, help='Release version / tag, e.g. v0.1.0')
	parser.add_argument('--revision', required=True, type=int, help='Store revision, must increase with every release')
	parser.add_argument('--sheet', action='store_true', help='Reference the icon sprite sheet (sonic3air-3ds.t3x) attached to the release')
	parser.add_argument('--channel', choices=sorted(CHANNELS), default='stable')
	parser.add_argument('--output')
	args = parser.parse_args()
	channel = CHANNELS[args.channel]
	store_file = channel['store_file']
	output = args.output or store_file
	prereleases = channel['prereleases']

	# Where the store file and icon sheet are downloaded from: the latest release, or the fixed test-build pre-release
	if args.channel == 'test':
		latest = f'https://github.com/{args.repo}/releases/download/test-build/'
	else:
		latest = f'https://github.com/{args.repo}/releases/latest/download/'
	now = datetime.datetime.now(datetime.timezone.utc).strftime('%Y-%m-%d at %H:%M (UTC)')

	store_info = {
		'title': channel['store_title'],
		'author': 'heccfluffin',
		'description': 'Unofficial New 3DS port of Sonic 3 A.I.R. by heccfluffin. Original game by Eukaryot & contributors.',
		'file': store_file,
		'url': latest + store_file,
		'version': 3,
		'revision': args.revision,
	}
	info = {
		'title': channel['app_title'],
		'author': 'heccfluffin (3DS port), Eukaryot & contributors (original)',
		'version': args.version,
		'category': ['game'],
		'console': ['3DS'],
		'description': ('Unofficial New 3DS / New 2DS XL port of Sonic 3 - Angel Island Revisited by heccfluffin. '
						'Needs your own Sonic 3 & Knuckles ROM (Steam) named Sonic_Knuckles_wSonic3.bin '
						'and the data folder of A.I.R. 26.03.28.0 in sdmc:/3ds/sonic3air/. See INSTALL.md.'),
		'license': 'gpl-3.0',
		'last_updated': now,
	}
	if args.sheet:
		store_info['sheet'] = channel['sheet_file']
		store_info['sheetURL'] = latest + SHEET_ASSET
		info['icon_index'] = 0
		info['sheet_index'] = 0

	# Release asset names are matched as regular expressions by Universal-Updater, hence the escaped dots
	cia_actions = [
		{'type': 'promptMessage', 'message': PROMPT},
		{'type': 'mkdir', 'directory': 'sdmc:/3ds/sonic3air'},
		{'type': 'downloadRelease', 'repo': args.repo, 'file': r'sonic3air\.cia', 'output': 'sdmc:/sonic3air.cia',
		 'includePrereleases': prereleases, 'message': 'Downloading sonic3air.cia...'},
		{'type': 'installCia', 'file': 'sdmc:/sonic3air.cia', 'message': 'Installing sonic3air.cia...'},
		{'type': 'deleteFile', 'file': 'sdmc:/sonic3air.cia', 'message': 'Deleting sonic3air.cia...'},
	]
	dsx_actions = [
		{'type': 'promptMessage', 'message': PROMPT},
		{'type': 'downloadRelease', 'repo': args.repo, 'file': r'sonic3air\.3dsx', 'output': 'sdmc:/3ds/sonic3air/sonic3air.3dsx',
		 'includePrereleases': prereleases, 'message': 'Downloading sonic3air.3dsx...'},
	]
	guide_actions = [
		{'type': 'downloadRelease', 'repo': args.repo, 'file': r'INSTALL\.md', 'output': 'sdmc:/3ds/sonic3air/INSTALL.md',
		 'includePrereleases': prereleases, 'message': 'Downloading the install guide...'},
	]

	store = {
		'storeInfo': store_info,
		'storeContent': [
			{
				'info': info,
				'sonic3air.cia (recommended)': cia_actions,
				'sonic3air.3dsx (Homebrew Launcher)': dsx_actions,
				'Install guide (to sdmc:/3ds/sonic3air/)': guide_actions,
			}
		],
	}

	with open(output, 'w', encoding='utf-8') as file:
		json.dump(store, file, indent='\t', ensure_ascii=True)
		file.write('\n')
	print(f'Wrote {output} ({args.channel} channel, version {args.version}, revision {args.revision})')


if __name__ == '__main__':
	main()
