#pragma once

#include "JuceHeader.h"

#include "Patch.h"
#include <future>

namespace midikraft {

	typedef std::promise<std::vector<std::shared_ptr<DataFile>>> TPromiseOfPatches;

	class DownloadStrategy {
	public:
		virtual void request() = 0;
		virtual bool requestSuccessful() = 0;
		virtual bool finished() = 0;
	};


	class Downloader : private Thread::Listener {
	public:
		void executeDownload(DownloadStrategy& strategy, std::function<void()> callback);

	private:
		void exitSignalSent() override;

		class DownloadSupervisionThread : public Thread {
		public:
			void run() override;

		private:
			DownloadStrategy &strategy_;
		};

		DownloadStrategy& strategy_;
		std::unique_ptr<DownloadSupervisionThread> supervisor_;
		std::function<void()> callback_;
	};

}

