#include "DownloadStrategy.h"
#include "MidiController.h"

#include "Logger.h"

namespace midikraft {

	void midikraft::Downloader::executeDownload(DownloadStrategy& strategy, std::function<void()> callback)
	{
		// Create a thread
		supervisor_ = std::make_unique<Downloader::DownloadSupervisionThread>();
		supervisor_->addListener(this);
		supervisor_->startThread();
	}

	void midikraft::Downloader::DownloadSupervisionThread::run()
	{
		// This thread supervises the progress of the download performed by the DownloadStrategy
		// It takes care of retries and timeouts
		// Get us a WaitableEvent from the MidiController - that will wake us up after a MidiMessage has been processed by the handlers
		auto& waitUntilNextMidiMessage = MidiController::instance()->registerWakeupCall();
		try {
			int retryCount = 0;
			bool isRetry = false;
			do {
				// Issue a request. The Strategy will have registered MidiMessage handlers processing the result. We only monitor the progress
				auto requestStarted = Time::getHighResolutionTicks();
				if (!isRetry) {
					retryCount = 0;
				}
				else {
					retryCount++;
					if (retryCount > 5) {
						SimpleLogger::instance()->postMessage("Download failed after 5 retries, giving up");
						break;
					}
				}
				strategy_.request();

				// Wait for the request to be successfully processed, or a timeout
				bool nextRequest = false;
				do {
					// Wait for a certain time on a MIDI message coming in
					if (waitUntilNextMidiMessage.wait(100)) {
						// The next MIDI Message occured. Check if our strategy is happy so we can reset the timeout counter and issue the next request
						if (strategy_.requestSuccessful()) {
							nextRequest = true;
							isRetry = false;
						}
						else {
							// Check if the request has lost patience
							if (((Time::getHighResolutionTicks() - requestStarted) / (double)Time::getHighResolutionTicksPerSecond()) > 0.5) {
								nextRequest = true;
								isRetry = true;
							}
						}
					}
				} while (!threadShouldExit() && !nextRequest);
			} while (!threadShouldExit() && !strategy_.finished());
		}
		catch (std::exception const& e) {
			SimpleLogger::instance()->postMessage("Error during download, caught exception: " + String(e.what()));
		}
		MidiController::instance()->removeWakeupCall(waitUntilNextMidiMessage);
	}

	void Downloader::exitSignalSent()
	{
		supervisor_->removeListener(this);
		if (callback_) {
			callback_();
		}
	}

}