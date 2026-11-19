/*
*	Part of the Oxygen Engine / Sonic 3 A.I.R. software distribution.
*	Copyright (C) 2017-2026 by Eukaryot
*
*	Published under the GNU GPLv3 open source software license, see license.txt
*	or https://www.gnu.org/licenses/gpl-3.0.en.html
*/

#include "lemon/pch.h"
#include "lemon/program/function/ScriptFunction.h"
#include "lemon/program/Module.h"
#include "lemon/compiler/Utility.h"
#include "lemon/runtime/Runtime.h"
#include "lemon/utility/PragmaSplitter.h"
#include "lemon/utility/QuickDataHasher.h"


namespace lemon
{
	namespace
	{
		// Variable-length encoding of unsigned values: 7 bits per byte, highest bit set if more bytes follow
		void writeVarUInt(std::vector<uint8>& output, uint64 value)
		{
			while (value >= 0x80)
			{
				output.push_back((uint8)(value | 0x80));
				value >>= 7;
			}
			output.push_back((uint8)value);
		}

		uint64 readVarUInt(const uint8*& input)
		{
			uint64 value = 0;
			int shift = 0;
			while (true)
			{
				const uint8 byte = *input;
				++input;
				value |= (uint64)(byte & 0x7f) << shift;
				if ((byte & 0x80) == 0)
					return value;
				shift += 7;
			}
		}

		// Signed values use zigzag encoding, so that small negative values are short as well
		void writeVarInt(std::vector<uint8>& output, int64 value)
		{
			writeVarUInt(output, ((uint64)value << 1) ^ (uint64)(value >> 63));
		}

		int64 readVarInt(const uint8*& input)
		{
			const uint64 value = readVarUInt(input);
			return (int64)(value >> 1) ^ -(int64)(value & 1);
		}

		inline bool isCallSiteOpcode(const Opcode& opcode)
		{
			return (opcode.mType == Opcode::Type::CALL || opcode.mType == Opcode::Type::EXTERNAL_CALL);
		}
	}


	ScriptFunction::~ScriptFunction()
	{
		for (LocalVariable* variable : mLocalVariablesByID)
		{
			mModule->destroyLocalVariable(*variable);
		}
	}

	LocalVariable* ScriptFunction::getLocalVariableByIdentifier(uint64 nameHash) const
	{
		const auto it = mLocalVariablesByIdentifier.find(nameHash);
		return (it == mLocalVariablesByIdentifier.end()) ? nullptr : it->second;
	}

	LocalVariable& ScriptFunction::getLocalVariableByID(uint32 id) const
	{
		return *mLocalVariablesByID[id];
	}

	LocalVariable& ScriptFunction::addLocalVariable(FlyweightString name, const DataTypeDefinition* dataType, uint32 lineNumber)
	{
		// Check if it already exists!
		if (mLocalVariablesByIdentifier.count(name.getHash()))
		{
			CHECK_ERROR(false, "Variable already exists", lineNumber);
		}

		LocalVariable& variable = mModule->createLocalVariable();
		variable.mName = name;
		variable.mDataType = dataType;

		// Set local memory offset and size
		const size_t variableSize = (dataType->getBytes() + 7) / 8 * 8;		// Align to multiples of 8 bytes (i.e. int64 size)
		variable.mLocalMemoryOffset = mLocalVariablesMemorySize;
		variable.mLocalMemorySize = variableSize;
		mLocalVariablesMemorySize += variableSize;

		// Register variable
		mLocalVariablesByIdentifier.emplace(name.getHash(), &variable);

		variable.mID = (uint32)mLocalVariablesByID.size();
		mLocalVariablesByID.emplace_back(&variable);

		return variable;
	}

	const ScriptFunction::Label* ScriptFunction::findLabelByName(FlyweightString labelName) const
	{
		for (const Label& label : mLabels)
		{
			if (label.mName == labelName)
			{
				return &label;
			}
		}
		return nullptr;
	}

	const ScriptFunction::Label* ScriptFunction::findLabelByOffset(size_t offset) const
	{
		// Note that this won't handle multiple labels at the same position too well
		for (const Label& label : mLabels)
		{
			if (label.mOffset == offset)
			{
				return &label;
			}
		}
		return nullptr;
	}

