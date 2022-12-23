/*
   Copyright (c) 2019-2022 Christof Ruch. All rights reserved.

   Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
*/

#pragma once

#include "DownloadStrategy.h"

#include "ProgramDumpCapability.h"
#include "ProgressHandler.h"

#include <future>

namespace midikraft {

	class ProgramDumpDownloadStrategy : public DownloadStrategy {
	public:
		ProgramDumpDownloadStrategy();
		virtual ~ProgramDumpDownloadStrategy();

		void init(std::shared_ptr<SafeMidiOutput> midiOutput, std::shared_ptr<Synth> synth, MidiBankNumber bank, TPromiseOfPatches& promise, ProgressHandler *progressHandler);
		void request() override;
		bool requestSuccessful() override;
		void deinit();

	private:
		void handleNextProgramBuffer(const juce::MidiMessage& editBuffer, MidiBankNumber bankNo);

		MidiController::HandlerHandle handle_;
		std::shared_ptr<SafeMidiOutput> midiOutput_;
		std::shared_ptr<Synth> synth_;
		std::shared_ptr<ProgramDumpCabability> programDumpCapability_;
		ProgressHandler* progressHandler_;

		int downloadNumber_;
		int lastRequestNumber_;
		int startDownloadNumber_;
		int endDownloadNumber_;
		std::vector<juce::MidiMessage> currentProgramDump_; // Messages since the last request
		std::vector<juce::MidiMessage> currentDownload_;  // All messages from successful requests

		TPromiseOfPatches promiseOfPatches_;
	};

}
