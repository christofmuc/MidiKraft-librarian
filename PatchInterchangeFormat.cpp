/*
   Copyright (c) 2020 Christof Ruch. All rights reserved.

   Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
*/

#include "PatchInterchangeFormat.h"

#include "Logger.h"
#include "Sysex.h"

#include "rapidjson/document.h"
#include "rapidjson/writer.h"
#include "rapidjson/prettywriter.h"
#include "rapidjson/filewritestream.h"

#include <boost/format.hpp>

#include "RapidjsonHelper.h"
#include "JsonSerialization.h"

#include <cstdio>

const char *kSynth = "Synth";
const char *kName = "Name";
const char *kSysex = "Sysex";
const char *kFavorite = "Favorite";
const char *kPlace = "Place";
const char *kCategories = "Categories";
const char *kNonCategories = "NonCategories";
const char *kSourceInfo = "SourceInfo";

namespace midikraft {

	bool findCategory(std::shared_ptr<AutomaticCategory> detector, const char *categoryName, midikraft::Category &outCategory) {
		// Hard code migration from the Rev2SequencerTool categoryNames to KnobKraft Orm
		//TODO this can use the mapping defined for the Virus now?
		//TODO - this could become arbitrarily complex with free tags?
		if (!strcmp(categoryName, "Bells")) categoryName = "Bell";
		if (!strcmp(categoryName, "FX")) categoryName = "SFX";

		// Check if this is a valid category
		for (auto acat : detector->predefinedCategoryVector()) {
			if (acat.category == categoryName) {
				// Found, great!
				outCategory = acat;
				return true;			
			}
		}
		return false;
	}

	std::vector<midikraft::PatchHolder> PatchInterchangeFormat::load(std::shared_ptr<Synth> activeSynth, std::string const &filename, std::shared_ptr<AutomaticCategory> detector)
	{
		std::vector<midikraft::PatchHolder> result;

		// Check if file exists
		File pif(filename);
		auto fileSource = std::make_shared<FromFileSource>(pif.getFileName().toStdString(), pif.getFullPathName().toStdString(), MidiProgramNumber::fromZeroBase(0));
		if (pif.existsAsFile()) {
			FileInputStream in(pif);
			String content = in.readEntireStreamAsString();

			// Try to parse it!
			rapidjson::Document jsonDoc;
			jsonDoc.Parse(content.toStdString().c_str());

			if (jsonDoc.IsArray()) {
				auto patchArray = jsonDoc.GetArray();
				for (auto item = jsonDoc.Begin(); item != jsonDoc.End(); item++) {
					if (!item->HasMember(kSynth)) {
						SimpleLogger::instance()->postMessage("Skipping patch which has no 'Synth' field");
						continue;
					}
					if (activeSynth->getName() != (*item)[kSynth].GetString()) {
						SimpleLogger::instance()->postMessage((boost::format("Skipping patch which is for synth %s and not for %s") % (*item)["Synth"].GetString() % activeSynth->getName()).str());
						continue;
					}
					if (!item->HasMember(kName)) {
						SimpleLogger::instance()->postMessage("Skipping patch which has no 'Name' field");
						continue;
					}
					std::string patchName = (*item)[kName].GetString(); //TODO this is not robust, as it might have a non-string type
					if (!item->HasMember(kSysex)) {
						SimpleLogger::instance()->postMessage((boost::format("Skipping patch %s which has no 'Sysex' field") % patchName).str());
						continue;
					}

					// Optional fields!
					Favorite fav;
					if (item->HasMember(kFarvorite)) {
						std::string favoriteStr = (*item)[kFarvorite].GetString();
						try {
							bool favorite = std::stoi(favoriteStr) != 0;
							fav = Favorite(favorite);
						}
						catch (std::invalid_argument &) {
							SimpleLogger::instance()->postMessage((boost::format("Ignoring favorite information for patch %s because %s does not convert to an integer") % patchName % favoriteStr).str());
						}
					}

					MidiProgramNumber place = MidiProgramNumber::fromZeroBase(0);
					if (item->HasMember(kPlace)) {
						std::string placeStr = (*item)[kPlace].GetString();
						try {
							place = MidiProgramNumber::fromZeroBase(std::stoi(placeStr));
						}
						catch (std::invalid_argument &) {
							SimpleLogger::instance()->postMessage((boost::format("Ignoring MIDI place information for patch %s because %s does not convert to an integer") % patchName % placeStr).str());
						}
					}

					std::vector<Category> categories;
					if (item->HasMember(kCategories)) {
						auto cats = (*item)[kCategories].GetArray();
						for (auto cat = cats.Begin(); cat != cats.End(); cat++) {
							midikraft::Category category("", Colours::aliceblue);
							if (findCategory(detector, cat->GetString(), category)) {
								categories.push_back(category);
							}
							else {
								SimpleLogger::instance()->postMessage((boost::format("Ignoring category %s of patch %s because it is not part of our standard categories!") % cat->GetString() % patchName).str());
							}
						}
					}

					std::vector<Category> nonCategories;
					if (item->HasMember(kNonCategories)) {
						auto cats = (*item)[kNonCategories].GetArray();
						for (auto cat = cats.Begin(); cat != cats.End(); cat++) {
							midikraft::Category category("", Colours::aliceblue);
							if (findCategory(detector, cat->GetString(), category)) {
								nonCategories.push_back(category);
							}
							else {
								SimpleLogger::instance()->postMessage((boost::format("Ignoring non-category %s of patch %s because it is not part of our standard categories!") % cat->GetString() % patchName).str());
							}
						}
					}

					std::shared_ptr<midikraft::SourceInfo> importInfo;
					if (item->HasMember(kSourceInfo)) {
						importInfo = SourceInfo::fromString(renderToJson((*item)[kSourceInfo]));
					}

					// All mandatory fields found, we can parse the data!
					MemoryBlock sysexData;
					MemoryOutputStream writeToBlock(sysexData, false);
					String base64encoded = (*item)[kSysex].GetString();
					if (Base64::convertFromBase64(writeToBlock, base64encoded)) {
						writeToBlock.flush();
						auto messages = Sysex::memoryBlockToMessages(sysexData);
						auto patches = activeSynth->loadSysex(messages);
						//jassert(patches.size() == 1);
						if (patches.size() == 1) {
							//TODO The file format did not specify MIDI banks 
							PatchHolder holder(activeSynth, fileSource, patches[0], MidiBankNumber::fromZeroBase(0), place, detector);
							holder.setFavorite(fav);
							holder.setName(patchName);
							for (const auto& cat : categories) {
								holder.setCategory(cat, true);
								holder.setUserDecision(cat); // All Categories loaded via PatchInterchangeFormat are considered user decisions
							}
							for (const auto &noncat : nonCategories) {
								holder.setUserDecision(noncat); // A Category mentioned here says it might not be present, but that is a user decision!
							}
							if (importInfo) {
								holder.setSourceInfo(importInfo);
							}
							result.push_back(holder);
						}
					}
					else {
						SimpleLogger::instance()->postMessage("Skipping patch with invalid base64 encoded data!");
					}
				}
			}
		}
		return result;
	}