	void ScriptFunction::addLabel(FlyweightString labelName, size_t offset, const std::vector<AddressHook>& addressHooks)
	{
		Label& label = vectorAdd(mLabels);
		label.mName = labelName;
		label.mOffset = (uint32)offset;
		label.mLabelAddressHooks = addressHooks;
	}

	void ScriptFunction::addOrProcessPragma(std::string_view pragmaString, bool consumeIfProcessed)
	{
		PragmaSplitter pragmaSplitter(pragmaString);
		if (!pragmaSplitter.mEntries.empty())
		{
			ScriptFunction::AddressHook* lastAddressHook = nullptr;
			bool hadAddressHook = false;
			bool hadAlias = false;
			for (const lemon::PragmaSplitter::Entry& entry : pragmaSplitter.mEntries)
			{
				if (entry.mArgument == "alias")
				{
					// Add alias for this function
					vectorAdd(mAliasNames).mName = entry.mValue;
					hadAlias = true;
				}
				else if (entry.mArgument == "address-hook")
				{
					// Create address hook
					RMX_CHECK(!entry.mValue.empty(), "Address hook must have a value", continue);
					ScriptFunction::AddressHook& addressHook = vectorAdd(mAddressHooks);
					addressHook.mAddress = (uint32)rmx::parseInteger(entry.mValue);
					lastAddressHook = &addressHook;
					hadAddressHook = true;
				}
				else if (entry.mArgument == "translated")
				{
					// You can use "translated" to denote that some code was already put into script, but should not be an actual address hook
					hadAddressHook = true;
				}
				else if (entry.mArgument == "off")
				{
					// Mark address hook as disabled
					if (nullptr != lastAddressHook)
					{
						lastAddressHook->mDisabled = true;
					}
				}
				else if (entry.mArgument == "deprecated")
				{
					if (hadAlias)
					{
						// Mark alias as deprecated
						mAliasNames.back().mIsDeprecated = true;
					}
					else
					{
						// Mark function itself as deprecated
						mFlags.set(Flag::DEPRECATED);
					}
				}
			}

			// If there was an address hook, there's no need to store this pragma
			if (consumeIfProcessed && hadAddressHook)
				return;
		}

		// Store this pragma as string
		mPragmas.emplace_back(pragmaString);
	}

	void ScriptFunction::releaseOpcodes()
	{
		if (mOpcodesReleased)
			return;

		// Layout of the compact data:
		//  - Number of opcodes
		//  - Number of line entries, then for each: opcode index (as difference to the previous entry), line number (as signed difference to the previous entry)
		//  - Number of call sites, then for each: opcode index (as difference to the previous one), type, data type, and for CALL also the 8-byte parameter
		static std::vector<uint8> data;
		data.clear();
		writeVarUInt(data, mOpcodes.size());

		size_t numLineEntries = 0;
		size_t numCallSites = 0;
		for (size_t k = 0; k < mOpcodes.size(); ++k)
		{
			if (k == 0 || mOpcodes[k].mLineNumber != mOpcodes[k-1].mLineNumber)
				++numLineEntries;
			if (isCallSiteOpcode(mOpcodes[k]))
				++numCallSites;
		}

		writeVarUInt(data, numLineEntries);
		{
			size_t lastIndex = 0;
			uint32 lastLineNumber = 0;
			for (size_t k = 0; k < mOpcodes.size(); ++k)
			{
				if (k == 0 || mOpcodes[k].mLineNumber != mOpcodes[k-1].mLineNumber)
				{
					writeVarUInt(data, k - lastIndex);
					writeVarInt(data, (int64)mOpcodes[k].mLineNumber - (int64)lastLineNumber);
					lastIndex = k;
					lastLineNumber = mOpcodes[k].mLineNumber;
				}
			}
		}

		writeVarUInt(data, numCallSites);
		{
			size_t lastIndex = 0;
			for (size_t k = 0; k < mOpcodes.size(); ++k)
			{
				const Opcode& opcode = mOpcodes[k];
				if (isCallSiteOpcode(opcode))
				{
					writeVarUInt(data, k - lastIndex);
					data.push_back((uint8)opcode.mType);
					data.push_back((uint8)opcode.mDataType);
					if (opcode.mType == Opcode::Type::CALL)
					{
						const uint64 parameter = (uint64)opcode.mParameter;
						for (int i = 0; i < 8; ++i)
							data.push_back((uint8)(parameter >> (i * 8)));
					}
					lastIndex = k;
				}
			}
		}

		// Copy with the exact size, and free the opcodes
		mReleasedOpcodeData.assign(data.begin(), data.end());
		std::vector<Opcode>().swap(mOpcodes);
		mOpcodesReleased = true;
	}

