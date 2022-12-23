/*
   Copyright (c) 2019-2022 Christof Ruch. All rights reserved.

   Dual licensed: Distributed under Affero GPL license by default, an MIT license is available for purchase
*/

#include "ProgramDumpDownloadStrategy.h"

#include "Capability.h"

namespace midikraft {

	ProgramDumpDownloadStrategy::ProgramDumpDownloadStrategy() : handle_(MidiController::makeNoneHandle())
	{
	}

	ProgramDumpDownloadStrategy::~ProgramDumpDownloadStrategy()
	{
		deinit();
	}

	void ProgramDumpDownloadStrategy::init(std::shared_ptr<SafeMidiOutput> midiOutput, std::shared_ptr<Synth> synth, MidiBankNumber bankNo, TPromiseOfPatches& promise, ProgressHandler* progressHandler)
	{
		jassert(handle_.isNull());
		deinit();
		promiseOfPatches_ = std::move(promise);
		midiOutput_ = midiOutput;
		synth_ = synth;
		progressHandler_ = progressHandler;
		handle_ = MidiController::makeOneHandle();
		MidiController::instance()->addMessageHandler(handle_, [this, bankNo](MidiInput* source, const juce::MidiMessage& editBuffer) {
			ignoreUnused(source);
			this->handleNextProgramBuffer(editBuffer, bankNo);
		});
		lastRequestNumber_ = -100;
		downloadNumber_ = bankNo.toZeroBased() * synth->numberOfPatches();
		startDownloadNumber_ = downloadNumber_;
		endDownloadNumber_ = downloadNumber_ + synth->numberOfPatches() - 1;
		programDumpCapability_ = Capability::hasCapability<ProgramDumpCabability>(synth_);
		if (!programDumpCapability_) {
			SimpleLogger::instance()->postMessage("Program Error: This synth does not implement the program dump capability");
		}
	}

	void ProgramDumpDownloadStrategy::request()
	{
		if (programDumpCapability_) {
			std::vector<MidiMessage> messages;
			currentProgramDump_.clear();
			messages = programDumpCapability_->requestPatch(downloadNumber_);
			lastRequestNumber_ = downloadNumber_;
			// Send messages
			if (!messages.empty()) {
				synth_->sendBlockOfMessagesToSynth(midiOutput_->name(), messages);
			}
		}
	}

	bool ProgramDumpDownloadStrategy::requestSuccessful()
	{
		return downloadNumber_ == lastRequestNumber_ + 1;
	}

	void ProgramDumpDownloadStrategy::deinit()
	{
		if (!handle_.isNull()) {
			MidiController::instance()->removeMessageHandler(handle_);
		}
	}

	void ProgramDumpDownloadStrategy::handleNextProgramBuffer(const juce::MidiMessage& editBuffer, MidiBankNumber bankNo)
	{
		if (programDumpCapability_) {
			// This message might be a part of a multi-message program dump?
			if (programDumpCapability_->isMessagePartOfProgramDump(editBuffer)) {
				currentProgramDump_.push_back(editBuffer);
			}
			if (programDumpCapability_->isSingleProgramDump(currentProgramDump_)) {
				// Ok, that worked, save it and continue!
				std::copy(currentProgramDump_.begin(), currentProgramDump_.end(), std::back_inserter(currentDownload_));

				// Finished?
				if (downloadNumber_ >= endDownloadNumber_) {
					auto patches = synth_->loadSysex(currentDownload_);
					promiseOfPatches_.set_value(patches);
					if (progressHandler_) progressHandler_->onSuccess();
				}
				else if (progressHandler_ && progressHandler_->shouldAbort()) {
					deinit();
					progressHandler_->onCancel();
				}
				else {
					downloadNumber_++;
					if (progressHandler_) progressHandler_->setProgressPercentage((downloadNumber_ - startDownloadNumber_) / (double)synth_->numberOfPatches());
				}
			}
		}
	}

}