	void PatchInterchangeFormat::save(std::vector<PatchHolder> const &patches, std::string const &toFilename)
	{
		File outputFile(toFilename);
		if (outputFile.existsAsFile()) {
			outputFile.deleteFile();
		}

		rapidjson::Document doc;
		doc.SetArray();
		for (auto patch : patches) {
			rapidjson::Value patchJson;
			patchJson.SetObject();
			addToJson(kSynth, patch.synth()->getName(), patchJson, doc);
			addToJson(kName, patch.name(), patchJson, doc);
			patchJson.AddMember(rapidjson::StringRef(kFavorite), patch.isFavorite() ? 1 : 0, doc.GetAllocator());
			patchJson.AddMember(rapidjson::StringRef(kPlace), patch.patchNumber().toZeroBased(), doc.GetAllocator());
			int64 categoriesSet = patch.categoriesAsBitfield();
			int64 userDecisions = patch.userDecisionAsBitfield();
			if (categoriesSet & userDecisions) {
				// Here is a list of categories to write
				rapidjson::Value categoryList;
				categoryList.SetArray();
				std::set<Category> userDefinedCategories;
				patch.makeSetOfCategoriesFromBitfield(userDefinedCategories, categoriesSet & userDecisions);
				for (auto cat : userDefinedCategories) {
					rapidjson::Value catValue;
					catValue.SetString(cat.category.c_str(), doc.GetAllocator());
					categoryList.PushBack(catValue, doc.GetAllocator());
				}
				patchJson.AddMember(rapidjson::StringRef(kCategories), categoryList, doc.GetAllocator());
			}
			if ((~categoriesSet) & userDecisions) {
				// Here is a list of non-categories to write
				rapidjson::Value nonCategoryList;
				nonCategoryList.SetArray();
				std::set<Category> userDefinedNonCategories;
				patch.makeSetOfCategoriesFromBitfield(userDefinedNonCategories, (~categoriesSet) & userDecisions);
				for (auto cat : userDefinedNonCategories) {
					rapidjson::Value catValue;
					catValue.SetString(cat.category.c_str(), doc.GetAllocator());
					nonCategoryList.PushBack(catValue, doc.GetAllocator());
				}
				patchJson.AddMember(rapidjson::StringRef(kNonCategories), nonCategoryList, doc.GetAllocator());
			}

			if (patch.sourceInfo()) {
				std::string jsonRep = patch.sourceInfo()->toString();
				rapidjson::Document sourceInfoDoc(&doc.GetAllocator());
				sourceInfoDoc.Parse(jsonRep.c_str());
				patchJson.AddMember(rapidjson::StringRef(kSourceInfo), sourceInfoDoc, doc.GetAllocator());
			}

			// Now the fun part, pack the sysex for transport
			auto sysexMessages = patch.synth()->patchToSysex(patch.patch(), nullptr);
			std::vector<uint8> data;
			// Just concatenate all messages generated into one uint8 array
			for (auto m : sysexMessages) {
				std::copy(m.getRawData(), m.getRawData() + m.getRawDataSize(), std::back_inserter(data));
			}
			std::string base64encoded = JsonSerialization::dataToString(data);
			addToJson(kSysex, base64encoded, patchJson, doc);

			doc.PushBack(patchJson, doc.GetAllocator());
		}

		// According to documentation of Rapid Json, this is the fastest way to write it to a stream
		// I'll just believe it and use a nice old C file handle.
#if WIN32
		FILE* fp;
		if (fopen_s(&fp, toFilename.c_str(), "wb") != 0) {
			SimpleLogger::instance()->postMessage((boost::format("Failure to open file %s to write patch interchange format to") % toFilename).str());
		}
#else
		FILE* fp = fopen(toFilename.c_str(), "w");
#endif
		char writeBuffer[65536];
		rapidjson::FileWriteStream os(fp, writeBuffer, sizeof(writeBuffer));
		rapidjson::PrettyWriter<rapidjson::FileWriteStream> writer(os);
		doc.Accept(writer);
		fclose(fp);
	}

}