	size_t ScriptFunction::getNumOpcodes() const
	{
		if (!mOpcodesReleased)
			return mOpcodes.size();

		const uint8* input = mReleasedOpcodeData.data();
		return (size_t)readVarUInt(input);
	}

	uint32 ScriptFunction::getOpcodeLineNumber(size_t opcodeIndex) const
	{
		if (!mOpcodesReleased)
		{
			if (mOpcodes.empty())
				return 0;
			return (opcodeIndex < mOpcodes.size()) ? mOpcodes[opcodeIndex].mLineNumber : mOpcodes.back().mLineNumber;
		}

		const uint8* input = mReleasedOpcodeData.data();
		const size_t numOpcodes = (size_t)readVarUInt(input);
		if (numOpcodes == 0)
			return 0;

		// Find the last line entry starting at or before the opcode index
		const size_t numLineEntries = (size_t)readVarUInt(input);
		size_t index = 0;
		uint32 lineNumber = 0;
		for (size_t i = 0; i < numLineEntries; ++i)
		{
			const size_t entryIndex = index + (size_t)readVarUInt(input);
			const uint32 entryLineNumber = (uint32)((int64)lineNumber + readVarInt(input));
			if (i > 0 && entryIndex > opcodeIndex)
				break;
			index = entryIndex;
			lineNumber = entryLineNumber;
		}
		return lineNumber;
	}

	void ScriptFunction::getCallSites(std::vector<CallSite>& outCallSites) const
	{
		outCallSites.clear();
		if (!mOpcodesReleased)
		{
			for (size_t k = 0; k < mOpcodes.size(); ++k)
			{
				const Opcode& opcode = mOpcodes[k];
				if (isCallSiteOpcode(opcode))
				{
					CallSite& callSite = vectorAdd(outCallSites);
					callSite.mParameter = (uint64)opcode.mParameter;
					callSite.mOpcodeIndex = (uint32)k;
					callSite.mType = opcode.mType;
					callSite.mDataType = opcode.mDataType;
				}
			}
			return;
		}

		const uint8* input = mReleasedOpcodeData.data();
		readVarUInt(input);		// Number of opcodes

		// Skip the line entries
		const size_t numLineEntries = (size_t)readVarUInt(input);
		for (size_t i = 0; i < numLineEntries; ++i)
		{
			readVarUInt(input);
			readVarInt(input);
		}

		const size_t numCallSites = (size_t)readVarUInt(input);
		outCallSites.reserve(numCallSites);
		size_t index = 0;
		for (size_t i = 0; i < numCallSites; ++i)
		{
			CallSite& callSite = vectorAdd(outCallSites);
			index += (size_t)readVarUInt(input);
			callSite.mOpcodeIndex = (uint32)index;
			callSite.mType = (Opcode::Type)input[0];
			callSite.mDataType = (BaseType)input[1];
			input += 2;
			if (callSite.mType == Opcode::Type::CALL)
			{
				uint64 parameter = 0;
				for (int k = 0; k < 8; ++k)
					parameter |= (uint64)input[k] << (k * 8);
				callSite.mParameter = parameter;
				input += 8;
			}
		}
	}

	uint64 ScriptFunction::addToCompiledHash(uint64 hash) const
	{
		QuickDataHasher dataHasher(hash);
		for (const Opcode& opcode : mOpcodes)
		{
			dataHasher.prepareNextData(10);
			dataHasher.addData((uint8)opcode.mType);
			dataHasher.addData((uint8)opcode.mDataType);
			if (opcode.mParameter != 0)
				dataHasher.addData((uint64)opcode.mParameter);
		}
		return dataHasher.getHash();
	}

}
