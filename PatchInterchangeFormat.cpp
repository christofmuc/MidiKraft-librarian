/*
   Copyright (c) 2020 Christof Ruch. All rights reserved.

   Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
*/

#include "PatchInterchangeFormat.h"

#include "RapidjsonHelper.h"

#include "Logger.h"
#include "Sysex.h"

#include <boost/format.hpp>

namespace midikraft {

	std::vector<midikraft::PatchHolder> PatchInterchangeFormat::load(std::shared_ptr<Synth> activeSynth, std::string const &filename)
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
					if (!item->HasMember("Synth")) {
						SimpleLogger::instance()->postMessage("Skipping patch which has no 'Synth' field");
						continue;
					}
					if (activeSynth->getName() != (*item)["Synth"].GetString()) {
						SimpleLogger::instance()->postMessage((boost::format("Skipping patch which is for synth %s and not for %s") % (*item)["Synth"].GetString() % activeSynth->getName()).str());
						continue;
					}
					if (!item->HasMember("Name")) {
						SimpleLogger::instance()->postMessage("Skipping patch which has no 'Name' field");
						continue;
					}
					std::string patchName = (*item)["Name"].GetString(); //TODO this is not robust, as it might have a non-string type
					if (!item->HasMember("Sysex")) {
						SimpleLogger::instance()->postMessage((boost::format("Skipping patch %s which has no 'Sysex' field") % patchName).str());
						continue;
					}

					// Optional fields!
					Favorite fav;
					if (item->HasMember("Favorite")) {
						std::string favoriteStr = (*item)["Favorite"].GetString();
						try {
							bool favorite = std::stoi(favoriteStr) != 0;
							fav = Favorite(favorite);
						}
						catch (std::invalid_argument &) {
							SimpleLogger::instance()->postMessage((boost::format("Ignoring favorite information for patch %s because %s does not convert to an integer") % patchName % favoriteStr).str());
						}
					}
					std::vector<Category> categories;
					if (item->HasMember("Categories")) {
						auto cats = (*item)["Categories"].GetArray();
						for (auto cat = cats.Begin(); cat != cats.End(); cat++) {
							auto categoryName = cat->GetString();
							// Check if this is a valid category
							bool found = false;
							for (auto acat : AutoCategory::predefinedCategoryVector()) {
								if (acat.category == categoryName) {
									// Found, great!
									categories.push_back(acat);
									found = true;
								}
							}
							if (!found) {
								SimpleLogger::instance()->postMessage((boost::format("Ignoring category %s of patch %s because it is not part of our standard categories!") % categoryName % patchName).str());
							}
						}
					}

					// All mandatory fields found, we can parse the data!
					MemoryBlock sysexData;
					MemoryOutputStream writeToBlock(sysexData, false);
					String base64encoded = (*item)["Sysex"].GetString();
					if (Base64::convertFromBase64(writeToBlock, base64encoded)) {
						writeToBlock.flush();
						auto messages = Sysex::memoryBlockToMessages(sysexData);
						auto patches = activeSynth->loadSysex(messages);
						//jassert(patches.size() == 1);
						if (patches.size() == 1) {
							PatchHolder holder(activeSynth, fileSource, patches[0], false);
							holder.setFavorite(fav);
							holder.setName(patchName);
							for (auto cat : categories) holder.setCategory(cat, true);
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

}